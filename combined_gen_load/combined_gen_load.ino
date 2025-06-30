#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <vector>

// —— YH-T7E SERIAL CONFIG ——
#define RX2_PIN       16      // D25 on your ESP32
#define SERIAL2_BAUD  9600    // match your indicator’s baud

// —— POWER-SOURCE PINS ——
#define WAPDA_PIN     32
#define GEN_PIN       33

// —— WIFI & ENDPOINTS ——
const char* ssid             = "ali";
const char* password         = "12345679";

// for load-cell quantity posts
const char* QUANTITY_ENDPOINT = "http://10.17.71.91:8082/api/dieselTank/quantity";
// for power-state posts
const char* POWER_ENDPOINT    = "http://10.17.71.91:8082/api/power/state";

// —— DEVICE INFO ——
const char* DEVICE_ID_Q      = "1";   // quantity device
const char* DEVICE_ID_P      = "1";  // power-state device
const char* FARM_ID          = "10";  // same for both

// —— OFFLINE QUEUES ——
static std::vector<String> pendingQuantityPosts;
static std::vector<String> pendingPowerPosts;

// —— NTP (PKT) OFFSETS ——
const long GMT_OFFSET_SEC      = 5 * 3600;  // UTC+5
const int  DAYLIGHT_OFFSET_SEC = 0;         // no DST

// —— FRAME-AVERAGING BUFFER ——
static float frameBuffer[10];
static int   frameCount       = 0;
static int   frameIndex       = 0;
static float frameSum         = 0;
static float lastAvgReading   = 0;

// —— POWER STATE LOGIC ——
bool     lastWapda    = LOW;
bool     lastGen      = LOW;
unsigned long startWapda = 0;
unsigned long startGen   = 0;
bool     firstLoop    = true;
int      wapdaCounter = 0;
int      genCounter   = 0;
const int STABLE_COUNT = 14;    // require 5 consecutive reads

// ─── DECLARE FORWARD ──────────────────────────────────────────────────────────
bool sendHttpQuantity(const String &body);
void flushPendingQuantity();
bool sendHttpPower(const String &body);
void flushPendingPower();

void setupWifi();

// ─── SERIAL FRAME PARSING & AVERAGING ────────────────────────────────────────
void processSerialFrames() {
  static String frameBuf;
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '=') {
      frameBuf = "=";
    } else if (frameBuf.length() > 0) {
      frameBuf += c;
      if (frameBuf.length() == 8) {
        String raw = frameBuf.substring(1);  
        String rev;
        for (int i = raw.length() - 1; --i >= 0;) rev += raw.charAt(i);
        float val = rev.toFloat();
        if (frameCount < 7) {
          frameSum += val;
          frameBuffer[frameIndex] = val;
          frameCount++;
        } else {
          frameSum -= frameBuffer[frameIndex];
          frameBuffer[frameIndex] = val;
          frameSum += val;
        }
        frameIndex = (frameIndex + 1) % 7;
        lastAvgReading = frameSum / frameCount;
        frameBuf = "";
      }
    }
  }
}

// ─── QUANTITY POSTING ────────────────────────────────────────────────────────
void postWeightReading(float weight) {
  struct tm ti;
  char ts[32] = "";
  if (getLocalTime(&ti)) {
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &ti);
  } else {
    Serial.println("Time not ready, queuing reading");
  }
  String body = "{"
    "\"deviceId\":"   + String(DEVICE_ID_Q) +
    ",\"farmId\":"    + String(FARM_ID) +
    ",\"quantity\":"  + String(weight, 2) +
    ",\"deviceTime\":\""+ String(ts) + "\"" +
    "}";
  Serial.println("Queueing quantity post: " + body);
  if (WiFi.status() == WL_CONNECTED) {
    flushPendingQuantity();
    if (!sendHttpQuantity(body)) {
      pendingQuantityPosts.push_back(body);
    }
  } else {
    pendingQuantityPosts.push_back(body);
  }
}

bool sendHttpQuantity(const String &body) {
  HTTPClient http;
  http.begin(QUANTITY_ENDPOINT);
  http.addHeader("Content-Type","application/json");
  int code = http.POST(body);
  http.end();
  Serial.printf("  → Quantity HTTP %d\n", code);
  return (code > 0 && code < 400);
}

void flushPendingQuantity() {
  if (WiFi.status() != WL_CONNECTED) return;
  while (!pendingQuantityPosts.empty()) {
    String &q = pendingQuantityPosts.front();
    Serial.println("Flushing queued quantity post:\n" + q);
    if (sendHttpQuantity(q)) {
      pendingQuantityPosts.erase(pendingQuantityPosts.begin());
    } else {
      break;
    }
  }
}

