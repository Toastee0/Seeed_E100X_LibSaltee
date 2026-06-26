// NetScan — an hourly "seen any sign of life lately" map of the local /24, scanned from the device.
//
// Once an hour the reTerminal wakes, sweeps its own subnet, and shows a grid of .1–.254: a cell
// with its octet = that host has been seen recently; a solid black block = not seen. Results stream
// in live — each grid row is partial-refreshed as it resolves (and each host is logged to serial the
// instant it answers), so you watch the map fill in rather than waiting for the whole sweep. When the
// sweep finishes it does ONE clean full refresh (to wipe partial-refresh ghosting) and then deep-sleeps
// for the rest of the hour. E-paper holds the image with the power off, so the wall display stays lit.
//
// Detection uses both ICMP and ARP: each host gets a ping (1 s timeout so slow responders aren't
// missed), and because a host can ignore ICMP yet still answer the ARP the ping triggers, the ARP
// table is also checked (etharp_find_addr — stable entries only, so no false positives). Both use
// only the ESP32 core (lwIP).
//
// The "seen" window is measured in SCANS, not wall-clock: a host stays marked until it's been quiet
// for SEEN_WINDOW_SCANS consecutive hourly sweeps (24 = ~24 h). The per-host state lives in RTC
// memory so it survives deep sleep; it resets only on a cold power-up.
//
// Fill in WIFI_SSID/WIFI_PASS. Needs Adafruit GFX. Board: XIAO_ESP32S3, OPI PSRAM, CDC On Boot.
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include "esp_sleep.h"
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
const uint64_t CYCLE_US = 3600ULL * 1000000;  // target wake-to-wake period: 1 hour
const uint32_t MIN_SLEEP_US = 60UL * 1000000; // never sleep less than this (e.g. after a slow sweep)
const uint32_t WIFI_FAIL_SLEEP_US = 300UL * 1000000;  // couldn't join WiFi -> retry in 5 min
const uint32_t PING_TIMEOUT_MS = 1000;     // generous per-host wait so slow responders aren't missed.
                                           // Alive hosts answer fast; only dead hosts cost the full
                                           // second, so a full /24 sweep takes a few minutes.
// A host stays "seen" until it's been quiet for this many consecutive hourly sweeps (24 ~= 24 h).
const uint32_t SEEN_WINDOW_SCANS = 24;
const int PROBE_ATTEMPTS = 2;             // retry each host this many times — catches dropped packets
                                          // on a lossy WiFi link

// Grid geometry (shared by the chrome and per-cell draws).
const int COLS = 18, GX0 = 14, GY0 = 90, CW = (PANEL_W - 28) / COLS, CH = 25;

Mono epd;
Peripherals io;
GFXcanvas1 canvas(PANEL_W, PANEL_H);

// --- state that must survive deep sleep: lives in RTC memory, zeroed only on a cold power-up ---
RTC_DATA_ATTR uint32_t scanIndex;            // sweeps since cold boot (1-based once scanning starts)
RTC_DATA_ATTR uint32_t lastSeenScan[256];    // scanIndex of each octet's last sighting (0 = never)

