// PCMonitorConfig — like PCMonitor, but the WHOLE layout comes from config.h, which you generate
// with a browser GUI (extras/setup.html) — no editing C by hand.
//
// Workflow: run LibreHardwareMonitor on the PC with its web server on (docs/PC_MONITOR.md) →
// open extras/setup.html, paste your http://<pc-ip>:8085/data.json, tick + place the sensors you
// want → download config.h into this folder → compile → flash. To change the screen later, just
// re-generate config.h and recompile; the firmware code never changes.
//
// Needs Adafruit GFX. Board: XIAO_ESP32S3, OPI PSRAM, USB CDC On Boot = Enabled.
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
#include <ReTerminalPeripherals.h>
#include "config.h"
using namespace ReTerminal;

Mono epd;
Peripherals io;
GFXcanvas1 canvas(PANEL_W, PANEL_H);
bool firstDraw = true;

// Value (as a String, number only) of the sensor named `name` whose reading carries `unit`'s
// identifying character — LibreHardwareMonitor reuses names across categories (clock/load/temp),
// so the unit hint disambiguates. "" if not found.
static String findValue(const String& j, const char* name, const char* unit) {
  String key = String("\"Text\":\"") + name + "\"";
  char u = unit[0] ? unit[0] : 'C';
  int from = 0;
  while (true) {
    int t = j.indexOf(key, from);
    if (t < 0) return "";
    int v = j.indexOf("\"Value\":\"", t);
    if (v < 0) return "";
    v += 9; int e = j.indexOf('"', v);
    String val = j.substring(v, e);                 // e.g. "45.0 °C" / "36 %" / "1234 MHz"
    if (val.indexOf(u) >= 0) {                       // right category
      int sp = val.indexOf(' ');                     // number is the part before the space
      return sp > 0 ? val.substring(0, sp) : val;
    }
    from = t + key.length();
  }
}

static void render(const String& j, bool ok) {
  canvas.fillScreen(0); canvas.setTextColor(1);
  canvas.setTextSize(3); canvas.setCursor(24, 16); canvas.print("PC monitor");
  if (!ok) { canvas.setTextSize(3); canvas.setCursor(24, 110); canvas.print("Can't reach PC."); }
  else for (int i = 0; i < CFG_ITEM_COUNT; i++) {
    const CfgItem& it = CFG_ITEMS[i];
    canvas.setTextSize(2); canvas.setCursor(it.x, it.y - 22); canvas.print(it.label);
    String val = findValue(j, it.sensor, it.unit);
    canvas.setTextSize(it.size); canvas.setCursor(it.x, it.y);
    canvas.print(val.length() ? val + it.unit : "--");
  }
  uint8_t* fb = epd.buffer();
  for (int y = 0; y < PANEL_H; y++)
    for (int x = 0; x < PANEL_W; x++)
      fb[y * PANEL_W + x] = canvas.getPixel(x, y) ? 0 : 3;
  if (firstDraw) { epd.displayFull(); firstDraw = false; }
  else epd.refreshChanged();
}

void setup() {
  Serial.begin(115200);
  if (!epd.begin()) { while (true) delay(1000); }
  io.begin();
  WiFi.mode(WIFI_STA); WiFi.begin(CFG_WIFI_SSID, CFG_WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); }
}

void loop() {
  String j; bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http; http.setTimeout(6000);
    if (http.begin(CFG_SOURCE_URL) && http.GET() == 200) { j = http.getString(); ok = true; }
    http.end();
  }
  render(j, ok);
  delay((uint32_t)CFG_REFRESH_S * 1000);
}
