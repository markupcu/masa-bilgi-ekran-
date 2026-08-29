/**
 * ESP32 Masa Bilgi Ekrani
 *
 * Moonraker/Klipper yazici durumunu, bilgisayardan gelen aktif muzik bilgisini
 * ve saati 320x240 TFT ekranda gosterir. TFT_eSPI pin ayarlari kutuphanenin
 * User_Setup dosyasindan yapilmalidir.
 */
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

TFT_eSPI tft;
WebServer server(80);

// Kendi ag ve Moonraker bilgilerinizi buraya girin.
const char *WIFI_SSID = "WIFI_ADINIZ";
const char *WIFI_PASSWORD = "WIFI_SIFRENIZ";
const char *MOONRAKER_HOST = "192.168.1.106";
constexpr uint16_t MOONRAKER_PORT = 7125;

constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;
constexpr uint32_t DATA_REFRESH_MS = 2000;
constexpr uint32_t WIFI_RETRY_MS = 5000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 1500;
constexpr uint32_t CLOCK_REFRESH_MS = 1000;
constexpr uint32_t MUSIC_STALE_MS = 6000;
constexpr uint32_t PRINTER_LOST_GRACE_MS = 15000;
constexpr uint8_t PRINTER_OFFLINE_CONFIRM_COUNT = 4;

const char *NTP_SERVER = "pool.ntp.org";
constexpr long GMT_OFFSET_SEC = 3 * 3600;
constexpr int DAYLIGHT_OFFSET_SEC = 0;

// RGB565 renk paleti.
constexpr uint16_t COLOR_BG = 0x0802;
constexpr uint16_t COLOR_CARD = 0x18C7;
constexpr uint16_t COLOR_CARD_DARK = 0x10A2;
constexpr uint16_t COLOR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_MUTED = 0x7BEF;
constexpr uint16_t COLOR_ACCENT = 0xF81F;
constexpr uint16_t COLOR_GRID = 0x4810;
constexpr uint16_t COLOR_HOTEND = 0xF28A;
constexpr uint16_t COLOR_BED = 0x3C1F;
constexpr uint16_t COLOR_WARN = 0xF500;
constexpr uint16_t COLOR_OK = TFT_CYAN;

struct PrinterData {
  float hotendTemp = 0;
  float hotendTarget = 0;
  float bedTemp = 0;
  float bedTarget = 0;
  uint8_t progress = 0;
  String filename = "Baski yok";
  String eta = "--";
  String state = "standby";
  String message = "Baslatiliyor";
};

struct MusicData {
  String title = "Muzik calmiyor";
  String artist = "Bilgisayar bosta";
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
  uint32_t receivedAtMs = 0;
  bool isPlaying = false;
};

struct UiCache {
  String time;
  String date;
  String title;
  String artist;
  String filename;
  String state;
  String eta;
  int hotend = -1;
  int hotendTarget = -1;
  int bed = -1;
  int bedTarget = -1;
  int progress = -1;
  uint16_t musicBar = UINT16_MAX;
};

enum ScreenMode { SCREEN_NONE, SCREEN_PRINTER, SCREEN_MUSIC, SCREEN_STANDBY };

PrinterData printer;
MusicData music;
UiCache ui;
ScreenMode activeScreen = SCREEN_NONE;
uint32_t lastDataRefresh = 0;
uint32_t lastWifiRetry = 0;
uint32_t lastClockRefresh = 0;
uint32_t lastSuccessfulPrinterFetch = 0;
uint8_t fetchFailures = 0;
bool lastFetchOk = false;

String twoDigits(int value) { return value < 10 ? "0" + String(value) : String(value); }

String dayName(int day) {
  static const char *names[] = {"PAZAR", "PAZARTESI", "SALI", "CARSAMBA", "PERSEMBE", "CUMA", "CUMARTESI"};
  return day >= 0 && day <= 6 ? names[day] : "";
}

String shortText(String text, uint8_t maxLength) {
  text.trim();
  if (text.length() <= maxLength) return text;
  return text.substring(0, maxLength - 3) + "...";
}

