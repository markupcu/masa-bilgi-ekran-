#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <math.h>

TFT_eSPI tft = TFT_eSPI();

// *** BURAYA KENDI BILGILERINI GIR ***
const char* ssid = "WIFI_ADINIZ";
const char* password = "WIFI_SIFRENIZ";
const char* moonrakerIP = "192.168.1.106";
const uint16_t moonrakerPort = 7125;

// Ekran ve zamanlama ayarlari
const uint16_t screenWidth = 320;
const uint16_t screenHeight = 240;
const unsigned long dataRefreshMs = 3000;
const unsigned long wifiRetryMs = 5000;
const unsigned long requestTimeoutMs = 2500;

struct PrinterData {
  float hotendTemp = 0;
  float hotendTarget = 0;
  float bedTemp = 0;
  float bedTarget = 0;
  float progress = 0;
  String filename = "Baski yok";
  String eta = "--";
  String state = "standby";
  String message = "Baslatiliyor";
};

PrinterData printer;
unsigned long lastDataRefresh = 0;
unsigned long lastWifiRetry = 0;
bool lastFetchOk = false;

String moonrakerUrl() {
  return "http://" + String(moonrakerIP) + ":" + String(moonrakerPort) +
         "/printer/objects/query?extruder&heater_bed&print_stats&display_status";
}

String formatDuration(float seconds) {
  if (seconds <= 0 || isnan(seconds) || isinf(seconds)) {
    return "--";
  }

  uint32_t totalSeconds = (uint32_t)seconds;
  uint16_t hours = totalSeconds / 3600;
  uint8_t minutes = (totalSeconds % 3600) / 60;

  if (hours > 0) {
    return String(hours) + "s " + String(minutes) + "dk";
  }

  return String(minutes) + "dk";
}

String safeFilename(String value) {
  value.trim();
  if (value.length() == 0) {
    return "Baski yok";
  }
  return value;
}

uint16_t stateColor(const String& state) {
  if (state == "printing") return TFT_GREEN;
  if (state == "paused") return TFT_YELLOW;
  if (state == "complete") return TFT_CYAN;
  if (state == "standby" || state == "ready") return tft.color565(100, 100, 100);
  return TFT_RED;
}

String stateLabel(const String& state) {
  if (state == "printing") return "Yazdiriyor";
  if (state == "paused") return "Duraklatildi";
  if (state == "complete") return "Tamamlandi";
  if (state == "standby" || state == "ready") return "Beklemede";
  if (state == "error") return "Hata";
  return state;
}

void drawTextLine(int16_t x, int16_t y, const String& label, const String& value, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(label);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(x, y + 13);
  tft.print(value);
}

void drawHeader() {
  tft.fillRect(0, 0, screenWidth, 35, tft.color565(30, 30, 30));
  tft.setTextColor(TFT_WHITE, tft.color565(30, 30, 30));
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("YAZICI TAKIP");

  uint16_t color = WiFi.status() == WL_CONNECTED ? stateColor(printer.state) : TFT_RED;
  tft.fillCircle(290, 17, 8, color);
}

void drawFooter() {
  tft.fillRect(0, 218, screenWidth, 22, tft.color565(20, 20, 20));
  tft.setTextSize(1);
  tft.setTextColor(lastFetchOk ? TFT_GREEN : TFT_ORANGE, tft.color565(20, 20, 20));
  tft.setCursor(10, 225);

  if (WiFi.status() != WL_CONNECTED) {
    tft.print("WiFi baglantisi yok - tekrar deneniyor");
  } else if (!lastFetchOk) {
    tft.print(printer.message.substring(0, 42));
  } else {
    tft.print("IP: " + WiFi.localIP().toString() + " | " + stateLabel(printer.state));
  }
}

void drawUI() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();

  drawTextLine(10, 45, "HOTEND", String((int)printer.hotendTemp) + "/" +
               String((int)printer.hotendTarget) + "C", tft.color565(255, 100, 50));
  drawTextLine(170, 45, "YATAK", String((int)printer.bedTemp) + "/" +
               String((int)printer.bedTarget) + "C", tft.color565(50, 150, 255));

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 100);
  tft.print("ILERLEME");
  tft.drawRect(10, 115, 300, 22, TFT_WHITE);

  int barWidth = constrain((int)(printer.progress * 2.98), 0, 298);
  tft.fillRect(11, 116, barWidth, 20, tft.color565(0, 200, 100));
  tft.setTextSize(2);
  tft.setCursor(130, 118);
  tft.print(String((int)printer.progress) + "%");

  tft.setTextColor(tft.color565(180, 180, 180), TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 152);
  tft.print("DOSYA:");
  tft.setCursor(10, 166);
  tft.print(printer.filename.substring(0, 42));

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 190);
  tft.print("ETA: " + printer.eta);

  drawFooter();
}

void showSplash(const String& line1, const String& line2, uint16_t color) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(35, 95);
  tft.print(line1);
  tft.setTextSize(1);
  tft.setCursor(35, 125);
  tft.print(line2);
}

void connectWiFi(bool force = false) {
  if (WiFi.status() == WL_CONNECTED) return;
  if (!force && millis() - lastWifiRetry < wifiRetryMs) return;

  lastWifiRetry = millis();
  WiFi.disconnect(false);
  WiFi.begin(ssid, password);
}

bool updatePrinterDataFromJson(const String& payload) {
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    printer.message = "JSON okunamadi: " + String(error.c_str());
    return false;
  }

  JsonVariant status = doc["result"]["status"];
  if (status.isNull()) {
    printer.message = "Moonraker yaniti eksik";
    return false;
  }

  printer.hotendTemp = status["extruder"]["temperature"] | 0.0;
  printer.hotendTarget = status["extruder"]["target"] | 0.0;
  printer.bedTemp = status["heater_bed"]["temperature"] | 0.0;
  printer.bedTarget = status["heater_bed"]["target"] | 0.0;
  printer.progress = constrain((status["display_status"]["progress"] | 0.0) * 100.0, 0.0, 100.0);
  printer.state = status["print_stats"]["state"] | "standby";
  printer.filename = safeFilename(status["print_stats"]["filename"] | "Baski yok");

  float printDuration = status["print_stats"]["print_duration"] | 0.0;
  if (printer.progress > 0.1 && printer.progress < 99.9) {
    float totalDuration = printDuration / (printer.progress / 100.0);
    printer.eta = formatDuration(totalDuration - printDuration);
  } else {
    printer.eta = "--";
  }

  printer.message = "Veri guncellendi";
  return true;
}

bool getData() {
  if (WiFi.status() != WL_CONNECTED) {
    printer.message = "WiFi baglantisi bekleniyor";
    return false;
  }

  HTTPClient http;
  http.setTimeout(requestTimeoutMs);
  http.begin(moonrakerUrl());
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    printer.message = "HTTP hata: " + String(code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  return updatePrinterDataFromJson(payload);
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  showSplash("WiFi baglaniyor...", ssid, TFT_WHITE);

  WiFi.mode(WIFI_STA);
  connectWiFi(true);
}

void loop() {
  connectWiFi();

  if (lastDataRefresh == 0 || millis() - lastDataRefresh >= dataRefreshMs) {
    lastDataRefresh = millis();
    lastFetchOk = getData();
    drawUI();
  }
}
