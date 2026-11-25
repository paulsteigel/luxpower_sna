// Uno as a standalone RS485 weather SLAVE (soak test, no PC needed on the bus).
// Answers every "?" line from the ESP with "T=..,H=..,R=..\n" at 9600.
// Wiring to a TTL-RS485 module (pins named from the MCU's point of view -> STRAIGHT):
//   module VCC->5V, GND->GND, TXD<-D3 (Uno TX), RXD->D2 (Uno RX); module A/B -> cable.
// D0/D1 stay free: the USB Serial Monitor (115200) shows a counter line every 30 s and any junk.
#include <SoftwareSerial.h>

SoftwareSerial rs(2, 3);  // RX, TX
char buf[24];
uint8_t n = 0;
uint32_t polls = 0, junk = 0, lastReport = 0;
float temp = 28.0, hum = 70.0;
uint8_t rain = 0;

void setup() {
  Serial.begin(115200);
  rs.begin(9600);  // must match the ESP's uart baud_rate
  randomSeed(analogRead(A0));
  Serial.println(F("slave up, waiting for '?'"));
}

void reply() {
  temp = constrain(temp + random(-5, 6) / 10.0, 10, 45);
  hum = constrain(hum + random(-20, 21) / 10.0, 20, 100);
  if (random(10) == 0) rain ^= 1;
  rs.print(F("T=")); rs.print(temp, 1);
  rs.print(F(",H=")); rs.print(hum, 1);
  rs.print(F(",R=")); rs.println(rain);
}

void loop() {
  while (rs.available()) {
    char c = rs.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (n < sizeof(buf) - 1) buf[n++] = c;
      else n = 0;  // overflow = noise, restart
      continue;
    }
    buf[n] = 0;
    if (n && buf[n - 1] == '?') {  // a poll, even with junk in front of it
      if (n > 1) junk++;
      polls++;
      delay(10);  // RS485 turnaround: the ESP's module is still in TX mode right after its "?"
      reply();
      delay(30);  // the module echoes our own reply back to RX: drop it so it can't glue onto the next poll
      while (rs.available()) rs.read();
      n = 0;
      continue;
    } else if (n) {
      junk++;
      Serial.print(F("JUNK: ")); Serial.println(buf);
    }
    n = 0;
  }
  if (millis() - lastReport > 30000) {
    lastReport = millis();
    Serial.print(F("up ")); Serial.print(millis() / 60000);
    Serial.print(F(" min, polls ")); Serial.print(polls);
    Serial.print(F(", junk ")); Serial.println(junk);
  }
}