String safeFilename(String name) {
  name.trim();
  return name.length() ? name : "Baski yok";
}

String formatDuration(float seconds) {
  if (!isfinite(seconds) || seconds <= 0) return "--";
  uint32_t total = static_cast<uint32_t>(seconds);
  uint16_t hours = total / 3600;
  uint8_t minutes = (total % 3600) / 60;
  return hours ? String(hours) + "s " + String(minutes) + "dk" : String(minutes) + "dk";
}

String formatMusicTime(uint32_t milliseconds) {
  uint32_t seconds = milliseconds / 1000;
  return String(seconds / 60) + ":" + twoDigits(seconds % 60);
}

bool printerActiveState() { return printer.state == "printing" || printer.state == "paused"; }

bool printerScreenRequired() {
  if (!printerActiveState()) return false;
  if (lastFetchOk) return true;
  // Gecici Wi-Fi/Moonraker kesintilerinde aktif baski ekrani ziplayip kaybolmasin.
  return lastSuccessfulPrinterFetch && millis() - lastSuccessfulPrinterFetch < PRINTER_LOST_GRACE_MS &&
         fetchFailures < PRINTER_OFFLINE_CONFIRM_COUNT;
}

bool musicIsFresh() { return music.isPlaying && music.receivedAtMs && millis() - music.receivedAtMs < MUSIC_STALE_MS; }

uint16_t statusColor() {
  if (!lastFetchOk) return COLOR_WARN;
  if (printer.state == "printing") return COLOR_ACCENT;
  if (printer.state == "paused") return COLOR_WARN;
  if (printer.state == "complete") return COLOR_OK;
  return COLOR_MUTED;
}

String statusLabel() {
  if (!lastFetchOk && WiFi.status() != WL_CONNECTED) return "WiFi yok";
  if (!lastFetchOk) return "Yazici bekleniyor";
  if (printer.state == "printing") return "Yazdiriyor";
  if (printer.state == "paused") return "Duraklatildi";
  if (printer.state == "complete") return "Tamamlandi";
  return "Beklemede";
}

void resetUiCache() { ui = UiCache(); }

void drawCard(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t accent, const String &title) {
  tft.fillRoundRect(x, y, width, height, 6, COLOR_CARD);
  tft.fillRect(x, y, 3, height, accent);
  tft.setTextColor(COLOR_MUTED, COLOR_CARD);
  tft.setTextSize(1);
  tft.setCursor(x + 12, y + 8);
  tft.print(title);
}

void drawFooter(const String &text, uint16_t color = COLOR_MUTED) {
  tft.fillRect(0, 224, SCREEN_WIDTH, 16, COLOR_CARD_DARK);
  tft.setTextColor(color, COLOR_CARD_DARK);
  tft.setTextSize(1);
  tft.setCursor(10, 228);
  tft.print(shortText(text, 51));
}

void drawPrinterLayout() {
  tft.fillScreen(COLOR_BG);
  resetUiCache();
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, COLOR_CARD);
  tft.drawFastHLine(0, 39, SCREEN_WIDTH, COLOR_ACCENT);
  tft.setTextColor(COLOR_TEXT, COLOR_CARD);
  tft.setTextSize(2);
  tft.setCursor(12, 12);
  tft.print("DASHBOARD");
  drawCard(10, 50, 145, 60, COLOR_HOTEND, "HOTEND");
  drawCard(165, 50, 145, 60, COLOR_BED, "YATAK");
  drawCard(10, 118, 300, 56, COLOR_ACCENT, "BASKI ILERLEME");
  drawCard(10, 182, 300, 36, COLOR_MUTED, "AKTIF DOSYA");
  activeScreen = SCREEN_PRINTER;
}

