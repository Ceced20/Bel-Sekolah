#include <Wire.h>
#include <RTClib.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Inisialisasi RTC, LCD, dan NTP Client
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 86400000); // GMT+7, update sekali sehari

// Pengaturan WiFi
const char* apSSID = "ESP8266-AP";
const char* apPassword = "123456789";
const char* ssid = "Ethan Deco";
const char* password = "Yroedi75";

// Pengaturan pin relay dan LED
int relayPin = D5;  // Pin untuk relay
int ledPin = LED_BUILTIN;  // LED internal ESP8266

// Struktur untuk menyimpan jadwal
struct Schedule {
  bool enabled;
  int hour;
  int minute;
};

Schedule schedules[7][15];  // 7 hari, 15 jadwal per hari
const char* daysOfWeek[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Variabel untuk manajemen waktu relay
bool relayActive[7][15] = {false}; // Status apakah relay sudah aktif pada waktu tertentu
unsigned long relayDuration = 3000; // 3 detik
unsigned long lastRTCUpdate = 0; // Waktu update RTC terakhir
unsigned long relayStartMillis[7][15] = {0}; // Menyimpan waktu mulai aktif relay

ESP8266WebServer server(80);

void setup() {
  Serial.begin(9600);
  EEPROM.begin(512);

  // Inisialisasi RTC
  if (!rtc.begin()) {
    Serial.println("RTC tidak ditemukan");
    while (1);
  }

  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Loading...");

  // Inisialisasi pin relay dan LED
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Koneksi ke WiFi
  if (!connectToWiFi()) {
    Serial.println("Koneksi WiFi gagal. Memulai Access Point...");
    WiFi.softAP(apSSID, apPassword);
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Terhubung ke WiFi");
  }

  // Mulai NTP client
  timeClient.begin();
  updateRTC();  // Update RTC pada setup

  // Inisialisasi web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/set_time", HTTP_POST, handleSetTime);
  server.on("/view_schedules", HTTP_GET, handleViewSchedules);
  server.on("/update_schedule", HTTP_POST, handleUpdateSchedule);
  server.on("/clear_schedules", HTTP_POST, handleClearSchedules);
  server.begin();
  Serial.println("HTTP server dimulai");

  // Muat jadwal dari EEPROM
  loadSchedules();
}

void loop() {
  timeClient.update();
  DateTime now = rtc.now();

  // Tampilkan waktu dan tanggal pada LCD
  lcd.setCursor(0, 0);
  lcd.printf("%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  lcd.setCursor(0, 1);
  lcd.printf("%s %02d/%02d/%04d", daysOfWeek[now.dayOfTheWeek()], now.day(), now.month(), now.year());

  // Reset status relayActive setiap hari pada pukul 00:00
  static int lastDay = -1;
  if (now.day() != lastDay) {
    resetDailySchedules();
    lastDay = now.day();
  }

  // Periksa apakah ada jadwal yang perlu dieksekusi
  checkSchedules(now);
  
  // Tangani request HTTP
  server.handleClient();

  // Update RTC sekali sehari
  unsigned long currentMillis = millis();
  if (currentMillis - lastRTCUpdate >= 86400000) { // 24 jam
    updateRTC();
    lastRTCUpdate = currentMillis;
  }
}

void updateRTC() {
  timeClient.update();
  rtc.adjust(DateTime(timeClient.getEpochTime()));
}

bool connectToWiFi() {
  WiFi.begin(ssid, password);
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 10) {
    delay(500);
    Serial.print(".");
    attempt++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nTerhubung ke WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    return false;
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Scheduler</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; background-color: #f4f4f9; color: #333; margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; }"
    "header { background-color: #4CAF50; color: #fff; width: 100%; padding: 20px; text-align: center; }"
    "main { width: 90%; max-width: 800px; margin: 20px 0; padding: 20px; background-color: #fff; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
    "h1 { margin-top: 0; }"
    "a { color: #4CAF50; text-decoration: none; font-weight: bold; }"
    "a:hover { text-decoration: underline; }"
    "</style></head><body>"
    "<header><h1>Scheduler</h1></header>"
    "<main>"
    "<form action='/set_time' method='post'>"
    "<h2>Simpan Jadwal</h2>"
    "<label>Hari: <select name='day'>";
  
  for (int i = 0; i < 7; i++) {
    html += "<option value='" + String(daysOfWeek[i]) + "'>" + String(daysOfWeek[i]) + "</option>";
  }
  html += "</select></label><br>"
          "<label>Waktu: <input type='time' name='time'></label><br>"
          "<label>Enabled: <input type='checkbox' name='enabled'></label><br>"
          "<input type='submit' value='Simpan Jadwal'>"
          "</form>"
          "<form action='/clear_schedules' method='post'>"
          "<h2>Hapus Semua Jadwal</h2>"
          "<input type='submit' value='Hapus Jadwal'>"
          "</form>"
          "<h2>Lihat Jadwal</h2>"
          "<a href='/view_schedules'>Lihat Jadwal yang Disimpan</a>"
          "</main></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSetTime() {
  if (server.hasArg("day") && server.hasArg("time")) {
    String day = server.arg("day");
    String time = server.arg("time");
    bool enabled = server.hasArg("enabled");

    int dayIndex = getDayIndex(day);
    if (dayIndex >= 0) {
      int hour = time.substring(0, 2).toInt();
      int minute = time.substring(3, 5).toInt();
      bool saved = false;
      
      for (int i = 0; i < 15; i++) {
        if (!schedules[dayIndex][i].enabled) {
          schedules[dayIndex][i].enabled = enabled;
          schedules[dayIndex][i].hour = hour;
          schedules[dayIndex][i].minute = minute;
          saved = true;
          break;
        }
      }
      
      if (saved) {
        saveSchedules();
        blinkLED(2);

        // Tampilkan pesan sukses dalam HTML
        String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Jadwal Disimpan</title>"
                      "<style>"
                      "body { font-family: Arial, sans-serif; text-align: center; padding: 20px; background-color: #f4f4f9; color: #333; }"
                      "a { color: #4CAF50; text-decoration: none; font-weight: bold; }"
                      "a:hover { text-decoration: underline; }"
                      "</style></head><body>"
                      "<h1>Jadwal Berhasil Disimpan</h1>"
                      "<p><a href='/'>Kembali ke Halaman Utama</a></p>"
                      "</body></html>";
        server.send(200, "text/html", html);
      } else {
        server.send(500, "text/html", "Tidak dapat menyimpan jadwal baru.");
      }
    } else {
      server.send(400, "text/html", "Hari tidak valid.");
    }
  } else {
    server.send(400, "text/html", "Data tidak lengkap.");
  }
}

void handleViewSchedules() {
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Jadwal</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; background-color: #f4f4f9; color: #333; margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; }"
    "header { background-color: #4CAF50; color: #fff; width: 100%; padding: 20px; text-align: center; }"
    "main { width: 90%; max-width: 800px; margin: 20px 0; padding: 20px; background-color: #fff; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
    "h1 { margin-top: 0; }"
    "</style></head><body>"
    "<header><h1>Jadwal</h1></header>"
    "<main>";
  
  for (int i = 0; i < 7; i++) {
    html += "<h2>" + String(daysOfWeek[i]) + "</h2><ul>";
    for (int j = 0; j < 15; j++) {
      if (schedules[i][j].enabled) {
        html += "<li>" + String(schedules[i][j].hour) + ":" + String(schedules[i][j].minute) + "</li>";
      }
    }
    html += "</ul>";
  }

void handleUpdateSchedule() {
  // Implementasikan pembaruan jadwal di sini jika diperlukan
}

void handleClearSchedules() {
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 15; j++) {
      schedules[i][j].enabled = false;
    }
  }
  saveSchedules();
  blinkLED(3);

  // Tampilkan pesan sukses dalam HTML
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Jadwal Dihapus</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; text-align: center; padding: 20px; background-color: #f4f4f9; color: #333; }"
                "a { color: #4CAF50; text-decoration: none; font-weight: bold; }"
                "a:hover { text-decoration: underline; }"
                "</style></head><body>"
                "<h1>Semua Jadwal Berhasil Dihapus</h1>"
                "<p><a href='/'>Kembali ke Halaman Utama</a></p>"
                "</body></html>";
  server.send(200, "text/html", html);
}

void saveSchedules() {
  int address = 0;
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 15; j++) {
      EEPROM.write(address++, schedules[i][j].enabled ? 1 : 0);
      EEPROM.write(address++, schedules[i][j].hour);
      EEPROM.write(address++, schedules[i][j].minute);
    }
  }
  EEPROM.commit();
}

