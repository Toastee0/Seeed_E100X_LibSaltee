// Dashboard — a config-driven WIDGET engine built around the panel's fast partial refresh.
//
// The layout lives entirely in config.h (generate it with extras/dashboard.html — no C editing).
// Each widget owns a bounding box and a `fast` flag: when a fast widget's value changes, only its
// box is partial-refreshed (~0.4 s); non-fast widgets ride the periodic full refresh. Six object
// types (box, bar, value, text line, text block, QR) and a handful of source "schemes" — and the
// schemes are the extension point: drop a new `if (scheme == "...")` into resolve() and you've
// added a data source. The foundation is small on purpose; build your cathedral on top.
//
// Needs Adafruit GFX. Board: XIAO_ESP32S3, OPI PSRAM, USB CDC On Boot = Enabled.
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
#include <ReTerminalQR.h>
#include <ReTerminalPeripherals.h>
#include "config.h"
using namespace ReTerminal;

Mono epd;
Peripherals io;
GFXcanvas1 canvas(PANEL_W, PANEL_H);
String lastVal[CFG_WIDGET_COUNT];
String dataJson;                                   // primary CFG_DATA_URL body, fetched once/tick
uint32_t tick = 0;

// ---- find a value in a LibreHardwareMonitor-style tree by sensor name (unit char disambiguates) ----
static String lhmFind(const String& j, String spec) {
  char unit = 'C'; int colon = spec.indexOf(':');
  if (colon >= 0) { unit = spec.charAt(colon + 1); spec = spec.substring(0, colon); }
  String key = "\"Text\":\"" + spec + "\"";
  int from = 0;
  while (true) {
    int t = j.indexOf(key, from); if (t < 0) return "";
    int v = j.indexOf("\"Value\":\"", t); if (v < 0) return "";
    v += 9; int e = j.indexOf('"', v); String val = j.substring(v, e);
    if (val.indexOf(unit) >= 0) { int sp = val.indexOf(' '); return sp > 0 ? val.substring(0, sp) : val; }
    from = t + key.length();
  }
}
// ---- find a scalar field "key": <value> in any JSON (first match) ----
static String jsonFind(const String& j, const String& key) {
  int k = j.indexOf("\"" + key + "\""); if (k < 0) return "";
  int c = j.indexOf(':', k); if (c < 0) return "";
  int s = c + 1; while (s < (int)j.length() && (j[s] == ' ' || j[s] == '"')) s++;
  int e = s; while (e < (int)j.length() && j[e] != ',' && j[e] != '}' && j[e] != '"') e++;
  return j.substring(s, e);
}

// ---- resolve a widget's "scheme:arg" source to its current string value (the extension point) ----
static String resolve(const char* src) {
  String s = src; int c = s.indexOf(':');
  String scheme = c >= 0 ? s.substring(0, c) : s, arg = c >= 0 ? s.substring(c + 1) : "";
  if (scheme == "static") return arg;
  if (scheme == "lhm")    return lhmFind(dataJson, arg);
  if (scheme == "json")   return jsonFind(dataJson, arg);
  if (scheme == "batt")   return String(io.batteryPercent());
  if (scheme == "temp") { float t, h; return io.readSHT4x(t, h) ? String(t, 1) : "--"; }
  if (scheme == "hum")  { float t, h; return io.readSHT4x(t, h) ? String((int)h) : "--"; }
  if (scheme == "clock") { struct tm tm; if (getLocalTime(&tm, 50)) { char b[40];
      strftime(b, sizeof b, arg.length() ? arg.c_str() : "%H:%M", &tm); return b; } return "--:--"; }
  return arg;
}

