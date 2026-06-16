#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <time.h>

// =========================
// Confirmed Project Pin Map
// =========================

#define TFT_DC    9
#define TFT_RST  -1   // Display RES is physically tied to ESP32 EN/RST
#define TFT_SCK  12
#define TFT_MOSI 11

#define MIC_PIN      1
#define AMP_PIN      4
#define TOUCH_PIN    5
#define BUTTON_PIN   6
#define TEST_LED_PIN 2

#define SAMPLE_RATE 8000
#define PWM_CARRIER_RATE 40000
#define MAX_RECORD_SECONDS 5
#define MAX_AUDIO_BYTES (SAMPLE_RATE * MAX_RECORD_SECONDS)
#define MIN_HOLD_RECORD_MS 1000
#define SILENCE_STOP_MS 1200
#define IDLE_CLOCK_MS 15000

#define AP_NAME "AI_Buddy_Setup"
#define AP_PASSWORD "12345678"
#define DEFAULT_LOCAL_SERVER "http://192.168.207.192:3000"
#define GMT_OFFSET_SEC (6 * 60 * 60)
#define DAYLIGHT_OFFSET_SEC 0

const uint8_t SETUP_QR_SIZE = 31;
const uint32_t SETUP_QR[31] PROGMEM = {
  0x00000000, 0x3FB344FE, 0x208C1C82, 0x2E8E9EBA,
  0x2EB94EBA, 0x2EBC4CBA, 0x20B5A482, 0x3FAAAAFE,
  0x00345800, 0x22F4B1F2, 0x0F7A41B0, 0x3DCB7932,
  0x0D64D344, 0x118AB764, 0x0A3A4070, 0x36AB59AA,
  0x3530BAF2, 0x12DDB74A, 0x3150C028, 0x05E3F302,
  0x051132C0, 0x3FFD3FE4, 0x0033D234, 0x3FA246AA,
  0x209CFE36, 0x2EB3A7EA, 0x2E90452A, 0x2E91709E,
  0x20913056, 0x3FB27BAC, 0x00000000,
};

Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, -1, TFT_SCK, TFT_MOSI, -1, &SPI, true);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0, true, 240, 240, 0, 0, 0, 80);
WebServer portal(80);
Preferences prefs;

enum AppMode {
  MODE_CLOCK,
  MODE_SETUP,
  MODE_EXTENDER,
  MODE_LOCAL_ARMED,
  MODE_RECORDING,
  MODE_SENDING,
  MODE_STATUS
};

enum HomePage {
  PAGE_DIGITAL,
  PAGE_ANALOG,
  PAGE_TODAY_WEATHER,
  PAGE_FORECAST,
  PAGE_SYSTEM,
  PAGE_AI
};

enum ButtonEvent {
  BUTTON_NONE,
  BUTTON_SHORT,
  BUTTON_LONG
};

String wifiSsid;
String wifiPass;
String localServer;
String weatherCity;
String weatherTemp = "--";
String weatherCond = "";
String weatherFeels = "--";
String weatherHumidity = "--";
String weatherWind = "--";
String forecastDay[3] = {"--", "--", "--"};
String forecastLow[3] = {"--", "--", "--"};
String forecastHigh[3] = {"--", "--", "--"};
String forecastCond[3] = {"--", "--", "--"};
String deviceId;
HomePage homePage = PAGE_DIGITAL;
AppMode mode = MODE_CLOCK;

uint8_t *audioBuffer = nullptr;
size_t audioLength = 0;

uint32_t lastDrawMs = 0;
uint32_t lastClockSyncMs = 0;
uint32_t lastWeatherMs = 0;
uint32_t lastActionMs = 0;
uint32_t touchStartedMs = 0;
uint32_t recordStartedMs = 0;
uint32_t lastSoundMs = 0;
uint32_t lastSerialMs = 0;
uint32_t lastLedMs = 0;
uint32_t buttonDownAt = 0;
uint32_t lastAnimMs = 0;
int lastClockMinuteDrawn = -1;
int lastHomePageDrawn = -1;
int lastSystemDrawSecond = -1;
bool lastButtonDown = false;
bool buttonLongFired = false;
bool lastTouchDown = false;
bool ledState = false;
bool localAiArmed = false;
bool timeReady = false;
bool uiFull = true;
bool setupPortalRunning = false;
bool extenderRunning = false;
bool touchIdleHigh = false;
uint16_t micMid = 2048;
float micLevel = 0.0f;
float noiseFloor = 90.0f;
String statusText = "Ready";
String lastTranscript = "";
String lastAudioUrl = "";
uint8_t visualBins[16] = {0};
uint8_t previousBins[16] = {0};
int aiPrevDotX[5] = {-1, -1, -1, -1, -1};
int aiPrevDotY[5] = {-1, -1, -1, -1, -1};
int aiPrevDotR[5] = {0, 0, 0, 0, 0};
int aiPrevWaitX = -1;
int aiPrevWaitY = -1;
int aiPrevWaitW = 0;
int aiPrevWaitH = 0;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

bool ensureAudioBuffer() {
  if (audioBuffer) return true;
  if (psramFound()) audioBuffer = (uint8_t *)ps_malloc(MAX_AUDIO_BYTES);
  if (!audioBuffer) audioBuffer = (uint8_t *)malloc(MAX_AUDIO_BYTES);
  return audioBuffer != nullptr;
}

void releaseAudioBuffer() {
  if (!audioBuffer) return;
  free(audioBuffer);
  audioBuffer = nullptr;
  audioLength = 0;
}

void drawThickLine(int x1, int y1, int x2, int y2, uint8_t w, uint16_t color) {
  for (int i = -(int)w / 2; i <= (int)w / 2; i++) {
    if (abs(x2 - x1) > abs(y2 - y1)) gfx->drawLine(x1, y1 + i, x2, y2 + i, color);
    else gfx->drawLine(x1 + i, y1, x2 + i, y2, color);
  }
}

void drawSevenDigit(int x, int y, int s, int digit, uint16_t color, uint16_t offColor) {
  static const uint8_t segs[10] = {
    0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
    0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011
  };
  int w = s * 4;
  int h = s * 7;
  int t = max(2, s);
  int x2 = x + w;
  int ym = y + h / 2;
  int y2 = y + h;
  uint8_t bits = segs[digit % 10];
  uint16_t colors[7];
  for (int i = 0; i < 7; i++) colors[i] = (bits & (1 << (6 - i))) ? color : offColor;
  drawThickLine(x + t, y, x2 - t, y, t, colors[0]);
  drawThickLine(x2, y + t, x2, ym - t, t, colors[1]);
  drawThickLine(x2, ym + t, x2, y2 - t, t, colors[2]);
  drawThickLine(x + t, y2, x2 - t, y2, t, colors[3]);
  drawThickLine(x, ym + t, x, y2 - t, t, colors[4]);
  drawThickLine(x, y + t, x, ym - t, t, colors[5]);
  drawThickLine(x + t, ym, x2 - t, ym, t, colors[6]);
}