void loadSchedules() {
  int address = 0;
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 15; j++) {
      schedules[i][j].enabled = EEPROM.read(address++) == 1;
      schedules[i][j].hour = EEPROM.read(address++);
      schedules[i][j].minute = EEPROM.read(address++);
    }
  }
}

void resetDailySchedules() {
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 15; j++) {
      relayActive[i][j] = false;
    }
  }
}

void checkSchedules(DateTime now) {
  int dayIndex = now.dayOfTheWeek();
  for (int i = 0; i < 15; i++) {
    if (schedules[dayIndex][i].enabled &&
        schedules[dayIndex][i].hour == now.hour() &&
        schedules[dayIndex][i].minute == now.minute()) {
      digitalWrite(relayPin, HIGH);
      relayStartMillis[dayIndex][i] = millis();
      relayActive[dayIndex][i] = true;
    }

    // Matikan relay setelah 3 detik
    if (relayActive[dayIndex][i] && millis() - relayStartMillis[dayIndex][i] >= relayDuration) {
      digitalWrite(relayPin, LOW);
      relayActive[dayIndex][i] = false;
    }
  }
}

int getDayIndex(String day) {
  for (int i = 0; i < 7; i++) {
    if (day == daysOfWeek[i]) {
      return i;
    }
  }
  return -1;
}

void blinkLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
  }
}
