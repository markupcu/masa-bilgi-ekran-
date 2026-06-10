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

// Renk paleti
const uint16_t colorBg = 0x0841;
const uint16_t colorPanel = 0x10A2;
const uint16_t colorPanelDark = 0x0861;
const uint16_t colorText = TFT_WHITE;
const uint16_t colorMuted = 0x9CF3;
const uint16_t colorHotend = 0xFB20;
const uint16_t colorBed = 0x2D7F;
const uint16_t colorAccent = 0x07F2;
const uint16_t colorWarn = 0xFFE0;
const uint16_t colorDanger = 0xF800;

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
bool layoutDrawn = false;

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
  if (state == "printing") return colorAccent;
  if (state == "paused") return colorWarn;
  if (state == "complete") return TFT_CYAN;
  if (state == "standby" || state == "ready") return colorMuted;
  return colorDanger;
}

String stateLabel(const String& state) {
  if (state == "printing") return "Yazdiriyor";
  if (state == "paused") return "Duraklatildi";
  if (state == "complete") return "Tamamlandi";
  if (state == "standby" || state == "ready") return "Beklemede";
  if (state == "error") return "Hata";
  return state;
}

String shortText(String value, uint8_t maxLen) {
  value.trim();
  if (value.length() <= maxLen) return value;
  return value.substring(0, maxLen - 3) + "...";
}


void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t borderColor, const String& title) {
  tft.fillRoundRect(x, y, w, h, 10, colorPanel);
  tft.drawRoundRect(x, y, w, h, 10, borderColor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, 17, 8, colorPanelDark);
  tft.setTextColor(borderColor, colorPanelDark);
  tft.setTextSize(1);
  tft.setCursor(x + 10, y + 6);
  tft.print(title);
}

void drawStaticLayout() {
  tft.fillScreen(colorBg);

  // Ust kisim: koyu bir baslik ve renkli vurgu cizgisi.
  tft.fillRect(0, 0, screenWidth, 42, 0x18E3);
  tft.fillRect(0, 40, screenWidth, 2, colorAccent);
  tft.setTextColor(colorText, 0x18E3);
  tft.setTextSize(2);
  tft.setCursor(12, 8);
  tft.print("YAZICI PANELI");
  tft.setTextColor(colorMuted, 0x18E3);
  tft.setTextSize(1);
  tft.setCursor(14, 28);
  tft.print("Moonraker durum ekrani");

  drawCard(10, 52, 145, 62, colorHotend, "HOTEND");
  drawCard(165, 52, 145, 62, colorBed, "YATAK");
  drawCard(10, 124, 300, 52, colorAccent, "BASKI ILERLEME");
  drawCard(10, 184, 300, 36, colorMuted, "AKTIF DOSYA");

  layoutDrawn = true;
}

void updateHeaderStatus() {
  uint16_t status = WiFi.status() == WL_CONNECTED ? stateColor(printer.state) : colorDanger;
  String label = WiFi.status() == WL_CONNECTED ? stateLabel(printer.state) : "WiFi yok";

  tft.fillRoundRect(218, 8, 90, 24, 12, 0x0841);
  tft.drawRoundRect(218, 8, 90, 24, 12, status);
  tft.fillCircle(232, 20, 5, status);
  tft.setTextColor(colorText, 0x0841);
  tft.setTextSize(1);
  tft.setCursor(243, 17);
  tft.print(shortText(label, 10));
}

void updateTemperatureCard(int16_t x, int16_t y, float current, float target, uint16_t accent) {
  tft.fillRect(x + 8, y + 24, 129, 30, colorPanel);
  tft.setTextColor(colorText, colorPanel);
  tft.setTextSize(2);
  tft.setCursor(x + 12, y + 28);
  tft.print(String((int)current));
  tft.setTextSize(1);
  tft.print(" / ");
  tft.setTextSize(2);
  tft.print(String((int)target));
  tft.setTextSize(1);
  tft.print(" C");

  int16_t meterWidth = map(constrain((int)current, 0, 300), 0, 300, 0, 118);
  tft.fillRoundRect(x + 12, y + 51, 118, 5, 3, colorPanelDark);
  tft.fillRoundRect(x + 12, y + 51, meterWidth, 5, 3, accent);
}

void updateProgressCard() {
  int progressBarWidth = constrain((int)(printer.progress * 2.72), 0, 272);

  tft.fillRect(22, 149, 276, 16, colorPanel);
  tft.fillRoundRect(22, 149, 276, 16, 8, colorPanelDark);
  if (progressBarWidth > 0) {
    tft.fillRoundRect(22, 149, progressBarWidth, 16, 8, colorAccent);
  }

  tft.fillRect(228, 130, 68, 14, colorPanelDark);
  tft.setTextColor(colorText, colorPanelDark);
  tft.setTextSize(1);
  tft.setCursor(242, 133);
  tft.print(String((int)printer.progress) + "%");

  tft.fillRect(22, 167, 180, 8, colorPanel);
  tft.setTextColor(colorWarn, colorPanel);
  tft.setCursor(22, 167);
  tft.print("ETA: " + printer.eta);
}

void updateFileCard() {
  tft.fillRect(22, 205, 276, 10, colorPanel);
  tft.setTextColor(colorText, colorPanel);
  tft.setTextSize(1);
  tft.setCursor(22, 205);
  tft.print(shortText(printer.filename, 43));
}

void updateFooter() {
  tft.fillRect(0, 222, screenWidth, 18, 0x18E3);
  tft.setTextSize(1);
  tft.setTextColor(lastFetchOk ? colorAccent : colorWarn, 0x18E3);
  tft.setCursor(10, 228);

  if (WiFi.status() != WL_CONNECTED) {
    tft.print("WiFi tekrar baglanmayi deniyor");
  } else if (!lastFetchOk) {
    tft.print(shortText(printer.message, 43));
  } else {
    tft.print("IP " + WiFi.localIP().toString() + "  |  " + stateLabel(printer.state));
  }
}

void updateUI() {
  if (!layoutDrawn) {
    drawStaticLayout();
  }

  updateHeaderStatus();
  updateTemperatureCard(10, 52, printer.hotendTemp, printer.hotendTarget, colorHotend);
  updateTemperatureCard(165, 52, printer.bedTemp, printer.bedTarget, colorBed);
  updateProgressCard();
  updateFileCard();
  updateFooter();
}

void showSplash(const String& line1, const String& line2, uint16_t color) {
  tft.fillScreen(colorBg);
  tft.fillRoundRect(28, 70, 264, 92, 14, colorPanel);
  tft.drawRoundRect(28, 70, 264, 92, 14, colorAccent);
  tft.setTextColor(color, colorPanel);
  tft.setTextSize(2);
  tft.setCursor(48, 92);
  tft.print(line1);
  tft.setTextColor(colorMuted, colorPanel);
  tft.setTextSize(1);
  tft.setCursor(48, 125);
  tft.print(line2);
  layoutDrawn = false;
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
  showSplash("WiFi baglaniyor", ssid, colorText);

  WiFi.mode(WIFI_STA);
  connectWiFi(true);
}

void loop() {
  connectWiFi();

  if (lastDataRefresh == 0 || millis() - lastDataRefresh >= dataRefreshMs) {
    lastDataRefresh = millis();
    lastFetchOk = getData();
    updateUI();
  }
}
