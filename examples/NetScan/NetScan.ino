// NetScan — a local-subnet "who's alive" map on e-paper, scanned from the device itself.
//
// The reTerminal pings every host in its own /24 (ICMP, via the ESP32 lwIP ping API — no extra
// library) and shows a grid of .1–.254: a filled cell = host replied. A full sweep takes ~20 s
// at the default timeout; results refresh every few minutes, and only the cells that changed
// are partial-refreshed. Battery % (from the on-board divider) rides in the header.
//
// Pinging also populates the ARP cache, so the hosts that answer now have known MACs if you
// want to extend this. Fill in WIFI_SSID/WIFI_PASS. Needs Adafruit GFX. Board: XIAO_ESP32S3,
// OPI PSRAM, USB CDC On Boot = Enabled.
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include "ping/ping_sock.h"
#include "lwip/ip_addr.h"
#include <ReTerminalMono.h>
#include <ReTerminalPeripherals.h>
using namespace ReTerminal;

const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const uint32_t SCAN_EVERY_MS  = 300000;   // re-sweep every 5 min
const uint32_t PING_TIMEOUT_MS = 80;       // per-host wait (dead hosts cost this much)

Mono epd;
Peripherals io;
GFXcanvas1 canvas(PANEL_W, PANEL_H);
bool alive[256];
bool firstDraw = true;

// ---- one synchronous ICMP ping via the lwIP ping_sock session API ----
static volatile bool s_alive, s_done;
static void onPingOk(esp_ping_handle_t, void*)  { s_alive = true; }
static void onPingEnd(esp_ping_handle_t, void*) { s_done = true; }

static bool pingHost(IPAddress ip) {
  ip_addr_t target; memset(&target, 0, sizeof(target));
  target.type = IPADDR_TYPE_V4;
  target.u_addr.ip4.addr = (uint32_t)ip;                 // Arduino IPAddress -> lwIP net-order u32
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target; cfg.count = 1; cfg.timeout_ms = PING_TIMEOUT_MS; cfg.interval_ms = 0;
  esp_ping_callbacks_t cb = { nullptr, onPingOk, nullptr, onPingEnd };
  s_alive = false; s_done = false;
  esp_ping_handle_t h;
  if (esp_ping_new_session(&cfg, &cb, &h) != ESP_OK) return false;
  esp_ping_start(h);
  uint32_t t0 = millis();
  while (!s_done && millis() - t0 < PING_TIMEOUT_MS + 600) delay(2);
  esp_ping_delete_session(h);
  return s_alive;
}

static int sweep() {
  IPAddress me = WiFi.localIP();
  int n = 0;
  for (int host = 1; host <= 254; host++) {
    IPAddress ip(me[0], me[1], me[2], host);
    alive[host] = (ip == me) ? true : pingHost(ip);       // count ourselves alive without pinging
    if (alive[host]) n++;
  }
  return n;
}

static void render(int count) {
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  IPAddress me = WiFi.localIP();
  canvas.setTextSize(3); canvas.setCursor(14, 14);
  canvas.printf("SUBNET %d.%d.%d.x", me[0], me[1], me[2]);
  canvas.setTextSize(2); canvas.setCursor(560, 8);
  canvas.printf("%d up", count);
  canvas.setCursor(560, 30); canvas.printf("batt %d%%", io.batteryPercent());
  canvas.drawFastHLine(14, 58, PANEL_W - 28, 1);
  // grid of .1..254 — 18 cols x 15 rows, filled = alive
  const int cols = 18, x0 = 14, y0 = 66, cw = (PANEL_W - 28) / cols, ch = 26;
  for (int host = 1; host <= 254; host++) {
    int i = host - 1, cx = x0 + (i % cols) * cw, cy = y0 + (i / cols) * ch;
    canvas.drawRect(cx, cy, cw - 2, ch - 2, 1);
    if (alive[host]) canvas.fillRect(cx + 2, cy + 2, cw - 6, ch - 6, 1);
    else { canvas.setTextSize(1); canvas.setCursor(cx + 4, cy + 8); canvas.print(host); }
  }
  uint8_t* fb = epd.buffer();
  for (int y = 0; y < PANEL_H; y++)
    for (int x = 0; x < PANEL_W; x++)
      fb[y * PANEL_W + x] = canvas.getPixel(x, y) ? 0 : 3;
}

void setup() {
  Serial.begin(115200); delay(200);
  io.begin();
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); while (true) delay(1000); }
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nwifi up" : "\nwifi FAILED");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { delay(1000); return; }
  uint32_t t0 = millis();
  int count = sweep();
  Serial.printf("swept in %lu ms, %d alive\n", millis() - t0, count);
  render(count);
  if (firstDraw) { epd.displayFull(); firstDraw = false; }  // crisp base
  else epd.refreshChanged();                                 // partial only the changed cells
  delay(SCAN_EVERY_MS);
}
