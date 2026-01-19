/**
 * ============================================================
 *  ESP32 Monitoring and Control System
 *  PZEM-004T v3.0 + Firebase Realtime Database
 *
 *  Author : Arung Tirto Nusantara
 *  Year   : 2026
 *
 *  Note:
 *  WiFi and Firebase credentials are intentionally omitted
 *  for security and publication purposes.
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>

// ======================= WiFi (PLACEHOLDER) =======================
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ======================= Firebase (PLACEHOLDER) ===================
#define FIREBASE_HOST "https://your-project-id.firebaseio.com"
#define FIREBASE_AUTH "YOUR_DATABASE_SECRET"

// ======================= Firebase Path ============================
#define KONTROL_BASE_PATH "/kontrol/room2"
#define PZEM_BASE_PATH    "/sensor/room2"

// ======================= Relay GPIO ===============================
#define RELAY_CH1_PIN 32
#define RELAY_CH2_PIN 33
#define RELAY_CH3_PIN 18
#define RELAY_CH4_PIN 19

// ======================= PZEM =====================================
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
HardwareSerial pzemSerial(1);
PZEM004Tv30 pzem(pzemSerial, PZEM_RX_PIN, PZEM_TX_PIN);

// ======================= LCD ======================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================= Firebase =================================
FirebaseData fbData;
FirebaseData fbStream;
FirebaseAuth auth;
FirebaseConfig config;

// ======================= Variables ================================
bool relayState[4] = {false, false, false, false};

float voltage = NAN, current = NAN, power = NAN;
float energy = NAN, frequency = NAN, powerFactor = NAN;

unsigned long lastSendMillis = 0;
const unsigned long SEND_INTERVAL = 5000;

// ======================= Utility =================================
float truncateFloat(float val, int dp) {
  if (isnan(val)) return NAN;
  float m = pow(10, dp);
  return ((long)(val * m)) / m;
}

// ======================= Setup ===================================
void setup() {
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();
  lcd.print("System Init");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  pinMode(RELAY_CH1_PIN, OUTPUT);
  pinMode(RELAY_CH2_PIN, OUTPUT);
  pinMode(RELAY_CH3_PIN, OUTPUT);
  pinMode(RELAY_CH4_PIN, OUTPUT);

  pzemSerial.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);

  Firebase.beginStream(fbStream, KONTROL_BASE_PATH);

  lcd.clear();
  lcd.print("System Ready");
}

// ======================= Loop ====================================
void loop() {
  unsigned long now = millis();

  if (now - lastSendMillis >= SEND_INTERVAL) {
    lastSendMillis = now;

    voltage = pzem.voltage();
    current = pzem.current();
    powerFactor = pzem.pf();

    if (!isnan(voltage) && !isnan(current)) {
      power = voltage * current * powerFactor;
      energy = pzem.energy();
      frequency = pzem.frequency();

      FirebaseJson json;
      json.set("voltage", truncateFloat(voltage, 1));
      json.set("current", truncateFloat(current, 3));
      json.set("power", truncateFloat(power, 2));
      json.set("energy", truncateFloat(energy, 3));
      json.set("frequency", truncateFloat(frequency, 1));
      json.set("power_factor", truncateFloat(powerFactor, 2));

      Firebase.set(fbData, PZEM_BASE_PATH, json);
    }
  }
}
