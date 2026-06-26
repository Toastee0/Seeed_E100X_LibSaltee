// NetScan — an hourly "seen any sign of life lately" map of the local /24, scanned from the device.
//
// Once an hour the reTerminal wakes, sweeps its own subnet, draws the result, does one clean full
// refresh, and deep-sleeps for the rest of the hour (e-paper holds the image with the power off).
//
// TWO display modes — press the front REFRESH button any time to toggle (it wakes the device,
// flips the mode, re-renders instantly, and goes back to sleep; the choice persists in RTC):
//   * LABELED  — each cell shows its octet if the host has been seen within SEEN_WINDOW_SCANS
//                sweeps (24 ~= 24 h), or a solid black block if not. Results stream in live: each
//                host logs to serial on answer and each grid row is partial-refreshed as it resolves.
//   * HEATMAP  — a 4-level-gray recency map: the longer since a host was last seen, the darker its
//                block, in 2-hour steps (<2h white, 2-4h light, 4-6h dark, >=6h black). Grayscale
//                only renders on a full refresh, so heatmap mode skips the per-row partials and
//                paints once at the end-of-sweep full refresh. Serial still streams live.
//
// Detection uses both ICMP and ARP: each host gets a ping (1 s timeout so slow responders aren't
// missed), and because a host can ignore ICMP yet still answer the ARP the ping triggers, the ARP
// table is also checked (etharp_find_addr — stable entries only, so no false positives). Both use
// only the ESP32 core (lwIP). Per-host state lives in RTC memory so it survives deep sleep; it
// resets only on a cold power-up.
//
// Fill in WIFI_SSID/WIFI_PASS. Needs Adafruit GFX. Board: XIAO_ESP32S3, OPI PSRAM, CDC On Boot.
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"   // rtc_gpio_* — configure the button pad as an ext0 deep-sleep wake source
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
// LABELED mode: a host stays "seen" until it's been quiet this many consecutive sweeps (24 ~= 24 h).
const uint32_t SEEN_WINDOW_SCANS = 24;
// HEATMAP mode: scans (~hours) per darkness step. 4 levels over 3 steps -> black after ~6 h.
const uint32_t HEAT_STEP_SCANS = 2;
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
RTC_DATA_ATTR bool heatmapMode;              // false = labeled grid, true = 4-level recency heatmap
RTC_DATA_ATTR uint8_t gNet[3];               // our /24 base (so we can label the map without WiFi)

// Has this octet shown a sign of life within the labeled-mode window? (windowed by scan count)
static bool seen(int host) {
  return lastSeenScan[host] && (scanIndex - lastSeenScan[host]) < SEEN_WINDOW_SCANS;
}

