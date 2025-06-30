#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <BH1750.h>
#include "DHT.h"
#include <Preferences.h> // For storing Ro and offset in non-volatile memory (ESP32)
#include <WiFi.h>
#include <HTTPClient.h>

// WiFi Credentials
//BABEKWAL
// const char* ssid = "Jazz 4G CPE_FECD";
// const char* password = "34278261";
//MANDRA
// const char* ssid = "Aamir khan";
// const char* password = "Hi-tech33";
//HI TECH TESTING
// const char* ssid = "Chief Executive_IoT";
// const char* password = "secure@1";
const char* ssid = "Hi-Tech-Farm";
const char* password = "secure@1";


const char* serverIP = "18.199.188.82";  // original ip
// const char* serverIP = "10.17.71.91";  // Replace with your Spring Boot server IP
//const char* serverIP = "155.135.1.90";
// const int serverPort = 8080;
const int serverPort = 9090;

const char* DeviceID = "43";
const char* DeviceLabel = "H1 P";

// ─── NEW TIMING CONSTANTS & STATE ───────────────────────────────
const unsigned long FIRST_SEND_DELAY_MS = 120UL * 1000;   // 1.5 min
const unsigned long SEND_INTERVAL_MS    = 45UL * 1000;   // 45 s
// const unsigned long FIRST_SEND_DELAY_MS = 1UL * 1000;   // 1.5 min
// const unsigned long SEND_INTERVAL_MS    = 1UL * 1000;   // 45 s
unsigned long bootMillis      = 0;  // captured in setup()
unsigned long lastSendMillis  = 0;  // updated after each successful POST
// ─── COUNTDOWN HELPERS ─────────────────────────────
unsigned long lastCountdownSec = 999;   // force first print
char          lcdBuf[17];               // 16-char temp buffer
// ────────────────────────────────────────────────── 

// ────────────────────────────────────────────────────────────────

// WiFi Client
WiFiClient client;

// === LCD Configuration ===
LiquidCrystal_I2C lcd(0x27, 16, 4);

// === BH1750 Light Sensor ===
BH1750 lightMeter;

// === DHT22 Sensor ===
#define DHTPIN 5
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// === MQ-137 Sensor Configuration ===
#define smoke_sensor 35  // ADC pin for MQ-137

// IMPORTANT: Set RL according to the actual resistor in ohms!
// e.g. 1 kΩ => 1000.0; 10 kΩ => 10000.0, etc.
#define RL 1000.0

// If you have the typical MQ-137 from datasheet, the clean-air ratio is about 1.7
const float CLEAN_AIR_RATIO = 1.5;

// Constants for typical MQ-137 log formula (from datasheet’s log-log plot)
// log10(PPM) = (log10(Rs/Ro) - b) / m
// => PPM = 10^[(log10(Ratio) - b)/m]
// The values below (b = 0.42, m = -0.263) are approximate and may need tweaking.
// const float SLOPE_M     = -0.477;   // steeper
// const float INTERCEPT_B =  1.63;    // moves curve up

const float SLOPE_M = -0.263;
const float INTERCEPT_B = 0.42;


// For storing calibration data in ESP32 flash
Preferences prefs;

// We store Ro and an environment offset globally
// float Ro = 6000.0;
float Ro = 7000.0;          // Will be calibrated or loaded
float ammoniaOffset = 0.0;  // Used to shift final PPM reading to near zero if needed

bool doCalibration = true;  // Decide if we should calibrate on startup

// === Function Prototypes ===
void setupLCD();
void setupDHT();
void setupBH1750();
void setupSmokeSensor();
void calibrateMQ137();
void clearMQ137Calibration();
void calibrateEnvironmentOffset();

void setupWifi();
void sendDataToAPI(float PPM, int lux, float humidity, float temperature);

float readTemperature();
float readHumidity();
float readLightLevel();
float readAmmoniaPPM();

void displayData(float temp, float hum, float lux, float ppm);

void setup() {
  Serial.begin(115200);
  Wire.begin();
  pinMode(smoke_sensor, INPUT);

  // record boot-time for first-send delay
  bootMillis = millis();

  // Connect to WiFi on startup
  setupWifi();

  bool shouldClear = true; // or read from a button or command
  if (shouldClear) {
    clearMQ137Calibration();
    lcd.print("  RESET RO ");
    delay(1000);
  }
  
  setupLCD();
  setupDHT();
  setupBH1750();
  setupSmokeSensor();

  // Give MQ sensor some time to warm up
  delay(5000);

  // === Retrieve stored calibration from Preferences (ESP32) ===
  // ... (your calibration code commented out here) ...

  Serial.println("Setup done. Entering main loop...");
}

