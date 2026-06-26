// NetScan — a "seen any sign of life lately" map of the local /24, scanned from the device itself.
//
// It sweeps its own subnet and shows a grid of .1–.254: a cell with its octet = that host has been
// seen within the last 24 h; a solid black block = not seen. It's deliberately NOT an instantaneous
// "who's online right now" view — a host that sleeps or only checks in occasionally stays marked
// until it's been quiet for the whole window, which is what you actually want on a wall display.
//
// Detection uses both ICMP and ARP: each host gets a ping (1 s timeout so slow responders aren't
// missed), and because a host can ignore ICMP yet still answer the ARP the ping triggers, the ARP
// table is also checked (etharp_find_addr — stable entries only, so no false positives). Both use
// only the ESP32 core (lwIP). A full sweep takes a few minutes; the panel auto-full-refreshes to
// clear ghosting. Battery % rides in the header.
//
// Fill in WIFI_SSID/WIFI_PASS. Needs Adafruit GFX. Board: XIAO_ESP32S3, OPI PSRAM, CDC On Boot.
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include "ping/ping_sock.h"
#include "lwip/ip_addr.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"     // LOCK_TCPIP_CORE — raw lwIP calls must hold the core lock off-thread
#include <ReTerminalMono.h>
#include <ReTerminalPeripherals.h>
using namespace ReTerminal;

const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const uint32_t SCAN_EVERY_MS  = 300000;   // re-sweep every 5 min
const uint32_t PING_TIMEOUT_MS = 1000;     // generous per-host wait so slow responders aren't missed.
                                           // Alive hosts answer fast; only dead hosts cost the full
                                           // second, so a full /24 sweep takes a few minutes.
// This is a "seen any sign of life lately" map, not an instantaneous one — a host that sleeps or
// only checks in occasionally stays marked until it's been quiet for the whole window.
const uint32_t SEEN_WINDOW_MS = 24UL * 60 * 60 * 1000;   // 24 h  (in-RAM; resets on power loss)
const int PROBE_ATTEMPTS = 2;             // retry each host this many times — catches dropped packets
                                          // on a lossy WiFi link (a /24 of dead hosts costs N seconds)

Mono epd;
Peripherals io;
GFXcanvas1 canvas(PANEL_W, PANEL_H);
uint32_t lastSeen[256] = {0};   // millis() of the last sighting per octet (0 = never seen)
bool firstDraw = true;

// Has this octet shown a sign of life within the window?
static bool seen(int host) {
  return lastSeen[host] && (uint32_t)(millis() - lastSeen[host]) < SEEN_WINDOW_MS;
}

// ---- one synchronous ICMP ping via the lwIP ping_sock session API ----
static volatile bool s_alive, s_done;
static volatile uint32_t s_rtt;
static void onPingOk(esp_ping_handle_t hdl, void*) {
  s_alive = true;
  uint32_t t = 0; esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &t, sizeof(t)); s_rtt = t;  // RTT ms
}
static void onPingEnd(esp_ping_handle_t, void*) { s_done = true; }

// Returns the ICMP round-trip time in ms, or -1 if no reply within the timeout.
static int pingHost(IPAddress ip) {
  ip_addr_t target; memset(&target, 0, sizeof(target));
  target.type = IPADDR_TYPE_V4;
  target.u_addr.ip4.addr = (uint32_t)ip;                 // Arduino IPAddress -> lwIP net-order u32
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target; cfg.count = 1; cfg.timeout_ms = PING_TIMEOUT_MS; cfg.interval_ms = 0;
  esp_ping_callbacks_t cb = { nullptr, onPingOk, nullptr, onPingEnd };
  s_alive = false; s_done = false; s_rtt = 0;
  esp_ping_handle_t h;
  if (esp_ping_new_session(&cfg, &cb, &h) != ESP_OK) return -1;
  esp_ping_start(h);
  uint32_t t0 = millis();
  while (!s_done && millis() - t0 < PING_TIMEOUT_MS + 600) delay(2);
  esp_ping_delete_session(h);
  return s_alive ? (int)s_rtt : -1;
}