// Heatmap shade for an octet: 3 (white, freshest) .. 0 (black, stale/never), stepping every
// HEAT_STEP_SCANS sweeps. Never-seen -> black.
static uint8_t heatLevel(int host) {
  if (!lastSeenScan[host]) return 0;
  uint32_t idx = (scanIndex - lastSeenScan[host]) / HEAT_STEP_SCANS;
  return idx >= 3 ? 0 : (uint8_t)(3 - idx);
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

// ---- drawing ----
// The 1-bit canvas holds chrome/outlines/text (blitted to the gray buffer as 1->black, 0->white).
// Gray SHADES can't live in the 1-bit canvas, so heatmap fills are written straight to the buffer.
static void blit(int x, int y, int w, int h) {
  uint8_t* fb = epd.buffer();
  for (int yy = y; yy < y + h && yy < PANEL_H; yy++)
    for (int xx = x; xx < x + w && xx < PANEL_W; xx++)
      fb[yy * PANEL_W + xx] = canvas.getPixel(xx, yy) ? 0 : 3;
}
static void fbFill(int x, int y, int w, int h, uint8_t gray) {
  uint8_t* fb = epd.buffer();
  for (int yy = y; yy < y + h && yy < PANEL_H; yy++)
    for (int xx = x; xx < x + w && xx < PANEL_W; xx++)
      fb[yy * PANEL_W + xx] = gray;
}

// One grid cell. LABELED: octet on white if seen, else black block. HEATMAP: interior shaded by recency.
static void drawCell(int host) {
  int i = host - 1, cx = GX0 + (i % COLS) * CW, cy = GY0 + (i / COLS) * CH;
  canvas.fillRect(cx + 1, cy + 1, CW - 4, CH - 4, 0);                    // clear interior to white
  canvas.drawRect(cx, cy, CW - 2, CH - 2, 1);                            // cell outline
  if (heatmapMode) {
    blit(cx, cy, CW - 2, CH - 2);                                        // push outline + white interior
    fbFill(cx + 2, cy + 2, CW - 6, CH - 6, heatLevel(host));             // overwrite interior with shade
  } else {
    if (!seen(host)) canvas.fillRect(cx + 2, cy + 2, CW - 6, CH - 6, 1); // not seen lately -> black block
    else { canvas.setTextSize(1); canvas.setCursor(cx + 4, cy + 8); canvas.print(host); }
    blit(cx, cy, CW - 2, CH - 2);
  }
}

// Top-right status line: "N active  batt X%" (heatmap, <6h) or "N seen 24h  batt X%" (labeled).
static void drawStatus() {
  int count = 0;
  if (heatmapMode) { for (int h = 1; h <= 254; h++) if (heatLevel(h)) count++; }
  else             { for (int h = 1; h <= 254; h++) if (seen(h))      count++; }
  canvas.fillRect(500, 52, PANEL_W - 508, 22, 0);
  canvas.setTextColor(1); canvas.setTextSize(2); canvas.setCursor(520, 58);
  canvas.printf("%d %s  batt %d%%", count, heatmapMode ? "active" : "seen 24h", io.batteryPercent());
  blit(500, 52, PANEL_W - 500, 24);
}

// HEATMAP legend: four swatches (fresh -> stale) with end labels. Outlines/labels into the canvas;
// the gray fills are applied to the buffer after the header is blitted.
static const int LEG_X = 300, LEG_Y = 54, LEG_SW = 22, LEG_GAP = 6, LEG_H = 18;
static void drawLegendCanvas() {
  for (int k = 0; k < 4; k++) canvas.drawRect(LEG_X + k * (LEG_SW + LEG_GAP), LEG_Y, LEG_SW, LEG_H, 1);
  canvas.setTextSize(1);
  canvas.setCursor(LEG_X, LEG_Y + LEG_H + 2); canvas.print("now");
  canvas.setCursor(LEG_X + 3 * (LEG_SW + LEG_GAP) - 6, LEG_Y + LEG_H + 2); canvas.print("6h+");
}
static void drawLegendShades() {
  const uint8_t sh[4] = {3, 2, 1, 0};
  for (int k = 0; k < 4; k++)
    fbFill(LEG_X + k * (LEG_SW + LEG_GAP) + 1, LEG_Y + 1, LEG_SW - 2, LEG_H - 2, sh[k]);
}

// Full frame from current state into the buffer (no panel refresh). Header band is blitted first so
// the per-cell heatmap shades (written straight to the buffer) aren't clobbered by a later canvas blit.
static void renderAll() {
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setTextSize(3); canvas.setCursor(20, 12); canvas.print("reTerminal E1001");
  canvas.setTextSize(2); canvas.setCursor(596, 18); canvas.print("Seeed Studio");
  canvas.drawFastHLine(20, 46, PANEL_W - 40, 2);
  canvas.setTextSize(2); canvas.setCursor(20, 58); canvas.printf("SUBNET %d.%d.%d.x", gNet[0], gNet[1], gNet[2]);
  if (heatmapMode) drawLegendCanvas();
  blit(0, 0, PANEL_W, GY0);                 // header band (everything above the grid)
  if (heatmapMode) drawLegendShades();      // legend grays go in after the header blit
  drawStatus();
  for (int host = 1; host <= 254; host++) drawCell(host);
}

// One full sweep. Streams each host to serial on answer; in LABELED mode also partial-refreshes each
// completed grid row (HEATMAP shades need a full refresh, so it paints once when the sweep finishes).
static int sweep() {
  IPAddress me = WiFi.localIP();
  int passCount = 0, maxRtt = 0, slowHost = 0;
  Serial.printf("\n[netscan] sweep #%lu of %d.%d.%d.x  [%s]  (P<ms>=ping  A=arp  *=self)\n",
                (unsigned long)scanIndex, me[0], me[1], me[2], heatmapMode ? "heatmap" : "labeled");
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
    if (host % COLS == 0 || host == 254) {                // row complete
      drawStatus();
      if (!heatmapMode) epd.refreshChanged(0);            // labeled: partial-refresh the changed band(s)
    }
  }
  int n = 0; for (int h = 1; h <= 254; h++) if (seen(h)) n++;
  Serial.printf("[netscan] sweep #%lu done: this pass %d, seen %d, slowest %dms (.%d)\n",
                (unsigned long)scanIndex, passCount, n, maxRtt, slowHost);
  return n;
}

