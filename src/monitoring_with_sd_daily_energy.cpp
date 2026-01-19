/**
 * ============================================================
 *  ESP32 Electrical Monitoring System (Daily Energy Logging)
 *
 *  Features:
 *  - PZEM-004T v3.0 energy monitoring
 *  - Daily energy calculation
 *  - Offline backup using SD Card
 *  - Firebase Realtime Database synchronization
 *  - Automatic backlog resend when WiFi reconnects
 *
 *  Note:
 *  WiFi and Firebase credentials are intentionally omitted
 *  for security and publication purposes.
 *
 *  Author : Arung Tirto Nusantara
 *  Year   : 2026
 * ============================================================
 */


#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <PZEM004Tv30.h>
#include <time.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>                        

// --------- WIFI (PLACEHOLDER) ----------
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// --------- FIREBASE (PLACEHOLDER) ---------
#define FIREBASE_HOST "your-project-id.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "YOUR_DATABASE_SECRET"

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// --------- PZEM ---------
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
HardwareSerial mySerial(1);
PZEM004Tv30 pzem(mySerial, PZEM_RX_PIN, PZEM_TX_PIN);

// --------- LCD ---------
LiquidCrystal_I2C lcd(0x27, 20, 4);

// --------- SD CARD ---------
#define SD_CS_PIN 15 // sesuaikan dengan pin CS SD card

// --------- ROOM ---------
String roomName = "room1";

// --------- ENERGI HARIAN ---------
float initialEnergy = 0;
float energiHarian  = 0;
String currentDate  = "";

// --------- Fungsi untuk mendapatkan tanggal saat ini ---------
String getCurrentDate() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char buffer[11];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", timeinfo);
  return String(buffer);
}

/* ******************************************************************
   ===============  FUNGSI TAMBAHAN: simpan & muat initialEnergy  ===============
******************************************************************* */
// Simpan nilai awal energi (awal hari) ke SD agar tidak hilang jika reboot
void saveInitialEnergyToSD(String date, float energy) {                 // === NEW ===
  File f = SD.open("/initial_energy.txt", FILE_WRITE);
  if (f) {
    f.printf("%s,%.3f\n", date.c_str(), energy);
    f.close();
    Serial.println(">> initialEnergy saved: " + String(energy,3));
  } else {
    Serial.println("!! Failed save initialEnergy");
  }
}

// Baca kembali nilai initialEnergy untuk tanggal hari ini
float loadInitialEnergyFromSD(String date) {                            // === NEW ===
  File f = SD.open("/initial_energy.txt", FILE_READ);
  if (!f) return NAN;            // file belum ada
  while (f.available()) {
    String line = f.readStringUntil('\n');
    int c = line.indexOf(',');
    if (c < 0) continue;
    String savedDate = line.substring(0, c);
    if (savedDate == date) {
      float val = line.substring(c + 1).toFloat();
      f.close();
      Serial.println(">> initialEnergy restored: " + String(val,3));
      return val;
    }
  }
  f.close();
  return NAN;                    // tidak ketemu tanggalnya
}

/* ****************************************************************** */

// --------- Fungsi untuk cek dan reconnect WiFi ---------
void checkWiFiReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
      delay(500);
      Serial.print(".");
      retryCount++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected!");
      lcd.setCursor(0, 1);
      lcd.print("WiFi Connected    ");
      sendBacklogToFirebase(); // Kirim backlog jika ada
    } else {
      Serial.println("\nFailed to reconnect WiFi.");
      lcd.setCursor(0, 1);
      lcd.print("WiFi Reconnect Err");
    }
  }
}

// --------- Fungsi untuk backup energi harian ke SD card ---------
void backupEnergyToSD(String date, float energi) {
  String filename = "/energi_backup.txt";
  File file = SD.open(filename, FILE_APPEND);
  if (file) {
    String data = date + "," + String(energi, 3) + "\n";
    file.print(data);
    file.close();
    Serial.println("Backed up energy to SD: " + data);
  } else {
    Serial.println("Failed to open backup file");
  }
}

// --------- Fungsi kirim backlog dari SD card ke Firebase ---------
void sendBacklogToFirebase() {
  String filename = "/energi_backup.txt";
  if (!SD.exists(filename)) {
    Serial.println("No backlog file found");
    return;
  }
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    Serial.println("Failed to open backlog file");
    return;
  }

  Serial.println("Sending backlog to Firebase...");

  bool allSent = true;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int commaIndex = line.indexOf(',');
    if (commaIndex < 0) continue;

    String date = line.substring(0, commaIndex);
    String energiStr = line.substring(commaIndex + 1);
    float energi = energiStr.toFloat();

    String path = "/energiHarian/" + roomName + "/" + date;
    if (Firebase.setFloat(firebaseData, path, energi)) {
      Serial.println("Sent backlog: " + line);
    } else {
      Serial.println("Failed to send backlog for " + date + ": " + firebaseData.errorReason());
      allSent = false;
      break; // berhenti kalau gagal kirim untuk menghindari hilangnya data
    }
  }
  file.close();

  if (allSent) {
    SD.remove(filename);
    Serial.println("Backlog sent and file removed");
  } else {
    Serial.println("Backlog sending incomplete, file kept");
  }
}

