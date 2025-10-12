#define THRESHOLD 40   /* Greater the value, more the sensitivity */
#include<atomic>

const int LED_PIN = 2;

TaskHandle_t main_task_handle = NULL;
TaskHandle_t read_data_task_handle = NULL;
TaskHandle_t upload_data_task_handle = NULL;

std::atomic<bool> dataCollectionActive;
unsigned long lightSleepStartTime = 0;
const unsigned long lightSleepTimeout = 10000; 

RTC_DATA_ATTR int bootCount = 0;

void read_data_sub_task(void *pvParameters) {
  for (;;) {
    if (dataCollectionActive) {
      Serial.println("Task 1: Reading data from UART and storing in buffer...");
      delay(700);
    } else {
      vTaskSuspend(NULL);
    }
  }
}

void upload_data_sub_task(void *pvParameters) {
  for (;;) {
    if (dataCollectionActive) {
      Serial.println("Task 2: Uploading data to the cloud...");
      delay(1000);
    } else {
      vTaskSuspend(NULL);
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
  Serial.println("Sending intent data to other MCU via UART.");
  delay(1000);
  tear_down();
}

void config_task(void *pvParameters) {
  Serial.println("\n--- Task: CONFIG ---");
  Serial.println("Device being configured...");
  delay(2000);
  Serial.println("Configuration finished.");
  tear_down();
}

// TODO: This currently pretends to use light sleep but does not actually
void dcp_manager_task(void *pvParameters) {
  xTaskCreatePinnedToCore(
    read_data_sub_task,
    "read_data_sub_task",
    10000,
    NULL,
    1,
    &read_data_task_handle,
    0);

  xTaskCreatePinnedToCore(
    upload_data_sub_task,
    "upload_data_sub_task",
    10000,
    NULL,
    1,
    &upload_data_task_handle,
    1);

  Serial.println("\n--- Task: DCP Manager (Light Sleep Logic) ---");
  Serial.println("Waiting for 'ReqData' or 'END_DCP' command...");
  unsigned long lightSleepStartTime = millis();

  while (true) {
    String command = get_command();

    if (command == "ReqData") {
      Serial.println("ReqData received. Starting data collection tasks.");
      dataCollectionActive = true;
      vTaskResume(read_data_task_handle);
      vTaskResume(upload_data_task_handle);

      delay(5000);
      dataCollectionActive = false;
      Serial.println("Data collection period finished. Awaiting next command...");
      lightSleepStartTime = millis(); 

    } else if (command == "END_DCP") {
      Serial.println("END_DCP received.");
      tear_down();
    }

    if (millis() - lightSleepStartTime > lightSleepTimeout) {
      Serial.println("Light sleep timeout reached.");
      tear_down();
    }
    delay(100); 
  }
}

void default_task(void *pvParameters) {
  Serial.println("default_task");
  delay(1000);
  tear_down();
}

// TODO: This is basically blocking, which is not great, there should be some timeout
String get_command() {
  while (Serial.available() == 0) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  String command = Serial.readStringUntil('\n');
  if (command == NULL) {
    command = "";
  }
  command.trim();
  return command;
}

void print_wakeup_touchpad() {
  switch (esp_sleep_get_touchpad_wakeup_status()) {
    case 0:  Serial.println("Touch detected on GPIO 4"); break;
    case 1:  Serial.println("Touch detected on GPIO 0"); break;
    case 2:  Serial.println("Touch detected on GPIO 2"); break;
    case 3:  Serial.println("Touch detected on GPIO 15"); break;
    case 4:  Serial.println("Touch detected on GPIO 13"); break;
    case 5:  Serial.println("Touch detected on GPIO 12"); break;
    case 6:  Serial.println("Touch detected on GPIO 14"); break;
    case 7:  Serial.println("Touch detected on GPIO 27"); break;
    case 8:  Serial.println("Touch detected on GPIO 33"); break;
    case 9:  Serial.println("Touch detected on GPIO 32"); break;
    default: Serial.println("Wakeup not by touchpad"); break;
  }
}

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_UNDEFINED");
      break;
    case ESP_SLEEP_WAKEUP_ALL: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_ALL");
      break;
    case ESP_SLEEP_WAKEUP_EXT0: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_EXT0");
      break;
    case ESP_SLEEP_WAKEUP_EXT1: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_EXT1");
      break;
    case ESP_SLEEP_WAKEUP_TIMER: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_TIMER");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_TOUCHPAD");
      break;
    case ESP_SLEEP_WAKEUP_ULP: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_ULP");
      break;
    case ESP_SLEEP_WAKEUP_GPIO: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_GPIO");
      break;
    case ESP_SLEEP_WAKEUP_UART: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_UART");
      break;
    case ESP_SLEEP_WAKEUP_WIFI: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_WIFI");
      break;
    case ESP_SLEEP_WAKEUP_COCPU: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_COCPU");
      break;
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG");
      break;
    case ESP_SLEEP_WAKEUP_BT: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_BT");
      break;
    case ESP_SLEEP_WAKEUP_VAD: 
      Serial.println("reason: ESP_SLEEP_WAKEUP_VAD");
      break;
    default: 
      Serial.println("reason: default");
      break;
  }
}

void tear_down() {
  Serial.println("Going to sleep now");
  // flush() blocks the program until Serial is done
  // This prevents deep sleep from being interfered by Serial
  Serial.flush();

  digitalWrite(LED_PIN, LOW);
  esp_deep_sleep_start();
}

void create_tasks_from_command(String command) {
  if (command == "INIT") {
    xTaskCreate(get_intent_task, "GetIntentTask", 4096, NULL, 1, &main_task_handle);
  } else if (command == "INTENT") {
    xTaskCreate(send_intent_task, "SendIntentTask", 4096, NULL, 1, &main_task_handle);
  } else if (command == "DCP") {
    xTaskCreate(dcp_manager_task, "DCPManagerTask", 4096, NULL, 1, &main_task_handle);
  } else if (command == "CONFIG") {
    xTaskCreate(config_task, "ConfigTask", 4096, NULL, 1, &main_task_handle);
  } else {
    Serial.println("Unknown command. Going back to sleep.");
    tear_down();
  }
}

void setup() {
  touchSleepWakeUpEnable(T3, THRESHOLD);

  Serial.begin(9600);
  // Serial.setTimeout(1000 * 10);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.println();
  // ++bootCount;
  // Serial.println("Boot number: " + String(bootCount));

  Serial.println("\n--- Woke up from DEEP_SLEEP: Awaiting command (INIT, INTENT, DCP, CONFIG) ---");
  String command = get_command();

  Serial.println("Command received: " + command);
  print_wakeup_reason();
  create_tasks_from_command(command);
}

void loop() {
  vTaskDelete(NULL);
}

