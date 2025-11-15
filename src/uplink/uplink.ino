#include <atomic>
#include "driver/uart.h"
#include "esp_sleep.h"

#define THRESHOLD 40
#define BUF_SIZE  4096
#define DATA_SIZE 1
#define LED_PIN   2
#define RDY_PIN   26
#define WAKE_PIN  27
#define LIGHT_SLEEP_TIMEOUT (10000 * 1000ULL)

enum Msg : uint8_t { 
  MSG_BUFFER_FULL, 
  MSG_END_DCP, 
  MSG_LIGHT_SLEEP, 
  MSG_DEEP_SLEEP 
};

TaskHandle_t main_task_handle = NULL;
TaskHandle_t read_data_task_handle = NULL;
TaskHandle_t upload_data_task_handle = NULL;
TaskHandle_t power_manager_task_handle = NULL;

QueueHandle_t dcpQueue  = NULL;
QueueHandle_t powerQueue = NULL;

std::atomic<bool> dataCollectionActive {false};
std::atomic<bool> dataUploadingActive {false};

uint8_t dataBufferA[BUF_SIZE];
uint8_t dataBufferB[BUF_SIZE];
uint8_t *activeBuffer = dataBufferA;
uint8_t *uploadBuffer = NULL;
static uint8_t data[DATA_SIZE];
int offset = 0;
uint8_t command;

enum DOWNLINK_MESSAGES {
  INIT = 1,
  INTENT,
  DCP,
  END_DCP
};

void init_uart() {
  const uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity    = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };

  uart_param_config(UART_NUM_1, &uart_config);
  uart_set_pin(UART_NUM_1, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);
}

void tear_down() {
  Serial.println("Entering deep sleep");
  // flush() blocks the program until Serial is done
  // This prevents deep sleep from being interfered by Serial
  Serial.flush();

  digitalWrite(LED_PIN, LOW);
  esp_deep_sleep_start();
}

static void enter_light_sleep_ms() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // Want to differentiate between waking for different pin type,
  esp_sleep_enable_ext0_wakeup((gpio_num_t) WAKE_PIN, 1);
  uint64_t wake_pin_mask = (1ULL << RDY_PIN);
  esp_sleep_enable_ext1_wakeup(wake_pin_mask, ESP_EXT1_WAKEUP_ANY_HIGH);

  Serial.println("Entering light sleep");
  Serial.flush();
  esp_light_sleep_start();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  switch (cause)
  {
  case 2:
    Serial.printf("Woke up due to WAKE_PIN\n");
    break;

  case 3:
    Serial.printf("Woke up due to RDY_PIN\n");
    break;
  
  default:
    Serial.printf("Unknown wakeup reason\n");
    break;
  }
}

// task to read data from uart
void read_data_sub_task(void *pvParameters) {
  while(true) {
    dataCollectionActive = true;
    Serial.println("Task 1: Reading data from UART and storing in buffer...");
    
    // obtain data
    // TODO: currently using 5sec timeout for ease of dev/debug with emulator
    // TODO: potentially need to trim/sanitize data
    int len = uart_read_bytes(UART_NUM_1, &data, DATA_SIZE, pdMS_TO_TICKS(5000));
    
    // notify DTT if buffer is full
    if (len > 0) {
      if ((offset + len) >= BUF_SIZE) {
        // amount that fits in current buffer
        int bytesToFill = BUF_SIZE - offset;
        memcpy(activeBuffer + offset, data, bytesToFill);
        Serial.println("Buffer full, fill up rest of buffer");

        // notify upload
        Msg bufferFullMsg = MSG_BUFFER_FULL;
        uploadBuffer = activeBuffer;
        vTaskResume(upload_data_task_handle);
        xQueueSend(dcpQueue, &bufferFullMsg, portMAX_DELAY);
        Serial.println("Notify Upload Task");

        // swap buffers
        activeBuffer = (activeBuffer == dataBufferA) ? dataBufferB : dataBufferA;
        offset = 0;
        Serial.println("Buffer swapped");

        // write remaining data to new buffer
        int remaining = len - bytesToFill;
        if (remaining > 0) {
          memcpy(activeBuffer + offset, data + bytesToFill, remaining);
          offset += remaining;
        }
        Serial.println("Fill up remaining of data to new buffer");
      } else {
        // fits fully
        memcpy(activeBuffer + offset, data, len);
        offset += len;
        Serial.println("Fill up buffer");
      }
    }

    // go to light sleep after finishing
    dataCollectionActive = false;
    Msg sleepMode = MSG_LIGHT_SLEEP;
    vTaskResume(power_manager_task_handle);
    xQueueSend(powerQueue, &sleepMode, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(10));
    vTaskSuspend(NULL);
  }
}