// A host can ignore ICMP yet still answer the ARP request the ping triggers. So after pinging,
// check the ARP table for a STABLE entry (a confirmed MAC) — etharp_find_addr only returns those,
// so no false positives. Checking per-host (right after its ping) beats reading the small ARP
// table once at the end, since the table only holds a handful of entries.
static bool arpKnown(IPAddress ip) {
  ip4_addr_t a; a.addr = (uint32_t)ip;                    // lwIP net-order u32
  struct eth_addr* eth = nullptr; const ip4_addr_t* ipret = nullptr;
  bool found = false;
  // Raw lwIP API from the app task -> must hold the TCPIP core lock. Walk every interface, not just
  // netif_default (which isn't reliably set under Arduino-ESP32).
  LOCK_TCPIP_CORE();
  for (struct netif* nif = netif_list; nif; nif = nif->next)
    if (etharp_find_addr(nif, &a, &eth, &ipret) >= 0) { found = true; break; }
  UNLOCK_TCPIP_CORE();
  return found;
}

static int sweep() {
  IPAddress me = WiFi.localIP();
  uint32_t now = millis(); if (!now) now = 1;             // 0 means "never", so avoid it
  int passCount = 0, maxRtt = 0, slowHost = 0; String list;
  for (int host = 1; host <= 254; host++) {
    IPAddress ip(me[0], me[1], me[2], host);
    bool self = (ip == me);
    int rtt = -1; bool arped = false;
    if (!self) for (int i = 0; i < PROBE_ATTEMPTS; i++) {  // retry to ride out dropped packets
      rtt = pingHost(ip);                                 // the ping resolves ARP internally (the proper,
      arped = arpKnown(ip);                               // thread-safe path); then we read the ARP table
      if (rtt >= 0 || arped) break;                       // found -> stop retrying
    }
    if (self || rtt >= 0 || arped) {
      lastSeen[host] = now; passCount++;                  // stamp the sighting (never cleared, just ages)
      if (rtt > maxRtt) { maxRtt = rtt; slowHost = host; }
      if (self)          list += " ." + String(host) + "*";
      else if (rtt >= 0) list += " ." + String(host) + "P" + rtt;   // ping + latency (ms)
      else               list += " ." + String(host) + "A";         // ARP only (ICMP blocked/dropped)
    }
  }
  int n = 0;
  for (int host = 1; host <= 254; host++) if (seen(host)) n++;
  Serial.printf("\n[netscan] %d.%d.%d.x  this pass: %d   seen(24h): %d   slowest ping: %dms (.%d)\n",
                me[0], me[1], me[2], passCount, n, maxRtt, slowHost);
  Serial.println(String("[netscan] P<ms>=ping  A=arp  *=self:") + list);
  return n;
}

static void render(int count) {
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  // branded header
  canvas.setTextSize(3); canvas.setCursor(20, 12); canvas.print("reTerminal E1001");
  canvas.setTextSize(2); canvas.setCursor(596, 18); canvas.print("Seeed Studio");
  canvas.drawFastHLine(20, 46, PANEL_W - 40, 2);
  // subnet + status line
  IPAddress me = WiFi.localIP();
  canvas.setTextSize(2); canvas.setCursor(20, 58);
  canvas.printf("SUBNET %d.%d.%d.x", me[0], me[1], me[2]);
  canvas.setCursor(520, 58); canvas.printf("%d seen 24h  batt %d%%", count, io.batteryPercent());
  // grid of .1..254 — 18 cols x 15 rows, filled = alive
  const int cols = 18, x0 = 14, y0 = 90, cw = (PANEL_W - 28) / cols, ch = 25;
  canvas.setTextSize(1);
  for (int host = 1; host <= 254; host++) {
    int i = host - 1, cx = x0 + (i % cols) * cw, cy = y0 + (i / cols) * ch;
    canvas.drawRect(cx, cy, cw - 2, ch - 2, 1);                            // cell outline
    if (!seen(host)) canvas.fillRect(cx + 2, cy + 2, cw - 6, ch - 6, 1);   // not seen lately -> black block
    else { canvas.setCursor(cx + 4, cy + 8); canvas.print(host); }         // seen in window -> octet on white
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
  epd.setFullEvery(4);                        // grid churns a lot — clear ghosting every 4 sweeps
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
