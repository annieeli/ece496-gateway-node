#include <Arduino.h>

enum State {
  DEEP_SLEEP,
  GET_INTENT,
  SEND_INTENT,
  COLLECT_AND_TRANSMIT,
  LIGHT_SLEEP,
  CONFIG
};

State currentState = DEEP_SLEEP;

TaskHandle_t readDataTaskHandle = NULL;
TaskHandle_t uploadDataTaskHandle = NULL;

volatile bool dataCollectionActive = false;
unsigned long lightSleepStartTime = 0;
const unsigned long lightSleepTimeout = 10000; 

void readDataTask(void *pvParameters) {
  for (;;) {
    if (dataCollectionActive) {
      Serial.println("Task 1: Reading data from UART and storing in buffer...");
      delay(700);
    } else {
      vTaskSuspend(NULL);
    }
  }
}

void uploadDataTask(void *pvParameters) {
  for (;;) {
    if (dataCollectionActive) {
      Serial.println("Task 2: Uploading data to the cloud...");
      delay(1000);
    } else {
      vTaskSuspend(NULL);
    }
  }
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;
  }
  
  xTaskCreatePinnedToCore(
    readDataTask,
    "ReadDataTask",
    10000,
    NULL,
    1,
    &readDataTaskHandle,
    0);

  xTaskCreatePinnedToCore(
    uploadDataTask,
    "UploadDataTask",
    10000,
    NULL,
    1,
    &uploadDataTaskHandle,
    1);

  vTaskSuspend(readDataTaskHandle);
  vTaskSuspend(uploadDataTaskHandle);
}

void loop() {
  switch (currentState) {
    case DEEP_SLEEP:
      Serial.println("\n--- DEEP_SLEEP: Awaiting command (INIT, INTENT, DCP, CONFIG) ---");
      if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command == "INIT") {
          currentState = GET_INTENT;
        } else if (command == "INTENT") {
          currentState = SEND_INTENT;
        } else if (command == "DCP") {
          currentState = COLLECT_AND_TRANSMIT;
        } else if (command == "CONFIG") {
          currentState = CONFIG;
        } else {
          Serial.println("Unknown command.");
        }
      }
      delay(1000);
      break;

    case CONFIG:
      Serial.println("\nState: CONFIG");
      Serial.println("Device being configured...");
      delay(2000);
      Serial.println("Configuration finished.");
      currentState = DEEP_SLEEP;
      break;

    case GET_INTENT:
      Serial.println("\nState: GET_INTENT");
      Serial.println("Connecting to cloud...");
      delay(1000);
      Serial.println("Getting intent data and storing it.");
      delay(1000);
      currentState = DEEP_SLEEP;
      break;

    case SEND_INTENT:
      Serial.println("\nState: SEND_INTENT");
      Serial.println("Sending intent data to other MCU via UART.");
      delay(1000);
      currentState = DEEP_SLEEP;
      break;

    case COLLECT_AND_TRANSMIT:
       Serial.println("\nState: COLLECT_AND_TRANSMIT");
       currentState = LIGHT_SLEEP;
       break;

    case LIGHT_SLEEP:
      if(lightSleepStartTime == 0) {
        Serial.println("--- Entering LIGHT_SLEEP state ---");
        Serial.println("Waiting for 'ReqData' or 'END_DCP' command...");
        lightSleepStartTime = millis();
      }

      if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command == "ReqData") {
          Serial.println("ReqData received. Starting data collection tasks.");
          dataCollectionActive = true;
          vTaskResume(readDataTaskHandle);
          vTaskResume(uploadDataTaskHandle);
          
          delay(5000); 
          
          Serial.println("No more data. Suspending tasks.");
          dataCollectionActive = false;
          lightSleepStartTime = 0; 
        } else if (command == "END_DCP") {
          Serial.println("END_DCP received.");
          currentState = DEEP_SLEEP;
          lightSleepStartTime = 0;
        }
      }

      if (millis() - lightSleepStartTime > lightSleepTimeout && lightSleepStartTime != 0) {
        Serial.println("Light sleep timeout reached.");
        currentState = DEEP_SLEEP;
        lightSleepStartTime = 0;
      }
      break;
  }
}
