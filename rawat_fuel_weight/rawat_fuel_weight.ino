#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <vector>

// —— YH-T7E SERIAL CONFIG ——
#define RX2_PIN       16      // D25 on your ESP32
#define SERIAL2_BAUD  9600    // match your indicator’s baud

// —— WIFI & SERVER ——
//const char* ssid     = "Jazz 4G CPE_FECD";
//const char* password = "34278261";
// const char* ssid     = "Aamir khan";
// const char* password = "Hi-tech33";
// const char* ENDPOINT = "http://18.199.188.82:8082/api/VehicleLoad/quantity";
const char* ssid = "Hi-Tech-Farm";
const char* password = "secure@1";
const char* ENDPOINT = "http://18.199.188.82:8082/api/dieselTank/quantity";

// —— DEVICE INFO (FKs) ——
const char* DEVICE_ID = "49";
const char* FARM_ID   = "11";

// —— OFFLINE QUEUE ——
static std::vector<String> pendingPosts;

// —— NTP (PKT) OFFSETS ——
const long  GMT_OFFSET_SEC      = 5 * 3600;  // UTC+5
const int   DAYLIGHT_OFFSET_SEC = 0;

// —— FRAME AVERAGING BUFFER ——
static float frameBuffer[10];    // circular buffer
static int   frameCount  = 0;    // how many valid entries so far (max 10)
static int   frameIndex  = 0;    // next write index
static float frameSum    = 0;    // sum of buffer contents
static float lastAvgReading = 0; // most recent averaged value

// ─── QUEUE-FLUSH & HTTP ───
// unchanged…
bool sendHttp(const String &body) {
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
  while (!pendingPosts.empty()) {
    String &q = pendingPosts.front();
    Serial.println("Flushing queued post:\n" + q);
    if (sendHttp(q)) {
      pendingPosts.erase(pendingPosts.begin());
    } else {
      break;
    }
  }
}

// ─── SETUP WIFI + NTP ───
// unchanged…
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

// ─── FRAME PARSING & AVERAGING ───
// Call this as often as possible (e.g. at top of loop)
// ─── FRAME PARSING & AVERAGING (reversal-decode) ───
void processSerialFrames() {
  static String frameBuf;      // accumulates one complete frame

  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '=') {            // every frame starts with ‘=’
      frameBuf = "=";          // reset & start new frame
    } else if (frameBuf.length() > 0) {
      frameBuf += c;

      // A full frame is 1 ‘=’ + 7 digits  → total length 8
      if (frameBuf.length() == 8) {
        String raw = frameBuf.substring(1);   // e.g. "0068000"
                                              //  ^^^^ seven digits
        // — Reverse the 7-digit string —————————————
        String rev;
        for (int i = raw.length() - 1; i >= 0; --i) rev += raw[i];
        // rev for "0068000"  →  "0008600"

        // — Convert to kg ————————————————
        int   intWeight = rev.toInt();        // 8600
        float val       = intWeight / 10.0;   // 860.0 kg

        Serial.println("\n[RAW FRAME] " + frameBuf);
        Serial.println("[PARSED] Weight: " + String(val, 1) + " kg");

        // — Add to 10-sample moving average ——
        if (frameCount < 10) {
          frameSum += val;
          frameBuffer[frameIndex] = val;
          frameCount++;
        } else {
          frameSum -= frameBuffer[frameIndex];
          frameBuffer[frameIndex] = val;
          frameSum += val;
        }
        frameIndex = (frameIndex + 1) % 10;
        lastAvgReading = frameSum / frameCount;

        Serial.printf("[AVG] Updated moving average: %.1f kg\n",
                      lastAvgReading);

        frameBuf = "";                         // ready for next frame
      }
    }
  }
}
// void processSerialFrames() {
//   static String frameBuf;
//   while (Serial2.available()) {
//     char c = Serial2.read();
//     if (c == '=') {
//       // start of a new frame
//       frameBuf = "=";
//     } else if (frameBuf.length() > 0) {
//       frameBuf += c;
//       // once we have "=" + 7 chars
//       if (frameBuf.length() == 8) {
//         // e.g. frameBuf = "=00.0050" or "=00.005-"
//         String raw = frameBuf.substring(1);  // "00.0050"
//         String rev;
//         for (int i = raw.length() - 1; i >= 0; --i) {
//           rev += raw.charAt(i);
//         }
//         float val = rev.toFloat();

//         // add into circular buffer
//         if (frameCount < 10) {
//           frameSum += val;
//           frameBuffer[frameIndex] = val;
//           frameCount++;
//         } else {
//           // overwrite oldest
//           frameSum -= frameBuffer[frameIndex];
//           frameBuffer[frameIndex] = val;
//           frameSum += val;
//         }
//         frameIndex = (frameIndex + 1) % 10;
//         lastAvgReading = frameSum / frameCount;

//         // clear and wait for next frame
//         frameBuf = "";
//       }
//     }
//   }
// }


// ─── POST A WEIGHT READING ───
// unchanged except using averaged reading
void postWeightReading(float weight) {
  struct tm ti;
  char ts[32] = "";

  if (getLocalTime(&ti)) {
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &ti);
  } else {
    Serial.println("Time not ready, queuing reading");
    return;
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
    }
  } else {
    pendingPosts.push_back(body);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  // start UART2 for RS-232
  Serial2.begin(SERIAL2_BAUD, SERIAL_8N1, RX2_PIN, -1);
  Serial2.setRxInvert(true);

  // connect Wi-Fi & sync time
  setupWifi();
}

void loop() {
  static unsigned long lastPostMillis = 0;

  // 1) Maintain Wi-Fi & flush queue
  if (WiFi.status() != WL_CONNECTED) {
    setupWifi();
  } else {
    flushPending();
  }

  // 2) Parse any incoming frames immediately
  processSerialFrames();

  // 3) Retrieve the 10-frame moving average
  float avgWeight = lastAvgReading;
  Serial.printf("Avg Weight: %.2f\n", avgWeight);

  // 4) Post once a minute
  unsigned long now = millis();
  if (lastPostMillis == 0 || now - lastPostMillis >= 60UL * 1000UL) {
    postWeightReading(avgWeight);
    lastPostMillis = now;
  }
}