void drawTemperature(int16_t x, int temp, int target, uint16_t color, int maximum) {
  tft.fillRect(x, 74, 120, 24, COLOR_CARD);
  tft.setTextColor(COLOR_TEXT, COLOR_CARD);
  tft.setTextSize(2);
  tft.setCursor(x, 74);
  tft.print(String(temp) + "C");
  tft.setTextSize(1);
  tft.setTextColor(COLOR_MUTED, COLOR_CARD);
  tft.print(" / " + String(target) + "C");
  tft.fillRect(x, 98, 121, 3, COLOR_CARD_DARK);
  int bar = map(constrain(temp, 0, maximum), 0, maximum, 0, 121);
  if (bar) tft.fillRect(x, 98, bar, 3, color);
}

void updatePrinterUi() {
  if (activeScreen != SCREEN_PRINTER) drawPrinterLayout();
  String state = statusLabel();
  if (ui.state != state) {
    ui.state = state;
    tft.fillRect(178, 10, 132, 20, COLOR_CARD);
    tft.setTextColor(statusColor(), COLOR_CARD);
    tft.setTextSize(1);
    tft.setCursor(308 - tft.textWidth(state), 14);
    tft.print(state);
  }
  int hotend = round(printer.hotendTemp), hotendTarget = round(printer.hotendTarget);
  if (ui.hotend != hotend || ui.hotendTarget != hotendTarget) {
    ui.hotend = hotend; ui.hotendTarget = hotendTarget;
    drawTemperature(22, hotend, hotendTarget, COLOR_HOTEND, 280);
  }
  int bed = round(printer.bedTemp), bedTarget = round(printer.bedTarget);
  if (ui.bed != bed || ui.bedTarget != bedTarget) {
    ui.bed = bed; ui.bedTarget = bedTarget;
    drawTemperature(177, bed, bedTarget, COLOR_BED, 120);
  }
  if (ui.progress != printer.progress || ui.eta != printer.eta) {
    ui.progress = printer.progress; ui.eta = printer.eta;
    tft.fillRect(240, 124, 60, 15, COLOR_CARD);
    String percentage = String(printer.progress) + "%";
    tft.setTextColor(COLOR_TEXT, COLOR_CARD); tft.setTextSize(1);
    tft.setCursor(300 - tft.textWidth(percentage), 126); tft.print(percentage);
    tft.fillRect(22, 142, 276, 4, COLOR_CARD_DARK);
    uint16_t bar = map(printer.progress, 0, 100, 0, 276);
    if (bar) tft.fillRect(22, 142, bar, 4, COLOR_ACCENT);
    tft.fillRect(22, 154, 270, 12, COLOR_CARD);
    tft.setTextColor(COLOR_WARN, COLOR_CARD); tft.setCursor(22, 154);
    tft.print("Kalan sure: " + printer.eta);
  }
  String filename = shortText(printer.filename, 42);
  if (ui.filename != filename) {
    ui.filename = filename;
    tft.fillRect(22, 198, 276, 15, COLOR_CARD);
    tft.setTextColor(COLOR_TEXT, COLOR_CARD); tft.setTextSize(1); tft.setCursor(22, 198); tft.print(filename);
  }
  drawFooter(lastFetchOk ? "IP: " + WiFi.localIP().toString() + " | " + statusLabel() : printer.message,
             lastFetchOk ? COLOR_ACCENT : COLOR_WARN);
}

void drawClock(bool withSeconds, uint16_t background) {
  tm now;
  if (!getLocalTime(&now, 20)) return;
  String time = twoDigits(now.tm_hour) + ":" + twoDigits(now.tm_min);
  if (withSeconds) time += ":" + twoDigits(now.tm_sec);
  String date = twoDigits(now.tm_mday) + " " + dayName(now.tm_wday);
  if (ui.time != time) {
    ui.time = time; tft.fillRect(12, 10, 112, 22, background);
    tft.setTextColor(COLOR_TEXT, background); tft.setTextSize(2); tft.setCursor(12, 12); tft.print(time);
  }
  if (ui.date != date) {
    ui.date = date; tft.fillRect(160, 10, 150, 22, background);
    tft.setTextColor(COLOR_MUTED, background); tft.setTextSize(1); tft.setCursor(308 - tft.textWidth(date), 16); tft.print(date);
  }
}

