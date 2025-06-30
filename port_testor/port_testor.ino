/*
  ESP32 Pin-Scanner
  ------------------------------------------------
  Prints the GPIO number and new level every time
  any monitored pin changes state.
*/

#include <Arduino.h>

// ⚠️  Pins 6-11 are wired to the on-board SPI-flash – never touch them.
//     The list below skips those and other “special” lines.
const uint8_t pins[] = {
  0, 2, 4, 5,           // usable bootstraps (OK if not held LOW at reset)
  12, 13, 14, 15,       // works on most boards
  16, 17, 18, 19,
  21, 22, 23,
  25, 26, 27,
  32, 33,               // DAC / ADC / touch
  34, 35, 36, 39        // input-only (no pull-ups/downs)
};
const uint8_t NUM_PINS = sizeof(pins) / sizeof(pins[0]);

bool lastState[NUM_PINS];

void setup() {
  Serial.begin(115200);
  delay(200);                     // wait for the port to open

  for (uint8_t i = 0; i < NUM_PINS; ++i) {
    // Use a pulldown where supported to avoid floating inputs.
    // (Pins 34-39 ignore pull-ups/downs, but calling it is harmless.)
    pinMode(pins[i], INPUT_PULLDOWN);
    lastState[i] = digitalRead(pins[i]);
  }

  Serial.println(F("\n--- ESP32 GPIO-scanner is running ---"));
  Serial.println(F("Touch or drive your signal to a pin; watch the console."));
}

void loop() {
  for (uint8_t i = 0; i < NUM_PINS; ++i) {
    bool curr = digitalRead(pins[i]);
    if (curr != lastState[i]) {           // state changed
      Serial.printf("GPIO %d → %d\n", pins[i], curr);
      lastState[i] = curr;
    }
  }
  delay(50);   // tame the serial spam – 20 Hz is plenty
}