void setupWifi() {
  // Enable auto-reconnect and persistent settings for WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  // Start connection
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {  // Try 20 times
      delay(1000);
      Serial.println("Connecting to WiFi...");
      attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
  } else {
      Serial.println("WiFi connection failed!");
  }
}

// Clear MQ-137 calibration data from flash
void clearMQ137Calibration() {
  prefs.begin("mq137", false);
  prefs.clear();
  prefs.end();
  ammoniaOffset = 0.0;
  doCalibration = true;
  Serial.println("Cleared MQ-137 calibration data from flash. Next run will recalibrate.");
}

void loop() {
    // ─── 90-second boot countdown ─────────────────────
  // unsigned long now = millis();
  // if (now - bootMillis < FIRST_SEND_DELAY_MS) {
  //   unsigned long remainingSec =
  //       (FIRST_SEND_DELAY_MS - (now - bootMillis) + 999) / 1000;  // ceil

  //   if (remainingSec != lastCountdownSec) {
  //     // —— Serial monitor ——
  //     Serial.print("[ ");
  //     Serial.print(DeviceLabel);     // adds the label, e.g. f_test
  //     Serial.print("] first post in ");

  //     Serial.print(remainingSec);
  //     Serial.println(" s");

  //     // —— LCD ——
  //     lcd.clear();
  //     lcd.setCursor(0, 0);
  //     lcd.print(DeviceLabel);
  //     lcd.print(" wait...");
  //     lcd.setCursor(0, 1);
  //     snprintf(lcdBuf, sizeof(lcdBuf), "Start in %3lus", remainingSec);
  //     lcd.print(lcdBuf);           // e.g. “Start in  90s”

  //     lastCountdownSec = remainingSec;
  //   }
  //   delay(1000);   // 1-second tick
  //   return;        // skip rest of loop until delay finished
  // }
  // ──────────────────────────────────────────────────

// Check if WiFi is still connected; if not, try reconnecting.

  // Check if WiFi is still connected; if not, try reconnecting.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    setupWifi();
  }
  // if (lastCountdownSec != 0) {   // just exited countdown
  //   lcd.clear();
  //   lastCountdownSec = 0;        // prevent this from running again
  // }

  
  // === Read Sensors ===
  float temperature = readTemperature();
  float humidity    = readHumidity();
  float lux         = readLightLevel();
  float ammoniaPPM  = readAmmoniaPPM();

  // === Display the Data ===
  displayData(temperature, humidity, lux, ammoniaPPM);

  // Unconditional call preserved – logic moved inside sendDataToAPI()
  sendDataToAPI(ammoniaPPM, lux, humidity, temperature);

  delay(3000);
}

// ───────────────────────────────────────────────────────────────
// sendDataToAPI now enforces the 1.5 min startup delay
// and 30 s interval between successful sends.
// ───────────────────────────────────────────────────────────────
void sendDataToAPI(float PPM, int lux, float humidity, float temperature) {

  unsigned long now = millis();

  // Wait 1.5 minutes after boot before the first send
  if ((now - bootMillis) < FIRST_SEND_DELAY_MS) {
    return;
  }

  // Enforce 30 second interval between sends
  if ((now - lastSendMillis) < SEND_INTERVAL_MS) {
    return;
  }

  Serial.println("Sending to API...");

  if (client.connect(serverIP, serverPort)) {

      String jsonData = String("{") 
          + "\"deviceLabel\": \"" + DeviceLabel + "\","
          + "\"deviceId\": \"" + DeviceID + "\","
          + "\"ammonia_ppm\": " + (isnan(PPM) ? "null" : String(PPM)) + ","
          + "\"lux\": " + (isnan(lux) ? "null" : String(lux)) + ","
          + "\"humidity\": " + (isnan(humidity) ? "null" : String(humidity)) + ","
          + "\"temperature\": " + (isnan(temperature) ? "null" : String(temperature))
          + "}";

      // extra log showing the exact JSON payload
      Serial.print("Payload: ");
      Serial.println(jsonData);

      client.println(jsonData);
      Serial.println("Data sent to the server");
      client.stop();

      lastSendMillis = now;   // mark successful send time
  } else {
      Serial.println("Connection to server failed");
  }
}

// =========================  (rest of your original code)  =========================
void setupLCD() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("  Hi-Tech ErpBG ");
  delay(1500);
  lcd.clear();
}