void drawMusicLayout() {
  tft.fillScreen(COLOR_BG); resetUiCache();
  tft.fillRect(0, 0, SCREEN_WIDTH, 42, COLOR_CARD); tft.drawFastHLine(0, 41, SCREEN_WIDTH, COLOR_ACCENT);
  drawCard(10, 54, 300, 158, COLOR_ACCENT, "SIMDI CALIYOR");
  activeScreen = SCREEN_MUSIC;
}

void updateMusicUi() {
  if (activeScreen != SCREEN_MUSIC) drawMusicLayout();
  drawClock(false, COLOR_CARD);
  String title = shortText(music.title, 24), artist = shortText(music.artist, 38);
  if (ui.title != title) {
    ui.title = title; tft.fillRect(20, 84, 280, 24, COLOR_CARD); tft.setTextColor(COLOR_TEXT, COLOR_CARD); tft.setTextSize(2);
    tft.setCursor(160 - tft.textWidth(title) / 2, 86); tft.print(title);
  }
  if (ui.artist != artist) {
    ui.artist = artist; tft.fillRect(20, 114, 280, 18, COLOR_CARD); tft.setTextColor(COLOR_MUTED, COLOR_CARD); tft.setTextSize(1);
    tft.setCursor(160 - tft.textWidth(artist) / 2, 116); tft.print(artist);
  }
  uint32_t progress = music.progressMs;
  if (music.isPlaying && music.durationMs) progress = min(music.durationMs, progress + millis() - music.receivedAtMs);
  tft.fillRect(25, 142, 50, 12, COLOR_CARD); tft.setTextColor(COLOR_MUTED, COLOR_CARD); tft.setTextSize(1); tft.setCursor(25, 142); tft.print(formatMusicTime(progress));
  String duration = formatMusicTime(music.durationMs); tft.fillRect(245, 142, 50, 12, COLOR_CARD); tft.setCursor(295 - tft.textWidth(duration), 142); tft.print(duration);
  uint16_t bar = music.durationMs ? (270UL * progress) / music.durationMs : 0;
  if (ui.musicBar != bar) { ui.musicBar = bar; tft.fillRect(25, 160, 270, 3, COLOR_CARD_DARK); if (bar) tft.fillRect(25, 160, bar, 3, COLOR_ACCENT); }
  drawFooter("Muzik bilgisayardan dinleniyor...");
}

void drawStandbyLayout() {
  tft.fillScreen(COLOR_BG); resetUiCache();
  tft.drawFastHLine(0, 39, SCREEN_WIDTH, COLOR_ACCENT);
  const int sunX = 160, sunY = 95, radius = 45;
  for (int r = radius; r > 0; --r) tft.drawCircle(sunX, sunY, r, tft.color565(255, min(255, 30 + (radius - r) * 4), 10));
  for (int y = sunY - 10; y < sunY + radius; y += 7) tft.drawFastHLine(sunX - radius, y, radius * 2, COLOR_BG);
  tft.drawFastHLine(0, 130, SCREEN_WIDTH, COLOR_ACCENT);
  for (int offset = -180; offset <= 180; offset += 30) tft.drawLine(160, 130, 160 + offset * 2, 220, COLOR_GRID);
  for (float y = 132, spacing = 2; y < 220; spacing *= 1.35, y += spacing) tft.drawFastHLine(0, static_cast<int>(y), SCREEN_WIDTH, COLOR_GRID);
  activeScreen = SCREEN_STANDBY;
}

void updateStandbyUi() {
  if (activeScreen != SCREEN_STANDBY) drawStandbyLayout();
  drawClock(true, COLOR_BG);
  drawFooter("Sistem hazir | IP: " + WiFi.localIP().toString());
}

String moonrakerUrl() {
  return "http://" + String(MOONRAKER_HOST) + ":" + String(MOONRAKER_PORT) + "/printer/objects/query?extruder&heater_bed&print_stats&display_status";
}

