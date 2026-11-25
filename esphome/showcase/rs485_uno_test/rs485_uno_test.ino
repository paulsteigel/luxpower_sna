// Uno as a fake RS485 weather sender on the HARDWARE UART (D0/D1).
// Wiring: module VCC->5V, module TXD->D0 (RX), module RXD->D1 (TX), module GND->GND.
// D0/D1 are shared with USB: unplug those two wires while uploading.
// LED 13 blinks on every byte received back (the ESP's "OK").
void setup() {
  Serial.begin(9600);  // must match uart: baud_rate in the ESP yaml
  pinMode(LED_BUILTIN, OUTPUT);
}
void loop() {
  static uint32_t last = 0;
  if (millis() - last > 2000) {
    last = millis();
    Serial.print("T="); Serial.print(25 + random(-30, 30) / 10.0, 1);
    Serial.print(",H="); Serial.print(70 + random(-100, 100) / 10.0, 1);
    Serial.print(",R="); Serial.println(random(2));
  }
  if (Serial.available()) {
    Serial.read();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}