static void deepSleep(uint64_t us) {
  Serial.printf("[netscan] sleeping %.1f min (press REFRESH to toggle heatmap)\n", us / 60.0e6);
  Serial.flush();
  epd.sleep();                                            // power the panel down (image is retained)
  esp_sleep_enable_timer_wakeup(us);
  // REFRESH button (GPIO3, RTC-capable, active-low) wakes us to toggle the display mode. ext0 is the
  // single-pad wake source; enable the pad's RTC pull-up so it reads HIGH until the button grounds it.
  rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_REFRESH);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_REFRESH);
  esp_err_t e = esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_REFRESH, 0);  // 0 = wake when it goes low
  if (e != ESP_OK) Serial.printf("[netscan] WARN ext0 wake enable failed: %d\n", (int)e);
  esp_deep_sleep_start();                                 // <- does not return; wakes back into setup()
}

void setup() {
  Serial.begin(115200); delay(200);
  uint32_t t0 = millis();
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  Serial.printf("[netscan] boot — wake cause = %d (2=ext0/button, 4=timer, 0=cold)\n", (int)cause);
  bool wokeTimer  = (cause == ESP_SLEEP_WAKEUP_TIMER);
  bool wokeButton = (cause == ESP_SLEEP_WAKEUP_EXT0);
  if (!wokeTimer && !wokeButton) {                        // cold power-up: no history, glass unknown
    memset(lastSeenScan, 0, sizeof(lastSeenScan));
    scanIndex = 0; heatmapMode = false; gNet[0] = gNet[1] = gNet[2] = 0;
  }

  io.begin();
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); deepSleep(MIN_SLEEP_US); }
  epd.setFullEvery(0);                        // we manage full refreshes by hand

  if (wokeButton) {                           // REFRESH pressed -> flip mode, re-render now, no rescan
    heatmapMode = !heatmapMode;
    io.beep();
    Serial.printf("[netscan] mode -> %s\n", heatmapMode ? "HEATMAP" : "labeled");
    renderAll();
    epd.displayFull();                        // instant grayscale/labeled switch from existing data
    uint32_t tr = millis();                   // wait for release so we don't immediately wake again
    while (digitalRead(PIN_BTN_REFRESH) == LOW && millis() - tr < 3000) delay(10);
    deepSleep(CYCLE_US);                       // resume the hourly cadence from now
  }

  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nwifi FAILED — back to sleep, retry next cycle");
    deepSleep(WIFI_FAIL_SLEEP_US);
  }
  Serial.println("\nwifi up");
  IPAddress me = WiFi.localIP(); gNet[0] = me[0]; gNet[1] = me[1]; gNet[2] = me[2];

  // Bring the framebuffer to the last-known image. On a timer wake the panel still physically shows
  // it (retained through deep sleep), so we sync the snapshot — no flash. Cold boot full-refreshes.
  renderAll();
  if (wokeTimer) epd.syncSnapshot();
  else           epd.displayFull();

  scanIndex++;                               // this sweep's index (after reconstructing the prior frame)
  sweep();                                   // streams results (labeled mode also partials per row)

  epd.displayFull();                         // the one "big" refresh — paints the heatmap / clears ghosting

  // Sleep out the rest of the hour (wake-to-wake ~= CYCLE_US), floored so a long sweep can't busy-loop.
  uint64_t elapsedUs = (uint64_t)(millis() - t0) * 1000;
  uint64_t sleepUs = elapsedUs < CYCLE_US - MIN_SLEEP_US ? CYCLE_US - elapsedUs : MIN_SLEEP_US;
  deepSleep(sleepUs);
}

void loop() {}   // never runs — setup() ends in deep sleep and wakes back into setup()