void drawSevenNumber(int x, int y, int s, const char *value, uint16_t color, uint16_t offColor, bool colon = true) {
  int cursor = x;
  for (size_t i = 0; value[i]; i++) {
    if (value[i] == ':') {
      if (colon) {
        gfx->fillCircle(cursor + s, y + s * 2, max(2, s / 2), color);
        gfx->fillCircle(cursor + s, y + s * 5, max(2, s / 2), color);
      }
      cursor += s * 2;
    } else if (isdigit((unsigned char)value[i])) {
      drawSevenDigit(cursor, y, s, value[i] - '0', color, offColor);
      cursor += s * 5;
    }
  }
}

void drawDotDigit(int x, int y, int scale, int digit, uint16_t color, uint16_t offColor) {
  static const uint8_t font[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}
  };
  int r = max(2, (scale + 1) / 2);
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 5; col++) {
      uint16_t c = (font[digit % 10][row] & (1 << (4 - col))) ? color : offColor;
      if (c == color) {
        gfx->fillCircle(x + col * scale, y + row * scale, r, c);
      } else if (offColor) {
        gfx->fillCircle(x + col * scale, y + row * scale, max(1, r - 2), c);
      }
    }
  }
}

void drawDotNumber(int x, int y, int scale, const char *value, uint16_t color, uint16_t offColor) {
  int cursor = x;
  for (size_t i = 0; value[i]; i++) {
    if (value[i] == ':') {
      gfx->fillCircle(cursor, y + scale * 2, max(1, scale / 2), color);
      gfx->fillCircle(cursor, y + scale * 4, max(1, scale / 2), color);
      cursor += scale * 2;
    } else if (isdigit((unsigned char)value[i])) {
      drawDotDigit(cursor, y, scale, value[i] - '0', color, offColor);
      cursor += scale * 6;
    }
  }
}

int textWidthPx(const String &value, uint8_t size) {
  return value.length() * 6 * size;
}

void drawCenteredText(const String &value, int y, uint8_t size, uint16_t color, uint16_t bg, int maxChars = 0) {
  String line = value;
  if (maxChars > 0 && (int)line.length() > maxChars) line = line.substring(0, maxChars);
  int x = (240 - textWidthPx(line, size)) / 2;
  if (x < 0) x = 0;
  gfx->setTextSize(size);
  gfx->setTextColor(color, bg);
  gfx->setCursor(x, y);
  gfx->print(line);
}

void drawHudTicks(int x, int y, int w, int h, uint16_t color, uint16_t majorColor) {
  for (int i = 0; i < 28; i++) {
    int tx = x + 16 + i * (w - 32) / 27;
    int len = (i % 7 == 0) ? 9 : 5;
    uint16_t c = (i % 7 == 0) ? majorColor : color;
    gfx->drawFastVLine(tx, y + 4, len, c);
    gfx->drawFastVLine(tx, y + h - 4 - len, len, c);
  }
  for (int i = 0; i < 22; i++) {
    int ty = y + 16 + i * (h - 32) / 21;
    int len = (i % 7 == 0) ? 9 : 5;
    uint16_t c = (i % 7 == 0) ? majorColor : color;
    gfx->drawFastHLine(x + 4, ty, len, c);
    gfx->drawFastHLine(x + w - 4 - len, ty, len, c);
  }
}

void drawHudFrame(uint16_t accent, uint16_t tickColor) {
  uint16_t bg = rgb565(18, 20, 19);
  uint16_t line = rgb565(28, 32, 32);
  uint16_t shadow = rgb565(7, 8, 9);
  gfx->fillScreen(bg);
  gfx->drawRoundRect(5, 5, 230, 230, 28, shadow);
  gfx->drawRoundRect(8, 8, 224, 224, 22, line);
  gfx->drawRoundRect(14, 14, 212, 212, 18, rgb565(34, 38, 38));
  gfx->drawRoundRect(28, 28, 184, 184, 14, rgb565(22, 25, 25));
  gfx->drawRoundRect(35, 35, 170, 170, 12, rgb565(30, 34, 34));
  drawHudTicks(14, 14, 212, 212, tickColor, accent);
  gfx->drawFastHLine(44, 13, 48, rgb565(64, 112, 48));
  gfx->drawFastHLine(114, 13, 52, rgb565(255, 74, 78));
  gfx->drawFastVLine(226, 116, 50, rgb565(255, 174, 36));
  gfx->drawFastVLine(226, 168, 40, rgb565(21, 239, 216));
  gfx->drawFastHLine(46, 226, 62, rgb565(21, 239, 216));
  gfx->drawFastHLine(116, 226, 60, rgb565(75, 161, 255));
  gfx->drawFastVLine(13, 118, 48, accent);
}

void drawHudGrid(uint16_t color, int step = 17) {
  for (int y = 55; y <= 181; y += step) {
    for (int x = 55; x <= 181; x += step) {
      gfx->fillCircle(x, y, 1, color);
    }
  }
}

void drawHudGridArea(uint16_t color, int left, int top, int right, int bottom, int step = 17) {
  for (int y = 55; y <= 181; y += step) {
    if (y < top || y > bottom) continue;
    for (int x = 55; x <= 181; x += step) {
      if (x < left || x > right) continue;
      gfx->fillCircle(x, y, 1, color);
    }
  }
}

void drawDotPill(int x, int y, int w, int h, uint16_t color, uint16_t dim) {
  for (int px = x + 10; px <= x + w - 10; px += 8) {
    gfx->fillCircle(px, y, 2, color);
    gfx->fillCircle(px, y + h, 2, color);
  }
  for (int py = y + 10; py <= y + h - 10; py += 8) {
    gfx->fillCircle(x, py, 2, color);
    gfx->fillCircle(x + w, py, 2, color);
  }
  for (int py = y + 15; py <= y + h - 15; py += 17) {
    for (int px = x + 17; px <= x + w - 17; px += 17) {
      gfx->fillCircle(px, py, 1, dim);
    }
  }
}

void drawFlickerWeatherIcon(int cx, int cy, uint16_t color, uint8_t pattern) {
  static const uint8_t icons[5][7] = {
    {0x14,0x08,0x49,0x3E,0x49,0x08,0x14},
    {0x00,0x1C,0x3E,0x7F,0x7E,0x3C,0x00},
    {0x00,0x1C,0x3E,0x7F,0x14,0x2A,0x44},
    {0x1C,0x3E,0x7F,0x08,0x18,0x0C,0x10},
    {0x00,0x77,0x00,0x3E,0x00,0x77,0x00}
  };
  uint8_t idx = pattern % 5;
  uint32_t now = millis() / 90;
  int dotStep = 13;
  int x0 = cx - dotStep * 3;
  int y0 = cy - dotStep * 3;
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 7; col++) {
      bool on = icons[idx][row] & (1 << (6 - col));
      int pulse = (on && ((now + row + col) % 6 == 0)) ? 2 : 0;
      int r = on ? 5 + pulse : 2;
      uint16_t c = on ? color : rgb565(34, 42, 34);
      gfx->fillCircle(x0 + col * dotStep, y0 + row * dotStep, r, c);
    }
  }
}