// upload data to cloud
void upload_data_sub_task(void *pvParameters) {
  Msg data;
  while(true) {
    // receive data buffer to upload
    if (xQueueReceive(dcpQueue, &data, portMAX_DELAY)){
      dataUploadingActive = true;
      Serial.println("Task 2: Uploading data to the cloud...");
      // TODO: upload data to cloud

      // debug print buffer data
      Serial.printf("Buffer: ");
      for (size_t i = 0; i < offset; i++) {
        Serial.printf("%02X ", uploadBuffer[i]);
      }
      Serial.println();

      if(uploadBuffer != NULL){
        Serial.println("Upload buffer if not null");
        uploadBuffer = NULL;
      }
      dataUploadingActive = false;

      // go to light/deep sleep
      Msg sleepMode = (command == END_DCP) ? MSG_DEEP_SLEEP : MSG_LIGHT_SLEEP;
      vTaskResume(power_manager_task_handle);
      xQueueSend(powerQueue, &sleepMode, portMAX_DELAY);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void power_manager_task(void *pvParameters) {
  Msg sleepMode;
  while (true) {
    if (xQueueReceive(powerQueue, &sleepMode, portMAX_DELAY)) {
      if (sleepMode == MSG_LIGHT_SLEEP) {
        if (!dataCollectionActive && !dataUploadingActive) {
          Serial.println("Both idle, go to light sleep");
          enter_light_sleep_ms();
        }
      }
      else if (sleepMode == MSG_DEEP_SLEEP) {
        if (!dataCollectionActive && !dataUploadingActive) {
          Serial.println("DCP done, go to deep sleep");
          tear_down();
        }
      }
    }
  }
}

void dcp_manager_task(void *pvParameters) {
  dcpQueue = xQueueCreate(8, sizeof(Msg));
  powerQueue = xQueueCreate(8, sizeof(Msg));
  xTaskCreatePinnedToCore(
    read_data_sub_task,
    "read_data_sub_task",
    10000,
    NULL,
    1,
    &read_data_task_handle,
    0);
  vTaskSuspend(read_data_task_handle);

  xTaskCreatePinnedToCore(
    upload_data_sub_task,
    "upload_data_sub_task",
    10000,
    NULL,
    1,
    &upload_data_task_handle,
    1);

  xTaskCreatePinnedToCore(
    power_manager_task,
    "power_manager_task",
    10000,
    NULL,
    1,
    &power_manager_task_handle,
    1);

  Serial.println("\n--- Task: DCP Manager ---");

  enter_light_sleep_ms();

  while (true) {
    // TODO MAX 180 seconds
    get_command();

    // TODO: interrupt?
    if (digitalRead(RDY_PIN) == HIGH) {
      Serial.println("ReqData received. Starting data collection tasks.");
      vTaskResume(read_data_task_handle);
    } 
    
    else if (command == END_DCP) {
      Serial.println("END_DCP received.");
      Msg endDcpMsg = MSG_END_DCP;
      if (activeBuffer != NULL) {
        uploadBuffer = activeBuffer;
      }
      vTaskResume(upload_data_task_handle);
      xQueueSend(dcpQueue, &endDcpMsg, portMAX_DELAY);
    }
  }
}

void get_intent_task(void *pvParameters) {
  Serial.println("\n--- Task: GET_INTENT ---");
  Serial.println("Connecting to cloud...");
  delay(1000);
  Serial.println("Getting intent data and storing it.");
  delay(1000);
  tear_down();
}

void send_intent_task(void *pvParameters) {
  Serial.println("\n--- Task: SEND_INTENT ---");
  Serial.println("Sending intent data to downlink via UART.");

  // TODO: replace dummy buffer with intent once implemented
  uint8_t dummy_buffer[7];
  dummy_buffer[0] = 0xA5;
  dummy_buffer[1] = 0x5A;
  dummy_buffer[2] = 0x99;
  dummy_buffer[3] = (300 >> 24) & 0xFF;
  dummy_buffer[4] = (300 >> 16) & 0xFF;
  dummy_buffer[5] = (300 >> 8) & 0xFF;
  dummy_buffer[6] = 300 & 0xFF;

  uart_write_bytes(UART_NUM_1, (const char*)dummy_buffer, sizeof(dummy_buffer));
  tear_down();
}

void config_task(void *pvParameters) {
  Serial.println("\n--- Task: CONFIG ---");
  Serial.println("Device being configured...");
  delay(2000);
  Serial.println("Configuration finished.");
  tear_down();
}

void default_task(void *pvParameters) {
  Serial.println("default_task");
  delay(1000);
  tear_down();
}

// TODO: This is basically blocking, which is not great, there should be some timeout
void get_command() {
  int message_len = 0;
  while (message_len == 0) {
    message_len = uart_read_bytes(UART_NUM_1, &command, DATA_SIZE, pdMS_TO_TICKS(5000));
  }
}

void create_tasks_from_command(uint8_t command) {
  if (command == INIT) {
    xTaskCreate(get_intent_task, "GetIntentTask", 4096, NULL, 1, &main_task_handle);
  } else if (command == INTENT) {
    xTaskCreate(send_intent_task, "SendIntentTask", 4096, NULL, 1, &main_task_handle);
  } else if (command == DCP) {
    xTaskCreate(dcp_manager_task, "DCPManagerTask", 4096, NULL, 1, &main_task_handle);
  } else {
    Serial.println("Unknown command. Going back to sleep.");
    tear_down();
  }
}

void setup() {
  esp_sleep_enable_ext0_wakeup((gpio_num_t) WAKE_PIN, 1);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(RDY_PIN, INPUT);
  pinMode(WAKE_PIN, INPUT);

  Serial.begin(9600);
  init_uart();

  Serial.printf("\n\n--- Woke up from DEEP_SLEEP: Awaiting command (INIT - 0x1, INTENT - 0x2, DCP - 0x3) ---\n");
  get_command();
  Serial.printf("Command received: %d\n", command);

  // TODO: Config tasks calling

  create_tasks_from_command(command);
}

void loop() {
  vTaskDelete(NULL);
}