// Has this octet shown a sign of life within the window? (windowed by scan count, not millis)
static bool seen(int host) {
  return lastSeenScan[host] && (scanIndex - lastSeenScan[host]) < SEEN_WINDOW_SCANS;
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

// ---- drawing (into the 1-bit canvas, then blitted to the gray framebuffer 1->black, 0->white) ----
static void blit(int x, int y, int w, int h) {
  uint8_t* fb = epd.buffer();
  for (int yy = y; yy < y + h && yy < PANEL_H; yy++)
    for (int xx = x; xx < x + w && xx < PANEL_W; xx++)
      fb[yy * PANEL_W + xx] = canvas.getPixel(xx, yy) ? 0 : 3;
}

// One grid cell: octet on white if seen lately, solid black block if not.
static void drawCell(int host) {
  int i = host - 1, cx = GX0 + (i % COLS) * CW, cy = GY0 + (i / COLS) * CH;
  canvas.fillRect(cx + 1, cy + 1, CW - 4, CH - 4, 0);                    // clear interior to white
  canvas.drawRect(cx, cy, CW - 2, CH - 2, 1);                            // cell outline
  if (!seen(host)) canvas.fillRect(cx + 2, cy + 2, CW - 6, CH - 6, 1);   // not seen lately -> black block
  else { canvas.setTextSize(1); canvas.setCursor(cx + 4, cy + 8); canvas.print(host); }
  blit(cx, cy, CW - 2, CH - 2);
}

// The top-right status line ("N seen 24h  batt X%"); redrawn as the running count climbs.
static void drawStatus(int count) {
  canvas.fillRect(500, 52, PANEL_W - 508, 22, 0);
  canvas.setTextColor(1); canvas.setTextSize(2); canvas.setCursor(520, 58);
  canvas.printf("%d seen 24h  batt %d%%", count, io.batteryPercent());
  blit(500, 52, PANEL_W - 500, 24);
}

static int seenCount() { int n = 0; for (int h = 1; h <= 254; h++) if (seen(h)) n++; return n; }

// Static chrome + every cell at the current scanIndex, blitted to the framebuffer (no refresh).
static void renderAll() {
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setTextSize(3); canvas.setCursor(20, 12); canvas.print("reTerminal E1001");
  canvas.setTextSize(2); canvas.setCursor(596, 18); canvas.print("Seeed Studio");
  canvas.drawFastHLine(20, 46, PANEL_W - 40, 2);
  IPAddress me = WiFi.localIP();
  canvas.setTextSize(2); canvas.setCursor(20, 58); canvas.printf("SUBNET %d.%d.%d.x", me[0], me[1], me[2]);
  for (int host = 1; host <= 254; host++) drawCell(host);
  drawStatus(seenCount());
  blit(0, 0, PANEL_W, PANEL_H);
}

// One full sweep. Streams results: each host -> serial immediately, each completed grid row ->
// a partial refresh, the running "seen" count -> the header.
static int sweep() {
  IPAddress me = WiFi.localIP();
  int passCount = 0, maxRtt = 0, slowHost = 0;
  Serial.printf("\n[netscan] sweep #%lu of %d.%d.%d.x  (P<ms>=ping  A=arp  *=self)\n",
                (unsigned long)scanIndex, me[0], me[1], me[2]);
  for (int host = 1; host <= 254; host++) {
    IPAddress ip(me[0], me[1], me[2], host);
    bool self = (ip == me);
    int rtt = -1; bool arped = false;
    // Empty IPs are the bulk of a /24 and dominate sweep time, so probe a never-seen address only
    // once. An address we've seen before gets the full retry budget to ride out dropped packets.
    int attempts = lastSeenScan[host] ? PROBE_ATTEMPTS : 1;
    if (!self) for (int i = 0; i < attempts; i++) {
      rtt = pingHost(ip);                                 // the ping resolves ARP internally (the proper,
      arped = arpKnown(ip);                               // thread-safe path); then we read the ARP table
      if (rtt >= 0 || arped) break;                       // found -> stop retrying
    }
    if (self || rtt >= 0 || arped) {
      lastSeenScan[host] = scanIndex; passCount++;        // stamp the sighting (ages out by scan count)
      if (rtt > maxRtt) { maxRtt = rtt; slowHost = host; }
      if (self)          Serial.printf("[netscan] .%d *\n", host);
      else if (rtt >= 0) Serial.printf("[netscan] .%d P%d\n", host, rtt);  // ping + latency (ms)
      else               Serial.printf("[netscan] .%d A\n", host);         // ARP only (ICMP blocked)
    }
    drawCell(host);                                       // reflect this host in the framebuffer now
    if (host % COLS == 0 || host == 254) {                // row complete -> push it + the count to glass
      drawStatus(seenCount());
      epd.refreshChanged(0);                              // partial-refresh just the changed band(s)
    }
  }
  int n = seenCount();
  Serial.printf("[netscan] sweep #%lu done: this pass %d, seen %d, slowest %dms (.%d)\n",
                (unsigned long)scanIndex, passCount, n, maxRtt, slowHost);
  return n;
}

static void deepSleepUs(uint64_t us) {
  Serial.printf("[netscan] sleeping %.1f min\n", us / 60.0e6);
  Serial.flush();
  epd.sleep();                                            // power the panel down (image is retained)
  esp_sleep_enable_timer_wakeup(us);
  esp_deep_sleep_start();                                 // <- does not return; wakes back into setup()
}

void setup() {
  Serial.begin(115200); delay(200);
  uint32_t t0 = millis();
  bool wokeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER);
  if (!wokeFromSleep) {                                   // cold power-up: no history, glass unknown
    memset(lastSeenScan, 0, sizeof(lastSeenScan));
    scanIndex = 0;
  }

  io.begin();
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); deepSleepUs(MIN_SLEEP_US); }
  epd.setFullEvery(0);                        // we manage full refreshes by hand (one per sweep)

  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nwifi FAILED — back to sleep, retry next cycle");
    deepSleepUs(WIFI_FAIL_SLEEP_US);
  }
  Serial.println("\nwifi up");

  // Bring the framebuffer to the last-known image. On a timer wake the panel still physically shows
  // it (retained through deep sleep), so we just sync the snapshot — no flash. On a cold boot the
  // glass is unknown, so we full-refresh to establish a clean base.
  renderAll();
  if (wokeFromSleep) epd.syncSnapshot();
  else               epd.displayFull();

  scanIndex++;                               // this sweep's index (after reconstructing the prior frame)
  sweep();                                   // streams: short partials per row + per-host serial

  epd.displayFull();                         // the one "big" refresh — wipes partial ghosting before the long idle

  // Sleep out the rest of the hour (wake-to-wake ~= CYCLE_US), floored so a long sweep can't busy-loop.
  uint64_t elapsedUs = (uint64_t)(millis() - t0) * 1000;
  uint64_t sleepUs = elapsedUs < CYCLE_US - MIN_SLEEP_US ? CYCLE_US - elapsedUs : MIN_SLEEP_US;
  deepSleepUs(sleepUs);
}

void loop() {}   // never runs — setup() ends in deep sleep and wakes back into setup()
