#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <vector>
#include <SPIFFS.h>

// —— YH-T7E SERIAL CONFIG —— 
#define RX2_PIN       25      // D25 on your ESP32
#define SERIAL2_BAUD  9600    // match your indicator’s baud

// —— WIFI & SERVER ——
const char* ssid       = "ali";
const char* password   = "12345679";
const char* ENDPOINT   = "http://10.17.71.91:8082/api/dieselTank/quantity";

// —— DEVICE INFO —— 
const char* DEVICE_ID  = "1";
const char* FARM_ID    = "10";

// —— OFFLINE QUEUE —— 
static std::vector<String> pendingPosts;

// —— NTP (PKT) OFFSETS —— 
const long  GMT_OFFSET_SEC      = 5 * 3600;  // UTC+5
const int   DAYLIGHT_OFFSET_SEC = 0;

// —— STATE —— 
float lastReading = 0.0;
bool  timeSynced  = false;

// ─── QUEUE PERSISTENCE ───
void loadQueue() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  File f = SPIFFS.open("/queue.txt", "r");
  if (!f) return;
  while (f.available()) {
    pendingPosts.push_back(f.readStringUntil('\n'));
  }
  f.close();
}

void saveQueue() {
  File f = SPIFFS.open("/queue.txt.tmp", "w");
  if (!f) {
    Serial.println("Failed to open temp queue file");
    return;
  }
  for (auto &b : pendingPosts) {
    f.println(b);
  }
  f.close();
  SPIFFS.remove("/queue.txt");
  SPIFFS.rename("/queue.txt.tmp", "/queue.txt");
}

// ─── HTTP POST ───
bool sendHttp(const String &body) {
  HTTPClient http;
  http.begin(ENDPOINT);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();
  Serial.printf("  → HTTP %d\n", code);
  return (code > 0 && code < 400);
}

// ─── FLUSH QUEUE ───
void flushPending() {
  if (WiFi.status() != WL_CONNECTED) return;
  while (!pendingPosts.empty()) {
    String &body = pendingPosts.front();
    Serial.println("Flushing queued post:\n" + body);
    if (sendHttp(body)) {
      pendingPosts.erase(pendingPosts.begin());
      saveQueue();
    } else {
      Serial.println("Flush failed, will retry later");
      break;
    }
  }
}

// ─── SETUP WIFI + NTP ───
void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts++ < 10) {
    delay(250);
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
    timeSynced = true;
    // flush any queued posts
    flushPending();
  } else {
    Serial.println("\nWiFi FAILED");
  }
}

// ─── READ WEIGHT WITH TIMEOUT ───
// Blocks up to timeoutMs, then returns lastReading
float readWeightWithTimeout(unsigned long timeoutMs = 500) {
  unsigned long start = millis();
  String frame;
  while (millis() - start < timeoutMs) {
    if (Serial2.available()) {
      char c = Serial2.read();
      if (c == '=') {
        frame = c;  // start new frame
      }
      else if (frame.length() > 0) {
        frame += c;
        if (frame.length() >= 8) {
          String raw = frame.substring(1);  // e.g. "00.0050"
          String rev;
          for (int i = raw.length() - 1; i >= 0; --i) {
            rev += raw.charAt(i);
          }
          float val = rev.toFloat();
          lastReading = val;
          return val;
        }
      }
    }
  }
  // timeout → return last valid
  return lastReading;
}

// ─── POST A WEIGHT READING ───
void postWeightReading(float weight) {
  // build timestamp
  struct tm ti;
  char ts[32] = "";
  if (timeSynced && getLocalTime(&ti)) {
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &ti);
  } else {
    Serial.println("Time not ready, will queue without timestamp");
  }

  String body = "{"
    "\"deviceId\":"   + String(DEVICE_ID) +
    ",\"farmId\":"    + String(FARM_ID) +
    ",\"quantity\":"  + String(weight, 2) +
    ",\"deviceTime\":\""+ String(ts) + "\"" +
    "}";

  Serial.println("Queueing post: " + body);
  if (WiFi.status() == WL_CONNECTED) {
    flushPending();
    if (!sendHttp(body)) {
      pendingPosts.push_back(body);
      saveQueue();
    }
  } else {
    pendingPosts.push_back(body);
    saveQueue();
  }
}

void setup() {
  Serial.begin(2400);
  while (!Serial) {}

  // init SPIFFS & load any saved queue
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount file system");
  }
  loadQueue();

  // start UART2 for RS-232
  Serial2.begin(SERIAL2_BAUD, SERIAL_8N1, RX2_PIN, -1);
  Serial2.setRxInvert(true);   // invert RS-232 idle

  // connect Wi-Fi & sync time
  setupWifi();
}

void loop() {
  static unsigned long lastPostMillis = 0;

  // reconnect & flush when possible
  if (WiFi.status() != WL_CONNECTED) {
    setupWifi();
  } else {
    flushPending();
  }

  // sample weight (updates lastReading)
  float weight = readWeightWithTimeout();
  Serial.printf("Weight: %.2f\n", weight);

  // post once a minute
  unsigned long now = millis();
  if (lastPostMillis == 0 || now - lastPostMillis >= 60UL * 1000UL) {
    postWeightReading(lastReading);
    lastPostMillis = now;
  }
  
}
