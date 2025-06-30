#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// —— PIN DEFINITIONS ——
#define WAPDA_PIN 32
#define GEN_PIN   33
#define PS_WAPDA       3 //brooder
#define PS_GENERATOR   4 //generator
#include <vector>

static std::vector<String> pendingPosts;
static bool wasWifiConnected = false;

// —— WIFI & SERVER ——
// const char* ssid     = "IT Department";
// const char* password = "secure@1";
// 
const char* ssid = "Hi-Tech-Farm";
const char* password = "secure@1";

// const char* ENDPOINT = "http://10.17.71.91:8082/api/power/state";
const char* ENDPOINT = "http://18.199.188.82:8082/api/power/state";

// —— DEVICE INFO ——
const char* DEVICE_ID = "50";
const char* FARM_ID   = "11"; 


// —— STATE & TIMERS ——
bool     lastWapda    = LOW;
bool     lastGen      = LOW;
unsigned long startWapda = 0;
unsigned long startGen   = 0;
bool     firstLoop    = true;

// —— DEBOUNCE COUNTERS ——
int wapdaCounter = 0;
int genCounter   = 0;
const int  STABLE_COUNT = 5;  // require 5 consecutive reads

void setup() {
  Serial.begin(115200);

  pinMode(WAPDA_PIN, INPUT);
  pinMode(GEN_PIN,   INPUT);

  // Initial Wi‑Fi connect
  Serial.print("Connecting WiFi");
  setupWifi();
  wasWifiConnected = (WiFi.status() == WL_CONNECTED);
}

void loop() {
  flushPending();
  // Reconnect if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, retrying...");
    setupWifi();
  }
  
  unsigned long now = millis();
  bool currWapda = digitalRead(WAPDA_PIN) == HIGH;
  bool currGen   = digitalRead(GEN_PIN)   == HIGH;

  // On first loop, just capture state & set timers
  if (firstLoop) {
    lastWapda    = currWapda;
    lastGen      = currGen;
    startWapda   = now;
    startGen     = now;
    firstLoop    = false;
    // No API call yet
  } else {
    // --- WAPDA debounce & change detection ---
    if (currWapda != lastWapda) {
      // possible change, increment counter
      wapdaCounter++;
      if (wapdaCounter >= STABLE_COUNT) {
        // confirmed stable change
        if (currWapda) {
          // OFF → ON
          startWapda = now;
          postStatus(PS_WAPDA, 1, 0);
        } else {
          // ON → OFF
          unsigned long dur = now - startWapda;
          postStatus(PS_WAPDA, 0, dur);
        }
        lastWapda = currWapda;
        wapdaCounter = 0;
      }
    } else {
      // reading matches last state, reset counter
      wapdaCounter = 0;
    }

    // --- GENERATOR debounce & change detection ---
    if (currGen != lastGen) {
      genCounter++;
      if (genCounter >= STABLE_COUNT) {
        if (currGen) {
          startGen = now;
          postStatus(PS_GENERATOR, 1, 0);
        } else {
          unsigned long dur = now - startGen;
          postStatus(PS_GENERATOR, 0, dur);
        }
        lastGen = currGen;
        genCounter = 0;
      }
    } else {
      genCounter = 0;
    }
  }

  // (Optional) print current raw readings
  Serial.print("Raw Bro: "); Serial.print(currWapda);
  Serial.print("  Raw Gen: ");   Serial.println(currGen);

  delay(1000);  // 100 ms poll interval
}

// —— YOUR ORIGINAL Wi‑Fi SETUP ——
void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, password);

  int attempts = 0;
  Serial.print(".");
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi reconnected — flushing pending posts");
    
    // ─── Sync to Pakistan Standard Time (UTC+5) ───
    const long gmtOffset_sec     = 5 * 3600;   // +5 hours
    const int  daylightOffset_sec = 0;         // no DST in Pakistan
    configTime(gmtOffset_sec, daylightOffset_sec,
               "pool.ntp.org", "time.nist.gov");

    // wait for time to be set
    Serial.print("Waiting for time sync");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
      Serial.print(".");
      delay(500);
    }
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.println(String("\nTime synced: ") + buf);

    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
    flushPending();
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

// —— POST JSON TO BACKEND ——
// void postChange(const char* src,
//                 const char* oldSt,
//                 const char* newSt,
//                 unsigned long dur) {
//   if (WiFi.status() != WL_CONNECTED) return;

//   String body = String("{") +
//     "\"deviceId\":\""  + DEVICE_ID + "\"," +
//     "\"farmId\":\""    + FARM_ID   + "\"," +
//     "\"source\":\""    + src       + "\"," +
//     "\"oldStatus\":\"" + oldSt     + "\"," +
//     "\"newStatus\":\"" + newSt     + "\"," +
//     "\"durationMs\":"  + dur       +
//     "}";

//   HTTPClient http;
//   http.begin(ENDPOINT);
//   http.addHeader("Content-Type", "application/json");
//   int code = http.POST(body);
//   Serial.printf("POST %s %s→%s dur=%lums → %d\n",
//                 src, oldSt, newSt, dur, code);
//   http.end();
// }
void postStatus(int powerSourceId, int currentStatus, unsigned long durationMs) {
  // get local (PKT) time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  char ts[25];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &timeinfo);

  // build JSON
  String body = String("{") +
    "\"deviceId\":\""      + DEVICE_ID             + "\"," +
    "\"farmId\":"          + String(FARM_ID)        + "," +
    "\"powerSourceId\":"   + String(powerSourceId)  + "," +
    "\"currentStatus\":"   + String(currentStatus)  + "," +
    "\"durationMs\":"      + String(durationMs)     + "," +
    "\"deviceTime\":\""    + String(ts)             + "\"" +
    "}";

  // if no Wi‑Fi, queue and return
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down—queuing post");
    pendingPosts.push_back(body);
    return;
  }

  // on reconnect, first flush any backlog
  flushPending();

  // send this one now
  Serial.printf("POST src=%d status=%d dur=%lums time=%s …\n",
                powerSourceId, currentStatus, durationMs, ts);
  if (!sendHttp(body)) {
    Serial.println("POST failed—queuing");
    pendingPosts.push_back(body);
  }
}
bool sendHttp(const String& body) {
  HTTPClient http;
  http.begin(ENDPOINT);
  http.addHeader("Content-Type","application/json");
  int code = http.POST(body);
  http.end();
  Serial.printf("  → HTTP %d\n", code);
  return (code > 0 && code < 400);
}
void flushPending() {
  if (WiFi.status() != WL_CONNECTED) return;
  for (auto &body : pendingPosts) {
    Serial.println("Flushing queued post:");
    Serial.println(body);
    if (!sendHttp(body)) {
      Serial.println("  failed again, keeping in queue");
      return;  // stop on first failure
    }
  }
  pendingPosts.clear();
  Serial.println("All pending posts flushed.");
}