// --------- SETUP ---------
void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Starting...");

  // SD Card init
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD card initialization failed!");
    lcd.setCursor(0, 1);
    lcd.print("SD Init Failed");
  } else {
    Serial.println("SD card initialized");
  }

  // WiFi connect with timeout
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi...");
  int wifiTimeout = 30;
  int wifiWait = 0;
  while (WiFi.status() != WL_CONNECTED && wifiWait < wifiTimeout) {
    delay(1000);
    Serial.print(".");
    wifiWait++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi Connection Failed!");
    lcd.setCursor(0, 1);
    lcd.print("WiFi Failed    ");
  } else {
    Serial.println("\nWiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print("WiFi Connected ");
  }

  // Firebase config (pastikan sesuai versi library)
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Time sync dengan timeout
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  int timeWait = 0;
  while (time(nullptr) < 100000 && timeWait < 30) {
    delay(500);
    Serial.println("Waiting for time sync...");
    timeWait++;
  }
  if (time(nullptr) < 100000) {
    Serial.println("Time sync failed!");
    // Bisa lanjut tanpa waktu sinkron, atau reset perangkat
  } else {
    Serial.println("Time synced");
  }

  currentDate = getCurrentDate();

  // Load or save initialEnergy
  float loaded = loadInitialEnergyFromSD(currentDate);
  if (!isnan(loaded)) {
    initialEnergy = loaded;
  } else {
    initialEnergy = pzem.energy();
    saveInitialEnergyToSD(currentDate, initialEnergy);
  }

  delay(2000);
  lcd.clear();
}


// --------- LOOP ---------
void loop() {
  checkWiFiReconnect();

  // Baca sensor
  float voltage   = pzem.voltage();
  float current   = pzem.current();
  float power     = pzem.power();
  float energy    = pzem.energy();
  float pf        = pzem.pf();
  float frequency = pzem.frequency();

  // Cek tanggal hari ini
  String today = getCurrentDate();
  if (today != currentDate) {
    // Kirim energi hari kemarin ke Firebase
    String path = "/energiHarian/" + roomName + "/" + currentDate;
    if (WiFi.status() == WL_CONNECTED) {
      if (Firebase.setFloat(firebaseData, path, energiHarian)) {
        Serial.println("Energi harian saved to Firebase: " + currentDate);
      } else {
        Serial.println("Firebase Error: " + firebaseData.errorReason());
        backupEnergyToSD(currentDate, energiHarian);
      }
    } else {
      backupEnergyToSD(currentDate, energiHarian);
      Serial.println("WiFi disconnected, backup energi harian ke SD");
    }

    // reset untuk hari baru
    currentDate   = today;
    initialEnergy = energy;
    energiHarian  = 0;

    saveInitialEnergyToSD(currentDate, initialEnergy);             // === NEW ===
  }

  energiHarian = energy - initialEnergy;

  // LCD Halaman 1
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("V:"); lcd.print(voltage); lcd.print("V ");
  lcd.setCursor(0, 1); lcd.print("I:"); lcd.print(current); lcd.print("A ");
  lcd.setCursor(0, 2); lcd.print("P:"); lcd.print(power); lcd.print("W ");
  lcd.setCursor(0, 3); lcd.print("E:"); lcd.print(energy); lcd.print("kWh");
  delay(2000);

  // LCD Halaman 2
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("PF: "); lcd.print(pf);
  lcd.setCursor(0, 1); lcd.print("Fq: "); lcd.print(frequency); lcd.print("Hz");
  lcd.setCursor(0, 2); lcd.print(currentDate);
  lcd.setCursor(0, 3); lcd.print("Eday:"); lcd.print(energiHarian); lcd.print("kWh");

  // Kirim data ke Firebase ke path: /sensor/{roomName}/...
  String basePath = "/sensor/" + roomName;
  Firebase.setFloat(firebaseData, basePath + "/voltage", voltage);
  Firebase.setFloat(firebaseData, basePath + "/current", current);
  Firebase.setFloat(firebaseData, basePath + "/power", power);
  Firebase.setFloat(firebaseData, basePath + "/energy", energy);
  Firebase.setFloat(firebaseData, basePath + "/power_factor", pf);
  Firebase.setFloat(firebaseData, basePath + "/frequency", frequency);

  delay(1000);
}
