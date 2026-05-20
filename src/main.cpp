#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[somfy2mqtt] hello world");
}

void loop() {
  Serial.println("tick");
  delay(1000);
}