// ─── POWER-STATE POSTING ────────────────────────────────────────────────────
void postStatus(int powerSourceId, int currentStatus, unsigned long durationMs) {
  struct tm tp;
  if (!getLocalTime(&tp)) {
    Serial.println("Failed to obtain time, queuing power post");
    return;
  }
  char ts[25];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tp);
  String body = String("{") +
    "\"deviceId\":\""    + DEVICE_ID_P          + "\"," +
    "\"farmId\":"        + String(FARM_ID)      + "," +
    "\"powerSourceId\":" + String(powerSourceId)+ "," +
    "\"currentStatus\":" + String(currentStatus)+ "," +
    "\"durationMs\":"    + String(durationMs)   + "," +
    "\"deviceTime\":\""  + String(ts)           + "\"" +
    "}";
  Serial.printf("Queueing power post src=%d status=%d dur=%lums\n",
                powerSourceId, currentStatus, durationMs);
  if (WiFi.status() != WL_CONNECTED) {
    pendingPowerPosts.push_back(body);
    return;
  }
  flushPendingPower();
  if (!sendHttpPower(body)) {
    pendingPowerPosts.push_back(body);
  }
}

bool sendHttpPower(const String &body) {
  HTTPClient http;
  http.begin(POWER_ENDPOINT);
  http.addHeader("Content-Type","application/json");
  int code = http.POST(body);
  http.end();
  Serial.printf("  → Power HTTP %d\n", code);
  return (code > 0 && code < 400);
}

void flushPendingPower() {
  if (WiFi.status() != WL_CONNECTED) return;
  for (auto &body : pendingPowerPosts) {
    Serial.println("Flushing queued power post:\n" + body);
    if (!sendHttpPower(body)) {
      Serial.println("  flush failed, keeping in queue");
      return;
    }
  }
  pendingPowerPosts.clear();
  Serial.println("All pending power posts flushed.");
}

// ─── WIFI & NTP SETUP ───────────────────────────────────────────────────────
void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
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
    // flush both queues
    flushPendingQuantity();
    flushPendingPower();
  } else {
    Serial.println("\nWiFi FAILED");
  }
}

// ─── SETUP & MAIN LOOP ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  // UART2 for load-cell
  Serial2.begin(SERIAL2_BAUD, SERIAL_8N1, RX2_PIN, -1);
  Serial2.setRxInvert(true);

  // Power-sensor pins
  pinMode(WAPDA_PIN, INPUT);
  pinMode(GEN_PIN,   INPUT);

  // Wi-Fi + NTP
  setupWifi();

  // initialize power timers
  lastWapda = digitalRead(WAPDA_PIN);
  lastGen   = digitalRead(GEN_PIN);
  startWapda= millis();
  startGen  = millis();
}

void loop() {
  static unsigned long lastWeightPost = 0;
  unsigned long now = millis();

  // 1) Maintain Wi-Fi & flush both queues
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, retrying...");
    setupWifi();
  } else {
    flushPendingQuantity();
    flushPendingPower();
  }

  // 2) Handle load-cell frames & average
  processSerialFrames();
  float avgWeight = lastAvgReading;
  Serial.printf("Avg Weight: %.2f\n", avgWeight);

  // 3) Post weight once a minute
  if (lastWeightPost == 0 || now - lastWeightPost >= 60UL * 1000UL) {
    postWeightReading(avgWeight);
    lastWeightPost = now;
  }

  // 4) Power-source debounce & change detection
  bool currWapda = digitalRead(WAPDA_PIN) == HIGH;
  bool currGen   = digitalRead(GEN_PIN)   == HIGH;

  if (firstLoop) {
    firstLoop = false;
  } else {
    // WAPDA
    if (currWapda != lastWapda) {
      if (++wapdaCounter >= STABLE_COUNT) {
        if (currWapda) {
          startWapda = now;
          postStatus(3, 1, 0);
        } else {
          postStatus(3, 0, now - startWapda);
        }
        lastWapda = currWapda;
        wapdaCounter = 0;
      }
    } else wapdaCounter = 0;

    // Generator
    if (currGen != lastGen) {
      if (++genCounter >= STABLE_COUNT) {
        if (currGen) {
          startGen = now;
          postStatus(4, 1, 0);
        } else {
          postStatus(4, 0, now - startGen);
        }
        lastGen = currGen;
        genCounter = 0;
      }
    } else genCounter = 0;
  }

  Serial.print("Raw WAPDA: "); Serial.print(currWapda);
  Serial.print("  Raw Gen: ");   Serial.println(currGen);

  //delay(1000);  // 1 s poll interval
}
