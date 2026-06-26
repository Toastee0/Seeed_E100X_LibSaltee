// PCMonitor — show a Windows PC's CPU/GPU temps on the e-paper, parsed straight on the device.
//
// Run LibreHardwareMonitor on the PC with its Remote Web Server enabled (see docs/PC_MONITOR.md).
// It serves a JSON sensor tree at http://<pc-ip>:8085/data.json. This sketch fetches that, pulls
// out the CPU Package + GPU Core temperatures (and loads), and shows them — refreshing only the
// numbers that changed via the panel's fast partial refresh. No JSON library: we scan the text
// for the named sensors, which keeps it dependency-free and robust to LibreHardwareMonitor's
// exact tree shape.
//
// Set WIFI_* and LHM_URL below. Needs Adafruit GFX. Board: XIAO_ESP32S3, OPI PSRAM, CDC on boot.
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
#include <ReTerminalPeripherals.h>
using namespace ReTerminal;

const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const char* LHM_URL   = "http://192.168.1.50:8085/data.json";   // <pc-ip>:8085
const uint32_t EVERY_MS = 5000;

Mono epd;
Peripherals io;
GFXcanvas1 canvas(PANEL_W, PANEL_H);
bool firstDraw = true;

// Find the sensor named `name` whose value is a TEMPERATURE (ends in C). LibreHardwareMonitor
// reuses names across categories (e.g. "GPU Core" is a clock, a load AND a temperature), so we
// skip matches whose value isn't in °C. Returns NAN if not found.
static float lhmTemp(const String& j, const char* name) {
  String key = String("\"Text\":\"") + name + "\"";
  int from = 0;
  while (true) {
    int t = j.indexOf(key, from);
    if (t < 0) return NAN;
    int v = j.indexOf("\"Value\":\"", t);
    if (v < 0) return NAN;
    v += 9; int e = j.indexOf('"', v);
    String val = j.substring(v, e);             // e.g. "45.0 °C"
    if (val.indexOf('C') >= 0) return val.toFloat();
    from = t + key.length();
  }
}

static void panel(int x, const char* label, float temp) {
  canvas.drawRect(x, 92, 366, 320, 1);                 // bordered card
  canvas.fillRect(x, 92, 366, 46, 1);                  // header strip (inverted)
  canvas.setTextColor(0); canvas.setTextSize(4); canvas.setCursor(x + 16, 102); canvas.print(label);
  canvas.setTextColor(1); canvas.setTextSize(11); canvas.setCursor(x + 40, 210);
  if (isnan(temp)) canvas.print("--"); else { canvas.print(temp, 0); }
  if (!isnan(temp)) { canvas.setTextSize(4); canvas.setCursor(x + 250, 230); canvas.print("\xF8""C"); }  // °C (GFX font)
}

static void render(float cpu, float gpu, bool ok) {
  canvas.fillScreen(0); canvas.setTextColor(1);
  canvas.setTextSize(4); canvas.setCursor(24, 20); canvas.print("PC monitor");
  canvas.drawFastHLine(24, 74, PANEL_W - 48, 1);
  if (!ok) { canvas.setTextSize(3); canvas.setCursor(24, 140); canvas.print("Can't reach PC."); }
  else { panel(24, "CPU", cpu); panel(410, "GPU", gpu); }
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
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); }
}

void loop() {
  float cpu = NAN, gpu = NAN; bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http; http.setTimeout(6000);
    if (http.begin(LHM_URL) && http.GET() == 200) {
      String j = http.getString();
      cpu = lhmTemp(j, "CPU Package");
      if (isnan(cpu)) cpu = lhmTemp(j, "Core (Tctl/Tdie)");   // AMD names it differently
      gpu = lhmTemp(j, "GPU Core");
      ok = true;
    }
    http.end();
  }
  render(cpu, gpu, ok);
  delay(EVERY_MS);
}