static void drawWidget(const Widget& w, const String& val) {
  switch (w.type) {
    case W_BOX: canvas.drawRect(w.x, w.y, w.w, w.h, 1); break;
    case W_BAR: {
      float lo = 0, hi = 100; int colon = String(w.fmt).indexOf(':');
      if (colon >= 0) { lo = atof(w.fmt); hi = atof(w.fmt + colon + 1); }
      float f = (hi > lo) ? (val.toFloat() - lo) / (hi - lo) : 0; f = f < 0 ? 0 : f > 1 ? 1 : f;
      canvas.drawRect(w.x, w.y, w.w, w.h, 1);
      canvas.fillRect(w.x + 2, w.y + 2, (int)((w.w - 4) * f), w.h - 4, 1);
      break;
    }
    case W_VALUE: {
      char buf[48];
      if (strchr(w.fmt, '%')) snprintf(buf, sizeof buf, w.fmt, val.toFloat());
      else snprintf(buf, sizeof buf, "%s%s", val.c_str(), w.fmt);
      canvas.setTextSize(w.size ? w.size : 3); canvas.setCursor(w.x, w.y); canvas.print(buf);
      break;
    }
    case W_TEXT:
      canvas.setTextSize(w.size ? w.size : 2); canvas.setCursor(w.x, w.y);
      canvas.print(val + w.fmt);
      break;
    case W_BLOCK: {                                 // word-wrap into the box (~ w / (6*size) chars/line)
      int sz = w.size ? w.size : 3; int perLine = w.w / (6 * sz); if (perLine < 1) perLine = 1;
      canvas.setTextSize(sz); String t = val; int line = 0;
      while (t.length()) {
        int cut = t.length() <= perLine ? t.length() : t.lastIndexOf(' ', perLine);
        if (cut <= 0) cut = min((int)t.length(), perLine);
        canvas.setCursor(w.x, w.y + line * (8 * sz + 4)); canvas.print(t.substring(0, cut));
        t = t.substring(cut); t.trim(); line++;
        if (w.y + line * (8 * sz + 4) > w.y + w.h) break;
      }
      break;
    }
    case W_QR: {
      uint8_t* fb = epd.buffer();                   // QR writes straight to the framebuffer
      ReTerminal::drawQR(fb, epd.width(), val.c_str(), w.x, w.y, w.w, w.h);
      break;
    }
  }
}

static void renderAll(String* curVal) {
  canvas.fillScreen(0); canvas.setTextColor(1);
  for (int i = 0; i < CFG_WIDGET_COUNT; i++) { curVal[i] = resolve(CFG_WIDGETS[i].source);
    if (CFG_WIDGETS[i].type != W_QR) drawWidget(CFG_WIDGETS[i], curVal[i]); }
  uint8_t* fb = epd.buffer();                        // 1-bit canvas -> gray buffer
  for (int y = 0; y < PANEL_H; y++)
    for (int x = 0; x < PANEL_W; x++)
      fb[y * PANEL_W + x] = canvas.getPixel(x, y) ? 0 : 3;
  for (int i = 0; i < CFG_WIDGET_COUNT; i++)         // QR widgets draw onto fb after the copy
    if (CFG_WIDGETS[i].type == W_QR) drawWidget(CFG_WIDGETS[i], curVal[i]);
}

void setup() {
  Serial.begin(115200);
  if (!epd.begin()) { while (true) delay(1000); }
  io.begin();
  WiFi.mode(WIFI_STA); WiFi.begin(CFG_WIFI_SSID, CFG_WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); }
  configTzTime("UTC0", "pool.ntp.org");              // set your own POSIX TZ for local clock widgets
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && strlen(CFG_DATA_URL)) {
    HTTPClient http; http.setTimeout(6000);
    if (http.begin(CFG_DATA_URL) && http.GET() == 200) dataJson = http.getString();
    http.end();
  }
  String cur[CFG_WIDGET_COUNT];
  renderAll(cur);
  bool fullDue = (tick == 0) || (tick % CFG_FULL_EVERY == 0);
  if (fullDue) epd.displayFull();
  else for (int i = 0; i < CFG_WIDGET_COUNT; i++)
    if (CFG_WIDGETS[i].fast && cur[i] != lastVal[i])
      epd.partial(CFG_WIDGETS[i].x, CFG_WIDGETS[i].y, CFG_WIDGETS[i].w, CFG_WIDGETS[i].h);
  for (int i = 0; i < CFG_WIDGET_COUNT; i++) lastVal[i] = cur[i];
  tick++;
  delay((uint32_t)CFG_REFRESH_S * 1000);
}