bool parsePrinterPayload(const String &payload) {
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);
  JsonVariant status = doc["result"]["status"];
  if (error || status.isNull()) { printer.message = error ? "JSON okunamadi" : "Moonraker yaniti eksik"; return false; }
  printer.hotendTemp = status["extruder"]["temperature"] | 0.0F;
  printer.hotendTarget = status["extruder"]["target"] | 0.0F;
  printer.bedTemp = status["heater_bed"]["temperature"] | 0.0F;
  printer.bedTarget = status["heater_bed"]["target"] | 0.0F;
  printer.progress = constrain(static_cast<int>((status["display_status"]["progress"] | 0.0F) * 100.0F), 0, 100);
  printer.state = status["print_stats"]["state"] | "standby";
  printer.filename = safeFilename(status["print_stats"]["filename"] | "Baski yok");
  float elapsed = status["print_stats"]["print_duration"] | 0.0F;
  printer.eta = printer.progress > 0 && printer.progress < 100 ? formatDuration(elapsed * (100.0F - printer.progress) / printer.progress) : "--";
  printer.message = "Veri guncellendi";
  return true;
}

bool fetchPrinterData() {
  if (WiFi.status() != WL_CONNECTED) { printer.message = "WiFi baglantisi bekleniyor"; return false; }
  HTTPClient http; http.setTimeout(REQUEST_TIMEOUT_MS);
  if (!http.begin(moonrakerUrl())) { printer.message = "HTTP baslatilamadi"; return false; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) { printer.message = "Moonraker HTTP hata: " + String(code); http.end(); return false; }
  String payload = http.getString(); http.end();
  return parsePrinterPayload(payload);
}

void handleMusicPost() {
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"Veri yok\"}"); return; }
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"JSON hatasi\"}"); return; }
  music.title = doc["title"] | "Muzik calmiyor";
  music.artist = doc["artist"] | "Bilinmeyen sanatci";
  music.progressMs = doc["progress"] | 0UL;
  music.durationMs = doc["duration"] | 0UL;
  music.isPlaying = doc["playing"] | false;
  music.receivedAtMs = millis();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStatus() {
  DynamicJsonDocument doc(384);
  doc["wifi"] = WiFi.status() == WL_CONNECTED;
  doc["ip"] = WiFi.localIP().toString();
  doc["printerState"] = printer.state;
  doc["printerReachable"] = lastFetchOk;
  doc["musicFresh"] = musicIsFresh();
  String response; serializeJson(doc, response);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
}

void connectWiFi(bool force = false) {
  if (WiFi.status() == WL_CONNECTED || (!force && millis() - lastWifiRetry < WIFI_RETRY_MS)) return;
  lastWifiRetry = millis(); WiFi.disconnect(false); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup() {
  Serial.begin(115200);
  tft.init(); tft.setRotation(1); tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG); tft.setTextSize(2); tft.setCursor(36, 96); tft.print("Sistem baslatiliyor");
  WiFi.mode(WIFI_STA); connectWiFi(true); configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  server.on("/music", HTTP_POST, handleMusicPost);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound([]() { server.send(404, "application/json", "{\"error\":\"Bulunamadi\"}"); });
  server.begin();
}

void loop() {
  connectWiFi(); server.handleClient();
  if (!lastDataRefresh || millis() - lastDataRefresh >= DATA_REFRESH_MS) {
    lastDataRefresh = millis(); lastFetchOk = fetchPrinterData();
    if (lastFetchOk) { fetchFailures = 0; lastSuccessfulPrinterFetch = millis(); }
    else if (fetchFailures < UINT8_MAX) ++fetchFailures;
  }
  if (printerScreenRequired()) updatePrinterUi();
  else if (musicIsFresh()) updateMusicUi();
  else updateStandbyUi();
  // Muzik ilerleme cubugu ve saat, HTTP/Moonraker sorgusundan bagimsiz akar.
  if (millis() - lastClockRefresh >= CLOCK_REFRESH_MS) {
    lastClockRefresh = millis();
    if (printerScreenRequired()) updatePrinterUi(); else if (musicIsFresh()) updateMusicUi(); else updateStandbyUi();
  }
}