void setupDHT() {
  dht.begin();
}

void setupBH1750() {
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 initialized");
  } else {
    Serial.println("BH1750 initialization failed!");
  }
}

void setupSmokeSensor() {
  // Placeholder: configure MQ-137 sensor if needed.
}

void calibrateMQ137() {
  Serial.println("Calibrating MQ-137 in clean air (or baseline air)...");
  float totalRs = 0.0;
  const int samples = 50;

  for (int i = 0; i < samples; i++) {
    int sensorValue = analogRead(smoke_sensor);
    float VRL = sensorValue * (3.3 / 4095.0);  // Convert ADC reading to voltage
    if (VRL < 0.1) VRL = 0.1;  // Avoid extreme low
    float Rs = ((3.3 / VRL) - 1.0) * RL;
    totalRs += Rs;
    delay(100);
  }

  float avgRs = totalRs / samples;
  Ro = avgRs / CLEAN_AIR_RATIO;

  Serial.print("Calibrated Ro: ");
  Serial.println(Ro);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrated Ro:");
  lcd.setCursor(0, 1);
  lcd.print(Ro);
  delay(2000);
}

void calibrateEnvironmentOffset() {
  Serial.println("Measuring baseline NH3 to create offset...");
  const int offsetSamples = 20;
  float totalPPM = 0.0;
  for (int i = 0; i < offsetSamples; i++) {
    float ppm = readAmmoniaPPM();
    totalPPM += ppm;
    delay(200);
  }
  float baselinePPM = totalPPM / offsetSamples;
  ammoniaOffset = baselinePPM;
  Serial.print("Baseline PPM: ");
  Serial.println(baselinePPM);
  Serial.print("Applying offset: ");
  Serial.println(ammoniaOffset);
}

float readTemperature() {
  float temperature = dht.readTemperature();
  if (isnan(temperature)) {
    Serial.println("Failed to read temperature!");
    return NAN;
  }
  return temperature;
}

float readHumidity() {
  float humidity = dht.readHumidity();
  if (isnan(humidity)) {
    Serial.println("Failed to read humidity!");
    return NAN;
  }
  return humidity;
}

float readLightLevel() {
  float lux = lightMeter.readLightLevel();
  if (lux < 0) {
    Serial.println("Failed to read light level!");
    return NAN;
  }
  return lux;
}

float readAmmoniaPPM() {
  const int numberOfSamples = 20;
  float totalPPM = 0;
  int counter = 0;

  for (int i = 0; i < numberOfSamples; i++) {
    int sensorValue = analogRead(smoke_sensor);
    float VRL = sensorValue * (3.3 / 4095.0);
    if (VRL < 0.1) VRL = 0.1;
    float Rs = ((3.3 / VRL) - 1.0) * RL;
    float ratio = Rs / Ro;
    if (ratio <= 0) ratio = 0.01;
    float logRatio = log10(ratio);
    float ppm = pow(10, ((logRatio - INTERCEPT_B) / SLOPE_M));
    totalPPM += ppm;
    counter++;
    delay(30);
  }
  float averagePPM = (counter == 0) ? 0.0 : (totalPPM / counter);
  averagePPM -= ammoniaOffset;
  if (averagePPM < 0) averagePPM = 0;
  return averagePPM;
}

void displayData(float temp, float hum, float lux, float ppm) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Tmp: ");
  if (isnan(temp)) lcd.print("N/A");
  else {
    lcd.print(temp, 1);
    lcd.print(" C");
  }

  lcd.setCursor(0, 1);
  lcd.print("Hum: ");
  if (isnan(hum)) lcd.print("N/A");
  else {
    lcd.print(hum, 1);
    lcd.print(" %");
  }

  lcd.setCursor(-4, 2);
  lcd.print("NH3: ");
  if (isnan(ppm)) lcd.print("N/A");
  else {
    lcd.print(ppm, 2);
    lcd.print(" ppm");
  }

  lcd.setCursor(-4, 3);
  lcd.print("Lux: ");
  if (isnan(lux)) lcd.print("N/A");
  else lcd.print(lux, 1);

  Serial.println("----------- Current Reading ------------");
  Serial.print("Temperature (C): ");
  Serial.println(temp);
  Serial.print("Humidity (%): ");
  Serial.println(hum);
  Serial.print("NH3 PPM: ");
  Serial.println(ppm);
  Serial.print("Light (lux): ");
  Serial.println(lux);
  Serial.println("----------------------------------------\n");
}