String htmlEscape(const String &input) {
  String out = input;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  return out;
}

void beep(uint16_t freq, uint16_t ms) {
  if (!freq || !ms) return;
  ledcWriteTone(AMP_PIN, freq);
  delay(ms);
  ledcWriteTone(AMP_PIN, 0);
  ledcChangeFrequency(AMP_PIN, PWM_CARRIER_RATE, 8);
  ledcWrite(AMP_PIN, 128);
  delay(2);
  ledcWrite(AMP_PIN, 0);
}

void setMode(AppMode next, const String &text = "") {
  mode = next;
  if (text.length()) statusText = text;
  uiFull = true;
  if (next == MODE_CLOCK) {
    lastClockMinuteDrawn = -1;
    lastHomePageDrawn = -1;
    lastSystemDrawSecond = -1;
  }
  lastActionMs = millis();
}

void saveConfig() {
  prefs.begin("buddy", false);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putString("server", localServer);
  prefs.putString("city", weatherCity);
  prefs.putUInt("page", (uint32_t)homePage);
  prefs.end();
}

void clearConfig() {
  prefs.begin("buddy", false);
  prefs.clear();
  prefs.end();
  wifiSsid = "";
  wifiPass = "";
  localServer = DEFAULT_LOCAL_SERVER;
  weatherCity = "Chittagong";
  homePage = PAGE_DIGITAL;
}

void loadConfig() {
  prefs.begin("buddy", true);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  localServer = prefs.getString("server", DEFAULT_LOCAL_SERVER);
  weatherCity = prefs.getString("city", "Chittagong");
  uint32_t page = prefs.getUInt("page", prefs.getUInt("face", 0));
  prefs.end();
  if (!weatherCity.length()) weatherCity = "Chittagong";
  if (!localServer.startsWith("http")) localServer = DEFAULT_LOCAL_SERVER;
  if (localServer.indexOf(":11434") >= 0) localServer = DEFAULT_LOCAL_SERVER;
  if (localServer.indexOf("192.168.2.105:3000") >= 0) localServer = DEFAULT_LOCAL_SERVER;
  homePage = (HomePage)(page % 6);
}

void drawSetupPage() {
  String page = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>AI Buddy Setup</title><style>";
  page += "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:24px;max-width:560px;background:#10131b;color:#eef2ff}";
  page += "input,select,button{box-sizing:border-box;width:100%;padding:13px;margin:8px 0;border-radius:10px;border:1px solid #374151;background:#171c28;color:#fff;font-size:16px}";
  page += "button{background:#7dd3fc;color:#07111f;font-weight:700}.hint{color:#aab3c5;font-size:14px;line-height:1.45}</style></head><body>";
  page += "<h2>AI Buddy Setup</h2>";
  page += "<p class='hint'>Touch cycles pages: time, weather, system, AI. Button enters AI, starts recording, then sends. Long button opens or exits this setup screen.</p>";
  page += "<form method='POST' action='/save'>";
  page += "<input name='ssid' placeholder='WiFi SSID' value='" + htmlEscape(wifiSsid) + "'>";
  page += "<input name='pass' placeholder='WiFi password' type='password'>";
  page += "<input name='server' placeholder='Local server, e.g. http://192.168.207.192:3000' value='" + htmlEscape(localServer) + "'>";
  page += "<input name='city' placeholder='Weather city, e.g. Chittagong' value='" + htmlEscape(weatherCity) + "'>";
  page += "<select name='face'>";
  const int pageValues[] = {(int)PAGE_DIGITAL, (int)PAGE_TODAY_WEATHER, (int)PAGE_SYSTEM, (int)PAGE_AI};
  const char *pages[] = {"Time", "Weather", "System", "AI visualizer"};
  for (int i = 0; i < 4; i++) {
    page += "<option value='" + String(pageValues[i]) + "'";
    if ((int)homePage == pageValues[i]) page += " selected";
    page += ">" + String(pages[i]) + "</option>";
  }
  page += "</select>";
  page += "<button>Save and reboot</button></form>";
  page += "<p class='hint'>Extender screen starts an AP named AI_Buddy_Extender. True internet repeating still needs NAT/NAPT firmware.</p>";
  page += "<p class='hint'>Device: " + deviceId + "</p></body></html>";
  portal.send(200, "text/html", page);
}

void startSetupPortal() {
  if (setupPortalRunning) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME, AP_PASSWORD);
  portal.on("/", HTTP_GET, drawSetupPage);
  portal.on("/save", HTTP_POST, []() {
    wifiSsid = portal.arg("ssid");
    wifiPass = portal.arg("pass");
    localServer = portal.arg("server");
    weatherCity = portal.arg("city");
    if (!weatherCity.length()) weatherCity = "Chittagong";
    homePage = (HomePage)(portal.arg("face").toInt() % 6);
    saveConfig();
    portal.send(200, "text/html", "<p>Saved. Rebooting...</p>");
    delay(700);
    ESP.restart();
  });
  portal.begin();
  setupPortalRunning = true;
  setMode(MODE_SETUP, "Setup AP active");
}

void stopSetupPortal() {
  if (!setupPortalRunning) return;
  portal.stop();
  WiFi.softAPdisconnect(true);
  setupPortalRunning = false;
}

void startExtenderMode() {
  stopSetupPortal();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("AI_Buddy_Extender", AP_PASSWORD);
  extenderRunning = true;
  setMode(MODE_EXTENDER, "Extender AP active");
}

void stopExtenderMode() {
  if (!extenderRunning) return;
  WiFi.softAPdisconnect(true);
  extenderRunning = false;
}

void returnToClockMode() {
  stopSetupPortal();
  stopExtenderMode();
  if (wifiSsid.length() && WiFi.status() != WL_CONNECTED) connectWiFi(2500);
  localAiArmed = false;
  setMode(MODE_CLOCK, "Clock");
}

bool connectWiFi(uint32_t timeoutMs) {
  if (!wifiSsid.length()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(150);
  }
  return WiFi.status() == WL_CONNECTED;
}

void updateTime() {
  static bool configured = false;
  if (WiFi.status() == WL_CONNECTED && !configured) {
    configured = true;
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov", "bd.pool.ntp.org");
  }
  struct tm t;
  timeReady = getLocalTime(&t, 5);
}

void currentTimeParts(int &hh, int &mm, int &ss, char *label, size_t labelLen) {
  struct tm t;
  if (getLocalTime(&t, 5)) {
    int hour24 = t.tm_hour;
    hh = hour24 % 12;
    if (hh == 0) hh = 12;
    mm = t.tm_min;
    ss = t.tm_sec;
    strftime(label, labelLen, "%I:%M %p", &t);
  } else {
    uint32_t now = millis() / 1000;
    int hour24 = (now / 3600) % 24;
    hh = hour24 % 12;
    if (hh == 0) hh = 12;
    mm = (now / 60) % 60;
    ss = now % 60;
    snprintf(label, labelLen, "UP %02d:%02d", mm, ss);
  }
}

