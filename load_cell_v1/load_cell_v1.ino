#include <vector>

// —— YH-T7E SERIAL CONFIG ——
#define RX2_PIN             // D25 on your ESP32
#define SERIAL2_BAUD  9600    // Match your indicator’s baud

// ─── READ ONE COMPLETE WEIGHT FRAME ───
// Blocks until “=00.0050” or “=00.005-” arrives, then returns as float
float readWeightBlocking() {
  String frame;
  while (true) {
    if (Serial2.available()) {
      char c = Serial2.read();
      if (c == '=') {
        frame = c;              // start new frame
      }
      else if (frame.length() > 0) {
        frame += c;
        if (frame.length() >= 8) {
          String raw = frame.substring(1);  // e.g. "00.0050"
          String rev;
          for (int i = raw.length() - 1; i >= 0; --i) {
            rev += raw.charAt(i);
          }
          return rev.toFloat();  // includes sign if “-”
        }
      }
    }
  }
}                                                                                                                                                   \a

void setup() {
  Serial.begin(115200);          // ← Serial monitor should use 115200 baud
  while (!Serial) {}
  Serial2.begin(SERIAL2_BAUD, SERIAL_8N1, RX2_PIN, -1);
  Serial2.setRxInvert(true);     // invert RS-232 idle
}

void loop() {
  float weight = readWeightBlocking();
  Serial.printf("Weight: %.2f\n", weight);
}
