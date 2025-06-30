#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <vector>

// —— YH-T7E SERIAL CONFIG ——
#define RX2_PIN       25      // D25 on your ESP32
#define SERIAL2_BAUD  9600    // match your indicator’s baud

// —— WIFI & SERVER ——
// const char* ssid     = "IT Department";
// const char* password = "secure@1";
const char* ssid = "ali";
const char* password = "12345679";
const char* ENDPOINT = "http://155.135.1.87:8082/api/dieselTank/quantity";

// —— DEVICE INFO (FKs) ——
const char* DEVICE_ID = "1";
const char* FARM_ID   = "10";

// —— OFFLINE QUEUE ——
static std::vector<String> pendingPosts;

// —— NTP (PKT) OFFSETS ——
const long  GMT_OFFSET_SEC      = 5 * 3600;  // UTC+5
const int   DAYLIGHT_OFFSET_SEC = 0;

// ─── POST JSON ───
bool sendHttp(const String &body) {
  HTTPClient http;
  http.begin(ENDPOINT);
  http.addHeader("Content-Type","application/json");
  int code = http.POST(body);
  http.end();
  Serial.printf("  → HTTP %d\n", code);
  return (code > 0 && code < 400);
}

// ─── FLUSH QUEUE ───
void flushPending() {
  if (WiFi.status() != WL_CONNECTED) return;
  for (auto &q : pendingPosts) {
    Serial.println("Flushing queued post:\n" + q);
    if (!sendHttp(q)) break;
    pendingPosts.erase(pendingPosts.begin());
  }
}

// ─── SETUP WIFI + NTP ───
void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts++ < 10) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
    // sync to PKT
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
               "pool.ntp.org", "time.nist.gov");
    Serial.print("Syncing time");
    struct tm ti;
    while (!getLocalTime(&ti)) {
      Serial.print(".");
      delay(500);
    }
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
    Serial.println("\nTime now: " + String(buf));
    // flush any queued posts
    flushPending();
  } else {
    Serial.println("\nWiFi FAILED");
  }
}

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
}

// ─── POST A WEIGHT READING ───
void postWeightReading(float weight) {
  // get current PKT time
  struct tm ti;
  if (!getLocalTime(&ti)) {
    Serial.println("Time not ready, queuing reading");
  }
  char ts[32];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &ti);

  String body = String("{") +
    "\"deviceId\":"   + DEVICE_ID        + "," +
    "\"farmId\":"     + FARM_ID          + "," +
    "\"quantity\":"   + String(weight,2) + "," +
    "\"deviceTime\":\""+ String(ts)       + "\"" +
    "}";

  Serial.println("Posting: " + body);
  if (WiFi.status() != WL_CONNECTED) {
    pendingPosts.push_back(body);
    return;
  }
  // flush backlog then this one
  flushPending();
  if (!sendHttp(body)) {
    pendingPosts.push_back(body);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  // start UART2 for RS-232
  Serial2.begin(SERIAL2_BAUD, SERIAL_8N1, RX2_PIN, -1);
  Serial2.setRxInvert(true);   // invert RS-232 idle
  setupWifi();
}

void loop() {
  static unsigned long lastPostMillis = 0;
  // reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    setupWifi();
  }
  // 1) Read & display every frame immediately
  float weight = readWeightBlocking();
  Serial.printf("Weight: %.2f\n", weight);

  // 2) Post once a minute
  unsigned long now = millis();
  if (lastPostMillis == 0) {
    lastPostMillis = now;
  }
  if (now - lastPostMillis >= 60UL * 1000UL) {
    postWeightReading(weight);
    lastPostMillis = now;
  }
}