String urlEncode(const String &input) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out += c;
    else if (c == ' ') out += '+';
    else {
      out += '%';
      out += hex[((uint8_t)c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String jsonValueAfter(const String &body, const String &key, int startAt = 0) {
  int keyPos = body.indexOf("\"" + key + "\"", startAt);
  if (keyPos < 0) return "";
  int colon = body.indexOf(':', keyPos);
  if (colon < 0) return "";
  int firstQuote = body.indexOf('"', colon + 1);
  if (firstQuote < 0) return "";
  int secondQuote = body.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return "";
  return body.substring(firstQuote + 1, secondQuote);
}

String shortDayName(const String &isoDate) {
  if (isoDate.length() < 10) return "--";
  int y = isoDate.substring(0, 4).toInt();
  int m = isoDate.substring(5, 7).toInt();
  int d = isoDate.substring(8, 10).toInt();
  if (m < 3) {
    m += 12;
    y--;
  }
  int k = y % 100;
  int j = y / 100;
  int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
  const char *names[] = {"Sat", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri"};
  return String(names[h]);
}

void updateWeather(bool force = false) {
  if (WiFi.status() != WL_CONNECTED || !weatherCity.length()) return;
  if (!force && millis() - lastWeatherMs < 30UL * 60UL * 1000UL) return;
  lastWeatherMs = millis();

  HTTPClient http;
  String url = "http://wttr.in/" + urlEncode(weatherCity) + "?format=j1";
  http.begin(url);
  http.setTimeout(7000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    body.trim();
    String temp = jsonValueAfter(body, "temp_C");
    String feels = jsonValueAfter(body, "FeelsLikeC");
    String humidity = jsonValueAfter(body, "humidity");
    String wind = jsonValueAfter(body, "windspeedKmph");
    int descArea = body.indexOf("\"weatherDesc\"");
    String cond = jsonValueAfter(body, "value", descArea);
    if (temp.length()) weatherTemp = temp;
    if (feels.length()) weatherFeels = feels;
    if (humidity.length()) weatherHumidity = humidity;
    if (wind.length()) weatherWind = wind;
    if (cond.length()) weatherCond = cond;
    if (weatherCond.length() > 18) weatherCond = weatherCond.substring(0, 18);

    int scan = body.indexOf("\"weather\"");
    for (int i = 0; i < 3; i++) {
      int item = body.indexOf("\"date\"", scan);
      if (item < 0) break;
      forecastDay[i] = shortDayName(jsonValueAfter(body, "date", item));
      forecastLow[i] = jsonValueAfter(body, "mintempC", item);
      forecastHigh[i] = jsonValueAfter(body, "maxtempC", item);
      int nextItem = body.indexOf("\"date\"", item + 8);
      int hourly = body.indexOf("\"hourly\"", item);
      int desc = body.indexOf("\"weatherDesc\"", hourly > 0 ? hourly : item);
      if (desc > 0 && (nextItem < 0 || desc < nextItem)) {
        String fc = jsonValueAfter(body, "value", desc);
        if (fc.length()) forecastCond[i] = fc.substring(0, min(12, (int)fc.length()));
      }
      scan = item + 8;
    }
    uiFull = true;
  }
  http.end();
}

void readMicStats() {
  uint32_t sum = 0;
  uint16_t mn = 4095;
  uint16_t mx = 0;
  for (int i = 0; i < 96; i++) {
    uint16_t v = analogRead(MIC_PIN);
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    delayMicroseconds(80);
  }
  micMid = sum / 96;
  uint16_t p2p = mx - mn;
  micLevel = micLevel * 0.88f + p2p * 0.12f;
  if (millis() < 8000 && p2p < 500) noiseFloor = noiseFloor * 0.96f + p2p * 0.04f;
}

uint8_t sampleMic8() {
  int raw = analogRead(MIC_PIN);
  int centered = raw - (int)micMid;
  centered = constrain(centered / 12, -127, 127);
  return (uint8_t)(centered + 128);
}

void startRecording() {
  if (!ensureAudioBuffer()) {
    setMode(MODE_STATUS, "No RAM for recording");
    beep(330, 80);
    return;
  }
  audioLength = 0;
  recordStartedMs = millis();
  lastSoundMs = millis();
  setMode(MODE_RECORDING, "Listening...");
  beep(880, 35);
}

bool continueRecording(bool touchHeld) {
  (void)touchHeld;
  if (!audioBuffer) return false;
  uint32_t nextSampleAt = micros();
  uint32_t sum = 0;
  uint16_t mn = 4095;
  uint16_t mx = 0;
  for (int i = 0; i < 640 && audioLength < MAX_AUDIO_BYTES; i++) {
    while ((int32_t)(micros() - nextSampleAt) < 0) {}
    nextSampleAt += 1000000UL / SAMPLE_RATE;
    int raw = analogRead(MIC_PIN);
    sum += raw;
    if (raw < mn) mn = raw;
    if (raw > mx) mx = raw;
    int centered = raw - (int)micMid;
    centered = constrain(centered / 12, -127, 127);
    audioBuffer[audioLength++] = (uint8_t)(centered + 128);
  }
  micMid = (micMid * 3 + (sum / 640)) / 4;
  uint16_t p2p = mx - mn;
  micLevel = micLevel * 0.75f + p2p * 0.25f;
  uint32_t now = millis();
  if (p2p > noiseFloor + 90) lastSoundMs = now;
  bool longEnough = now - recordStartedMs > MIN_HOLD_RECORD_MS;
  bool quietLongEnough = now - lastSoundMs > SILENCE_STOP_MS;
  return audioLength < MAX_AUDIO_BYTES && !(longEnough && quietLongEnough);
}

String jsonValue(const String &body, const String &key) {
  String needle = "\"" + key + "\"";
  int keyPos = body.indexOf(needle);
  if (keyPos < 0) return "";
  int colon = body.indexOf(':', keyPos + needle.length());
  if (colon < 0) return "";
  int firstQuote = body.indexOf('"', colon + 1);
  if (firstQuote < 0) return "";
  String out;
  bool esc = false;
  for (int i = firstQuote + 1; i < body.length(); i++) {
    char c = body[i];
    if (esc) {
      if (c == 'n') out += ' ';
      else out += c;
      esc = false;
    } else if (c == '\\') {
      esc = true;
    } else if (c == '"') {
      break;
    } else {
      out += c;
    }
  }
  return out;
}

bool waitForStreamByte(WiFiClient *stream, uint8_t &out, uint32_t timeoutMs = 4000) {
  uint32_t start = millis();
  while (!stream->available()) {
    if (millis() - start > timeoutMs) return false;
    delay(1);
  }
  out = stream->read();
  return true;
}

bool readU32LE(WiFiClient *stream, uint32_t &out) {
  uint8_t b[4];
  for (int i = 0; i < 4; i++) {
    if (!waitForStreamByte(stream, b[i])) return false;
  }
  out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return true;
}

bool playServerWav(const String &audioUrl) {
  if (!audioUrl.length()) return false;
  ledcChangeFrequency(AMP_PIN, PWM_CARRIER_RATE, 8);
  ledcWrite(AMP_PIN, 128);
  String url = audioUrl;
  if (url.startsWith("/")) url = localServer + url;
  HTTPClient http;
  http.begin(url);
  http.setTimeout(20000);
  int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t b = 0;
  char id[5] = {0};
  for (int i = 0; i < 4; i++) {
    if (!waitForStreamByte(stream, b)) {
      http.end();
      return false;
    }
    id[i] = (char)b;
  }
  if (String(id) != "RIFF") {
    http.end();
    return false;
  }
  uint32_t skip = 0;
  if (!readU32LE(stream, skip)) {
    http.end();
    return false;
  }
  for (int i = 0; i < 4; i++) {
    if (!waitForStreamByte(stream, b)) {
      http.end();
      return false;
    }
  }

  uint32_t dataLen = 0;
  bool foundData = false;
  while (http.connected()) {
    for (int i = 0; i < 4; i++) {
      if (!waitForStreamByte(stream, b)) {
        http.end();
        return false;
      }
      id[i] = (char)b;
    }
    id[4] = 0;
    if (!readU32LE(stream, dataLen)) {
      http.end();
      return false;
    }
    if (String(id) == "data") {
      foundData = true;
      break;
    }
    for (uint32_t i = 0; i < dataLen; i++) {
      if (!waitForStreamByte(stream, b)) {
        http.end();
        return false;
      }
    }
  }

  if (!foundData) {
    http.end();
    return false;
  }

  setMode(MODE_SENDING, "Speaking...");
  homePage = PAGE_AI;
  renderUi();
  uint32_t nextSampleAt = micros();
  for (uint32_t i = 0; i < dataLen; i++) {
    if (!waitForStreamByte(stream, b, 8000)) break;
    ledcWrite(AMP_PIN, b);
    while ((int32_t)(micros() - nextSampleAt) < 0) {}
    nextSampleAt += 1000000UL / SAMPLE_RATE;
  }
  ledcWrite(AMP_PIN, 0);
  ledcChangeFrequency(AMP_PIN, PWM_CARRIER_RATE, 8);
  http.end();
  return true;
}

bool sendVoiceToLocalServer() {
  if (!audioBuffer || audioLength == 0) {
    setMode(MODE_STATUS, "No recording");
    beep(330, 80);
    releaseAudioBuffer();
    return false;
  }
  if (WiFi.status() != WL_CONNECTED || !localServer.startsWith("http")) {
    setMode(MODE_STATUS, "WiFi/server offline");
    beep(330, 80);
    releaseAudioBuffer();
    return false;
  }

  setMode(MODE_SENDING, "Sending " + String(audioLength / 1000) + "KB...");
  renderUi();
  HTTPClient http;
  String url = localServer + "/api/voice-message";
  http.begin(url);
  http.setTimeout(45000);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Device-Id", deviceId);
  http.addHeader("X-Audio-Format", "u8;rate=8000;manual=1");
  int code = http.POST(audioBuffer, audioLength);
  releaseAudioBuffer();
  String body = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    setMode(MODE_STATUS, "Local AI HTTP " + String(code));
    beep(330, 80);
    return false;
  }
  body.trim();
  String text = jsonValue(body, "text");
  lastTranscript = jsonValue(body, "transcript");
  lastAudioUrl = jsonValue(body, "audio_url");
  if (!text.length()) text = body;
  if (!text.length()) text = "Local AI received voice";
  setMode(MODE_STATUS, "Reply: " + text.substring(0, 52));
  homePage = PAGE_AI;
  if (lastAudioUrl.length()) {
    if (!playServerWav(lastAudioUrl)) {
      setMode(MODE_STATUS, text.substring(0, 48) + " / audio failed");
      beep(330, 80);
    } else {
      setMode(MODE_STATUS, text.substring(0, 60));
    }
  } else {
    beep(990, 45);
  }
  return true;
}

ButtonEvent readButtonEvent() {
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down && !lastButtonDown) {
    lastButtonDown = true;
    buttonLongFired = false;
    buttonDownAt = millis();
  }
  if (down && lastButtonDown && !buttonLongFired && millis() - buttonDownAt > 900) {
    buttonLongFired = true;
    return BUTTON_LONG;
  }
  if (!down && lastButtonDown) {
    uint32_t held = millis() - buttonDownAt;
    lastButtonDown = false;
    if (!buttonLongFired && held > 35) return BUTTON_SHORT;
  }
  return BUTTON_NONE;
}

void cycleHomePage() {
  if (mode != MODE_CLOCK) return;
  if (homePage == PAGE_DIGITAL) homePage = PAGE_TODAY_WEATHER;
  else if (homePage == PAGE_TODAY_WEATHER) homePage = PAGE_SYSTEM;
  else if (homePage == PAGE_SYSTEM) homePage = PAGE_AI;
  else homePage = PAGE_DIGITAL;
  saveConfig();
  lastClockMinuteDrawn = -1;
  lastHomePageDrawn = -1;
  lastSystemDrawSecond = -1;
  uiFull = true;
  beep(720, 25);
}

void jumpToAiPage() {
  stopSetupPortal();
  stopExtenderMode();
  localAiArmed = true;
  homePage = PAGE_AI;
  lastClockMinuteDrawn = -1;
  lastHomePageDrawn = -1;
  lastSystemDrawSecond = -1;
  setMode(MODE_CLOCK, "AI ready");
  beep(880, 35);
}

void exitAiToClock() {
  localAiArmed = false;
  homePage = PAGE_DIGITAL;
  setMode(MODE_CLOCK, "Clock");
  beep(440, 35);
}

void handleShortButtonAction() {
  if (mode == MODE_RECORDING) {
    beep(660, 35);
    sendVoiceToLocalServer();
    return;
  }
  if (mode == MODE_SENDING) return;
  if (mode == MODE_STATUS || mode == MODE_LOCAL_ARMED) {
    startRecording();
    return;
  }
  if (mode == MODE_CLOCK && homePage == PAGE_AI) {
    startRecording();
    return;
  }
  if (mode == MODE_CLOCK) {
    jumpToAiPage();
    return;
  }
}

void handleLongButtonAction() {
  if (mode == MODE_RECORDING) {
    beep(660, 35);
    sendVoiceToLocalServer();
    return;
  }
  if (mode == MODE_SETUP) {
    stopSetupPortal();
    connectWiFi(5000);
    updateTime();
    updateWeather(true);
    homePage = PAGE_DIGITAL;
    setMode(MODE_CLOCK, "Clock");
    beep(440, 35);
    return;
  }
  if (mode == MODE_SENDING) return;
  stopExtenderMode();
  localAiArmed = false;
  startSetupPortal();
  beep(880, 35);
}

void updateStatusLed() {
  uint32_t interval = 1000;
  if (mode == MODE_SETUP || mode == MODE_EXTENDER) {
    digitalWrite(TEST_LED_PIN, HIGH);
    ledState = true;
    return;
  }
  if (mode == MODE_RECORDING || mode == MODE_SENDING) interval = 120;
  else if (localAiArmed) interval = 300;

  if (millis() - lastLedMs >= interval) {
    lastLedMs = millis();
    ledState = !ledState;
    digitalWrite(TEST_LED_PIN, ledState ? HIGH : LOW);
  }
}

void drawSetupQr() {
  uint16_t bg = rgb565(18, 20, 19);
  uint16_t fg = rgb565(8, 10, 14);
  uint16_t qrBg = rgb565(232, 245, 255);
  uint16_t blue = rgb565(75, 161, 255);
  uint16_t muted = rgb565(142, 155, 141);
  drawHudFrame(blue, rgb565(45, 92, 145));
  drawHudGrid(rgb565(22, 44, 70));
  int scale = 4;
  int qrPixels = SETUP_QR_SIZE * scale;
  int x0 = (240 - qrPixels) / 2;
  int y0 = 50;

  drawCenteredText("SETUP", 24, 2, blue, bg);
  gfx->fillRect(x0 - 7, y0 - 7, qrPixels + 14, qrPixels + 14, qrBg);
  for (int y = 0; y < SETUP_QR_SIZE; y++) {
    uint32_t row = pgm_read_dword(&SETUP_QR[y]);
    for (int x = 0; x < SETUP_QR_SIZE; x++) {
      if (row & (1UL << (SETUP_QR_SIZE - 1 - x))) {
        gfx->fillRect(x0 + x * scale, y0 + y * scale, scale, scale, fg);
      }
    }
  }
  gfx->setTextSize(1);
  gfx->setTextColor(muted, bg);
  drawCenteredText("AI_Buddy_Setup", 187, 1, muted, bg);
  drawCenteredText("Password: 12345678", 202, 1, muted, bg);
  drawCenteredText("Open 192.168.4.1", 217, 1, muted, bg);
}

void drawAnalogPage() {
  int hh, mm, ss;
  char label[18];
  currentTimeParts(hh, mm, ss, label, sizeof(label));
  uint16_t bg = rgb565(255, 91, 48);
  uint16_t line = rgb565(8, 10, 14);
  uint16_t muted = rgb565(118, 41, 30);
  uint16_t red = rgb565(255, 232, 214);
  gfx->fillScreen(bg);
  gfx->fillRoundRect(8, 8, 224, 224, 18, bg);
  gfx->drawRoundRect(8, 8, 224, 224, 18, line);
  gfx->drawRoundRect(18, 18, 204, 204, 12, muted);
  gfx->fillRect(116, 14, 8, 2, red);
  gfx->fillRect(116, 224, 8, 2, red);
  for (int i = 0; i < 60; i++) {
    float a = (i * 6 - 90) * DEG_TO_RAD;
    int r1 = i % 5 == 0 ? 82 : 88;
    int r2 = 94;
    uint16_t c = i % 5 == 0 ? line : muted;
    gfx->drawLine(120 + cos(a) * r1, 120 + sin(a) * r1,
                  120 + cos(a) * r2, 120 + sin(a) * r2, c);
  }
  float hourA = (((hh % 12) + mm / 60.0f) * 30 - 90) * DEG_TO_RAD;
  float minA = (mm * 6 - 90) * DEG_TO_RAD;
  gfx->drawLine(120, 120, 120 + cos(hourA) * 46, 120 + sin(hourA) * 46, line);
  gfx->drawLine(120, 120, 120 + cos(minA) * 72, 120 + sin(minA) * 72, line);
  gfx->fillCircle(120, 120, 5, red);
  gfx->setTextColor(line, bg);
  gfx->setTextSize(2);
  gfx->setCursor(56, 202);
  gfx->print(label);
}

void drawDigitalPage() {
  int hh, mm, ss;
  char label[18];
  currentTimeParts(hh, mm, ss, label, sizeof(label));
  uint16_t bg = rgb565(18, 20, 19);
  uint16_t yellow = rgb565(255, 211, 61);
  uint16_t dim = rgb565(45, 39, 18);
  drawHudFrame(yellow, rgb565(96, 78, 24));
  drawHudGrid(rgb565(45, 38, 15));
  char big[8];
  snprintf(big, sizeof(big), "%02d:%02d", hh, mm);
  int scale = 8;
  int dotWidth = 5 * scale * 4 + scale * 2;
  int x = (240 - dotWidth) / 2;
  drawDotNumber(x, 78, scale, big, yellow, dim);
  float a = (ss * 6 - 90) * DEG_TO_RAD;
  int x2 = 120 + cos(a) * 72;
  int y2 = 120 + sin(a) * 72;
  gfx->drawLine(120, 120, x2, y2, yellow);
  gfx->drawLine(121, 120, x2 + 1, y2, yellow);
  gfx->fillCircle(120, 120, 4, yellow);
  drawCenteredText(String(label).substring(6), 154, 1, yellow, bg);
  String footer = weatherCity.substring(0, 12) + "  " + weatherTemp.substring(0, 3) + "C";
  drawCenteredText(footer, 199, 1, rgb565(142, 155, 141), bg, 26);
}

void drawTodayWeatherPage() {
  uint16_t bg = rgb565(18, 20, 19);
  uint16_t green = rgb565(124, 255, 79);
  uint16_t yellow = rgb565(255, 211, 61);
  uint16_t blue = rgb565(75, 161, 255);
  uint16_t red = rgb565(255, 73, 77);
  String cond = weatherCond;
  cond.toLowerCase();
  uint8_t icon = 0;
  uint16_t iconColor = yellow;
  if (cond.indexOf("rain") >= 0 || cond.indexOf("drizzle") >= 0) {
    icon = 2;
    iconColor = blue;
  } else if (cond.indexOf("storm") >= 0 || cond.indexOf("thunder") >= 0) {
    icon = 3;
    iconColor = red;
  } else if (cond.indexOf("mist") >= 0 || cond.indexOf("fog") >= 0 || cond.indexOf("haze") >= 0) {
    icon = 4;
    iconColor = rgb565(21, 239, 216);
  } else if (cond.indexOf("cloud") >= 0 || cond.indexOf("overcast") >= 0) {
    icon = 1;
    iconColor = green;
  }
  drawHudFrame(iconColor, rgb565(58, 82, 58));
  drawHudGrid(rgb565(30, 42, 30));
  drawFlickerWeatherIcon(120, 82, iconColor, icon);
  drawCenteredText(weatherTemp.substring(0, 3) + "C", 139, 4, rgb565(237, 244, 236), bg);
  drawCenteredText(weatherCity.substring(0, 14), 178, 1, rgb565(237, 244, 236), bg);
  String label = weatherCond.length() ? weatherCond : String("Weather");
  drawCenteredText(label, 193, 1, rgb565(142, 155, 141), bg, 24);
  String meta = "Feels " + weatherFeels + "C  Hum " + weatherHumidity + "%";
  drawCenteredText(meta, 213, 1, rgb565(142, 155, 141), bg, 28);
}

void drawForecastPage() {
  uint16_t yellow = rgb565(255, 214, 10);
  uint16_t black = rgb565(2, 3, 5);
  uint16_t dark = rgb565(12, 13, 18);
  uint16_t text = rgb565(236, 239, 244);
  gfx->fillScreen(black);
  gfx->fillRoundRect(8, 8, 100, 224, 18, yellow);
  gfx->drawRoundRect(8, 8, 100, 224, 18, rgb565(70, 58, 0));
  gfx->setTextColor(black, yellow);
  gfx->setTextSize(5);
  gfx->setCursor(24, 70);
  gfx->print(weatherTemp);
  gfx->setTextSize(2);
  gfx->print((char)247);
  gfx->setTextSize(1);
  gfx->setCursor(18, 154);
  gfx->print(weatherCity.substring(0, 12));
  gfx->setTextSize(1);
  gfx->setCursor(20, 184);
  gfx->print(weatherCond.substring(0, 12));

  gfx->fillRoundRect(114, 8, 118, 224, 18, dark);
  gfx->drawRoundRect(114, 8, 118, 224, 18, rgb565(42, 45, 55));
  gfx->setTextColor(text, dark);
  gfx->setTextSize(1);
  for (int i = 0; i < 3; i++) {
    int y = 28 + i * 64;
    gfx->setCursor(124, y);
    gfx->print(forecastDay[i].substring(0, 3));
    gfx->setCursor(162, y);
    gfx->print(forecastLow[i].substring(0, 2));
    gfx->print("-");
    gfx->print(forecastHigh[i].substring(0, 2));
    gfx->setCursor(124, y + 28);
    gfx->print(forecastCond[i].substring(0, 14));
  }
}

void drawSystemPage() {
  uint16_t bg = rgb565(18, 20, 19);
  uint16_t text = rgb565(237, 244, 236);
  uint16_t muted = rgb565(142, 155, 141);
  uint16_t green = rgb565(124, 255, 79);
  drawHudFrame(green, rgb565(58, 82, 58));
  drawHudGrid(rgb565(28, 42, 28));
  drawCenteredText("SYSTEM", 25, 2, green, bg);
  String serverShort = localServer;
  serverShort.replace("http://", "");
  String rows[6] = {
    "WiFi " + String(WiFi.status() == WL_CONNECTED ? WiFi.SSID().substring(0, 14) : String("offline")),
    "IP " + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("--")),
    "Srv " + serverShort.substring(0, 18),
    "Mic " + String((int)micLevel),
    "RAM " + String(ESP.getFreeHeap() / 1024) + "KB",
    "Flash " + String(ESP.getFreeSketchSpace() / 1024) + "KB"
  };
  gfx->setTextSize(1);
  for (int i = 0; i < 6; i++) {
    int y = 62 + i * 22;
    gfx->drawFastHLine(46, y + 13, 148, rgb565(34, 40, 36));
    drawCenteredText(rows[i], y, 1, i == 0 ? text : muted, bg, 25);
  }
  uint16_t statusColor = WiFi.status() == WL_CONNECTED ? green : rgb565(75, 161, 255);
  gfx->fillRoundRect(91, 201, WiFi.status() == WL_CONNECTED ? 58 : 36, 10, 4, statusColor);
}

void drawExtenderScreen() {
  uint16_t bg = rgb565(8, 13, 24);
  uint16_t accent = rgb565(74, 222, 128);
  gfx->fillScreen(bg);
  gfx->setTextColor(accent, bg);
  gfx->setTextSize(2);
  gfx->setCursor(30, 46);
  gfx->print("EXTENDER");
  gfx->setTextSize(1);
  gfx->setTextColor(rgb565(214, 226, 240), bg);
  gfx->setCursor(24, 92);
  gfx->print("AP: AI_Buddy_Extender");
  gfx->setCursor(24, 110);
  gfx->print("Password: 12345678");
  gfx->setCursor(24, 148);
  gfx->print("NAT repeating not active yet");
  gfx->setCursor(24, 166);
  gfx->print("Press button for clock");
}

void drawVisualizerScreen() {
  uint16_t bg = rgb565(18, 20, 19);
  uint16_t text = rgb565(237, 244, 236);
  uint16_t muted = rgb565(142, 155, 141);
  uint16_t green = rgb565(124, 255, 79);
  uint16_t red = rgb565(255, 73, 77);
  bool listening = mode == MODE_RECORDING;
  bool waitingAi = mode == MODE_SENDING && statusText.startsWith("Sending");
  bool speaking = (mode == MODE_SENDING && !waitingAi) || (mode == MODE_STATUS && statusText.startsWith("Reply"));
  bool aiPageIdle = mode == MODE_CLOCK && homePage == PAGE_AI;
  uint16_t accent = (waitingAi || speaking) ? red : green;
  uint16_t dim = (waitingAi || speaking) ? rgb565(54, 24, 26) : rgb565(30, 48, 28);
  uint32_t now = millis();
  if (uiFull) {
    drawHudFrame(accent, dim);
    drawHudGrid(dim);
    for (int i = 0; i < 5; i++) {
      aiPrevDotX[i] = -1;
      aiPrevDotY[i] = -1;
      aiPrevDotR[i] = 0;
    }
    aiPrevWaitX = -1;
  }

  if (waitingAi) {
    for (int i = 0; i < 5; i++) {
      if (aiPrevDotX[i] >= 0) {
        gfx->fillCircle(aiPrevDotX[i], aiPrevDotY[i], aiPrevDotR[i] + 2, bg);
        aiPrevDotX[i] = -1;
      }
    }
    if (aiPrevWaitX >= 0) {
      gfx->fillRoundRect(aiPrevWaitX, aiPrevWaitY, aiPrevWaitW, aiPrevWaitH, 8, bg);
      drawHudGridArea(dim, aiPrevWaitX, aiPrevWaitY, aiPrevWaitX + aiPrevWaitW, aiPrevWaitY + aiPrevWaitH);
    }
    int phase = (now / 80) % 32;
    float angle = phase * TWO_PI / 32.0f;
    int half = 27 + (int)(sin(angle) * 9.0f);
    float ca = cos(angle);
    float sa = sin(angle);
    int pts[4][2] = {{-half, -half}, {half, -half}, {half, half}, {-half, half}};
    int sx[4], sy[4];
    for (int i = 0; i < 4; i++) {
      sx[i] = 120 + (int)(pts[i][0] * ca - pts[i][1] * sa);
      sy[i] = 112 + (int)(pts[i][0] * sa + pts[i][1] * ca);
    }
    for (int i = 0; i < 4; i++) {
      int j = (i + 1) % 4;
      drawThickLine(sx[i], sy[i], sx[j], sy[j], 8, red);
    }
    gfx->fillRoundRect(120 - half / 2, 112 - half / 2, half, half, 8, red);
    int ext = (int)(half * 1.45f) + 10;
    aiPrevWaitX = 120 - ext;
    aiPrevWaitY = 112 - ext;
    aiPrevWaitW = ext * 2;
    aiPrevWaitH = ext * 2;
  } else {
    if (aiPrevWaitX >= 0) {
      gfx->fillRoundRect(aiPrevWaitX, aiPrevWaitY, aiPrevWaitW, aiPrevWaitH, 8, bg);
      aiPrevWaitX = -1;
    }
    for (int i = 0; i < 5; i++) {
      if (aiPrevDotX[i] >= 0) gfx->fillCircle(aiPrevDotX[i], aiPrevDotY[i], aiPrevDotR[i] + 2, bg);
    }
    drawDotPill(42, 78, 156, 78, accent, dim);
    int baseY = 117;
    int levelBoost = listening ? constrain((int)(micLevel / 16), 0, 18) : 0;
    for (int i = 0; i < 5; i++) {
      float p = (now / (speaking ? 120.0f : 155.0f)) + i * 0.85f;
      int bounce = (int)(sin(p) * (listening ? 13 + levelBoost / 2 : (speaking ? 11 : 4)));
      int r = (speaking || listening) ? 11 + ((int)(now / 110 + i) % 3) : 10;
      int x = 74 + i * 23;
      int y = baseY - bounce;
      gfx->fillCircle(x, y, r, accent);
      aiPrevDotX[i] = x;
      aiPrevDotY[i] = y;
      aiPrevDotR[i] = r;
    }
  }

  gfx->fillRect(18, 191, 204, 39, bg);
  if (listening) drawCenteredText("LISTENING", 196, 1, accent, bg);
  else if (waitingAi) drawCenteredText("WAITING FOR AI", 196, 1, accent, bg);
  else if (speaking) drawCenteredText("SPEAKING", 196, 1, accent, bg);
  else if (aiPageIdle) drawCenteredText("AI READY", 196, 1, accent, bg);
  else drawCenteredText("IDLE", 196, 1, accent, bg);

  String line = statusText;
  if (line.startsWith("Reply: ")) line = line.substring(7);
  drawCenteredText(line, 215, 1, speaking ? text : muted, bg, 30);
}

void renderUi() {
  if (mode == MODE_SETUP) {
    if (uiFull) drawSetupQr();
    uiFull = false;
    return;
  }
  if (mode == MODE_EXTENDER) {
    if (uiFull) drawExtenderScreen();
    uiFull = false;
    return;
  }
  if (mode == MODE_CLOCK) {
    int hh, mm, ss;
    char label[18];
    currentTimeParts(hh, mm, ss, label, sizeof(label));
    bool needsMinute = homePage == PAGE_DIGITAL || homePage == PAGE_ANALOG;
    bool needsSlowSystem = homePage == PAGE_SYSTEM;
    bool needsLiveAi = homePage == PAGE_AI;
    int slowBucket = ss / 5;
    if (!uiFull &&
        lastHomePageDrawn == (int)homePage &&
        !needsLiveAi &&
        (!needsMinute || lastClockMinuteDrawn == mm) &&
        (!needsSlowSystem || lastSystemDrawSecond == slowBucket)) {
      return;
    }
    lastClockMinuteDrawn = mm;
    lastHomePageDrawn = (int)homePage;
    lastSystemDrawSecond = slowBucket;
    if (homePage == PAGE_DIGITAL || homePage == PAGE_ANALOG) drawDigitalPage();
    else if (homePage == PAGE_TODAY_WEATHER) drawTodayWeatherPage();
    else if (homePage == PAGE_FORECAST) drawTodayWeatherPage();
    else if (homePage == PAGE_SYSTEM) drawSystemPage();
    else drawVisualizerScreen();
    uiFull = false;
    return;
  }
  drawVisualizerScreen();
  uiFull = false;
}

void handleTouch() {
  bool raw = digitalRead(TOUCH_PIN) == HIGH;
  bool down = raw != touchIdleHigh;
  uint32_t now = millis();
  if (down && !lastTouchDown) {
    lastTouchDown = true;
    touchStartedMs = now;
    if (mode == MODE_CLOCK) cycleHomePage();
  }
  if (!down && lastTouchDown) {
    lastTouchDown = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  deviceId = "esp32s3-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TEST_LED_PIN, OUTPUT);
  digitalWrite(TEST_LED_PIN, HIGH);
  delay(180);
  digitalWrite(TEST_LED_PIN, LOW);
  loadConfig();
  if (digitalRead(BUTTON_PIN) == LOW) clearConfig();
  delay(60);
  touchIdleHigh = digitalRead(TOUCH_PIN) == HIGH;

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  ledcAttach(AMP_PIN, PWM_CARRIER_RATE, 8);
  ledcWrite(AMP_PIN, 0);

  gfx->begin();
  gfx->setRotation(2);
  gfx->fillScreen(0);
  setMode(MODE_CLOCK, "Clock");

  connectWiFi(5000);
  updateTime();
  updateWeather(true);
  beep(880, 40);
}

void loop() {
  uint32_t now = millis();
  if (setupPortalRunning) portal.handleClient();
  updateStatusLed();

  if (now - lastClockSyncMs > 1000) {
    lastClockSyncMs = now;
    updateTime();
    updateWeather(false);
  }

  readMicStats();

  ButtonEvent event = readButtonEvent();
  if (event == BUTTON_SHORT) {
    handleShortButtonAction();
  } else if (event == BUTTON_LONG) {
    handleLongButtonAction();
  }

  handleTouch();

  if (mode == MODE_RECORDING) {
    bool keepGoing = continueRecording((digitalRead(TOUCH_PIN) == HIGH) != touchIdleHigh);
    if (!keepGoing) {
      beep(660, 35);
      sendVoiceToLocalServer();
    }
  }

  if (mode != MODE_CLOCK && mode != MODE_SETUP && mode != MODE_EXTENDER && mode != MODE_RECORDING && now - lastActionMs > IDLE_CLOCK_MS) {
    homePage = PAGE_AI;
    setMode(MODE_CLOCK, "AI ready");
  }

  uint32_t frameMs = (mode == MODE_RECORDING) ? 300 : ((mode == MODE_CLOCK && homePage != PAGE_AI) ? 250 : 120);
  if (now - lastDrawMs > frameMs || uiFull) {
    lastDrawMs = now;
    renderUi();
  }

  if (now - lastSerialMs > 1500) {
    lastSerialMs = now;
    Serial.printf("mode=%d wifi=%s armed=%d mic=%.1f server=%s\n",
      mode,
      WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "none",
      localAiArmed,
      micLevel,
      localServer.c_str());
  }
}
