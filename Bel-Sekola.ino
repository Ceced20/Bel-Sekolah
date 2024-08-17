#include <Wire.h>
#include <RTClib.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <LiquidCrystal_I2C.h>

// Inisialisasi RTC, NTP, dan LCD
RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "id.pool.ntp.org", 25200, 60000);
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Sesuaikan alamat I2C dan ukuran LCD jika diperlukan

const char* daysOfWeek[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void setup() {
  Serial.begin(9600);  // Memulai komunikasi serial dengan baud rate 9600

  // Memulai modul RTC DS3231
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);  // Hentikan eksekusi jika RTC tidak ditemukan
  }

  // Memulai komunikasi dengan LCD
  lcd.init();         // Inisialisasi LCD dengan lcd.init()
  lcd.backlight();   // Menyalakan lampu belakang LCD
  lcd.setCursor(0, 0); // Set cursor ke kolom 0 baris 0
  lcd.print("Loading.");  // Menampilkan teks awal

  // Menghubungkan ke WiFi
  const char* ssid = "Ethan Deco";        // Ganti dengan SSID WiFi Anda
  const char* password = "Yroedi75";      // Ganti dengan password WiFi Anda
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");

  // Memulai NTP client
  timeClient.begin();
  timeClient.update();  // Mengambil waktu epoch dari server NTP
  unsigned long unix_epoch = timeClient.getEpochTime();  // Mengambil waktu epoch
  rtc.adjust(DateTime(unix_epoch));  // Mengatur waktu RTC menggunakan waktu epoch dari NTP
}

void loop() {
  timeClient.update();  // Memperbarui waktu dari server NTP
  unsigned long unix_epoch = timeClient.getEpochTime();  // Mengambil waktu epoch saat ini

  DateTime now = rtc.now();  // Mendapatkan waktu saat ini dari RTC

  // Tampilkan waktu di LCD
  lcd.setCursor(0, 0);  // Tempatkan kursor pada baris pertama
  lcd.printf("%02d:%02d:%02d", now.hour(), now.minute(), now.second());  // Tampilkan waktu

  lcd.setCursor(0, 1);  // Tempatkan kursor pada baris kedua
  lcd.printf("%s %02d/%02d/%04d", daysOfWeek[now.dayOfTheWeek()], now.day(), now.month(), now.year());  // Tampilkan nama hari, tanggal, dan tahun

  delay(1000);  // Delay 1 detik sebelum iterasi berikutnya
}
