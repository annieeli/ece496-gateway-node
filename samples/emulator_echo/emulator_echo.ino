void setup() {
  Serial.begin(115200);  /* USB Serial (UART0) */
  while (!Serial);
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    Serial.print("Received: ");
    Serial.println(input);
  }

  delay(100);
}
