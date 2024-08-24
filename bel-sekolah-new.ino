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
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60000); // GMT+7, update setiap 60 detik

// Pengaturan WiFi
const char* apSSID = "ESP8266-AP";
const char* apPassword = "123456789";
const char* ssid = "Ethan Deco";
const char* password = "Yroedi75";

// Pengaturan pin relay dan LED
int relayPin = 5;  // Pin untuk relay
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

  // Mulai NTP client dan update RTC
  timeClient.begin();
  updateRTC();

  // Inisialisasi web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/set_time", HTTP_POST, handleSetTime);
  server.on("/view_schedules", HTTP_GET, handleViewSchedules);
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

  // Periksa apakah ada jadwal yang perlu dieksekusi
  checkSchedules(now);
  
  // Tangani request HTTP
  server.handleClient();
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
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Scheduler</title></head>"
    "<body><h1>Scheduler</h1>"
    "<form action='/set_time' method='post'>"
    "<h2>Simpan Jadwal</h2>"
    "<label>Hari: <input type='text' name='day'></label><br>"
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
    "</body></html>";
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
        server.send(200, "text/plain", "Jadwal disimpan");
      } else {
        server.send(400, "text/plain", "Tidak ada slot tersedia");
      }
    } else {
      server.send(400, "text/plain", "Hari tidak valid");
    }
  } else {
    server.send(400, "text/plain", "Parameter tidak lengkap");
  }
}

void handleViewSchedules() {
  String json = "{";
  for (int i = 0; i < 7; i++) {
    json += "\"" + String(daysOfWeek[i]) + "\":[";
    for (int j = 0; j < 15; j++) {
      if (schedules[i][j].enabled) {
        json += "{";
        json += "\"hour\":" + String(schedules[i][j].hour) + ",";
        json += "\"minute\":" + String(schedules[i][j].minute);
        json += "},";
      }
    }
    if (json.endsWith(",")) {
      json.remove(json.length() - 1);
    }
    json += "],";
  }
  if (json.endsWith(",")) {
    json.remove(json.length() - 1);
  }
  json += "}";
  blinkLED(1);
  server.send(200, "application/json", json);
}

void handleClearSchedules() {
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 15; j++) {
      schedules[i][j].enabled = false;
      schedules[i][j].hour = 0;
      schedules[i][j].minute = 0;
      relayActive[i][j] = false;  // Reset status relay
    }
  }
  saveSchedules();  // Simpan jadwal yang telah dihapus ke EEPROM
  blinkLED(3);      // Blink LED sebagai indikasi bahwa jadwal telah dihapus
  server.send(200, "text/plain", "Semua jadwal dihapus");
}

void checkSchedules(DateTime now) {
  int dayIndex = now.dayOfTheWeek();
  for (int i = 0; i < 15; i++) {
    if (schedules[dayIndex][i].enabled && schedules[dayIndex][i].hour == now.hour() && schedules[dayIndex][i].minute == now.minute()) {
      if (!relayActive[dayIndex][i]) {
        relayActive[dayIndex][i] = true;  // Tandai relay telah aktif untuk waktu ini
        digitalWrite(relayPin, HIGH);  // Aktifkan relay
        blinkLED(1);  // Blink LED saat relay aktif
        delay(relayDuration);  // Aktifkan relay selama 3 detik
        digitalWrite(relayPin, LOW);  // Matikan relay
      }
      break;  // Keluar dari loop setelah relay diaktifkan
    }
  }
}

void saveSchedules() {
  int addr = 0;
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 15; j++) {
      EEPROM.write(addr++, schedules[i][j].enabled);
      EEPROM.write(addr++, schedules[i][j].hour);
      EEPROM.write(addr++, schedules[i][j].minute);
      EEPROM.write(addr++, relayActive[i][j]);  // Simpan status relay
    }
  }
  EEPROM.commit(); // Commit untuk memastikan data disimpan ke EEPROM
}

void loadSchedules() {
  int addr = 0;
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 15; j++) {
      schedules[i][j].enabled = EEPROM.read(addr++);
      schedules[i][j].hour = EEPROM.read(addr++);
      schedules[i][j].minute = EEPROM.read(addr++);
      relayActive[i][j] = EEPROM.read(addr++);  // Muat status relay
    }
  }
}

int getDayIndex(String day) {
  for (int i = 0; i < 7; i++) {
    if (day.equalsIgnoreCase(daysOfWeek[i])) {
      return i;
    }
  }
  return -1; // Mengembalikan -1 jika hari tidak valid
}

void blinkLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
  }
}
