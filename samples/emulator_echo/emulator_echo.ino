HardwareSerial downlinkSerial(2);

void setup() {
  Serial.begin(115200);  /* USB Serial (UART0) for debug printing */
  downlinkSerial.begin(115200, SERIAL_8N1, 16, 17); /* UART2 using GPIO pins 16, 17 */
}

void loop() {
  if (downlinkSerial.available()) {
    String input = downlinkSerial.readStringUntil('\n');

    Serial.print("Received from Uplink: ");
    Serial.println(input);
    Serial.println("Sending ACK.");

    downlinkSerial.println("MESSAGE ACK");
  }

  delay(100);
}
