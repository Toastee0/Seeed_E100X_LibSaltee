// ThinClient — render server-side, blit on the device, update only what changed.
//
// The reTerminal E1001 fetches an 800x480 grayscale frame (one byte/pixel, value 0..3) from a
// URL you control and shows it. Your server does all the layout, so you change the UI with no
// reflash. The device keeps a snapshot of the glass and uses the panel's fast 1-bit PARTIAL
// refresh to redraw only the pixels that differ between fetches (~0.4-1.5 s) — a full 4-gray
// refresh happens on boot, on a view change, and every FULL_EVERY cycles to clear ghosting.
//
// Frame format: exactly 800*480 = 384000 bytes, row-major, each byte 0(black)..3(white).
// (Generate it however you like — e.g. Pillow: img.convert to 4 levels, write .tobytes().)
//
// Buttons: LEFT/RIGHT change the ?view= index; REFRESH forces a full gray refresh.
// Fill in WIFI_SSID/WIFI_PASS/FRAME_URL below. Board: XIAO_ESP32S3, OPI PSRAM, CDC on boot.
#include <WiFi.h>
#include <HTTPClient.h>
#include <ReTerminalMono.h>
#include <ReTerminalPeripherals.h>
using namespace ReTerminal;

// ---- configure me ----
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const char* FRAME_URL = "http://192.168.1.50:8080/frame.raw";  // appends ?view=N
const uint32_t REFRESH_MS = 60000;     // fetch cadence
const int      NVIEWS     = 1;         // how many ?view= pages your server serves
const int      FULL_EVERY = 30;        // force a full gray refresh at least this often
const int      HEADER_ROWS = 0;        // top rows to leave untouched (keep gray chrome there)

Mono epd;
Peripherals io;
int  view = 0, cyclesSinceFull = 0;
bool needFull = true;
uint32_t lastFetch = 0;

static bool fetchFrame(int v) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin(String(FRAME_URL) + "?view=" + v);
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  WiFiClient* s = http.getStreamPtr();
  const size_t want = (size_t)Mono::width() * Mono::height();
  size_t got = 0; uint8_t* fb = epd.buffer(); uint32_t t0 = millis();
  while (got < want && millis() - t0 < 12000) {
    if (s->available()) { int r = s->readBytes(fb + got, want - got); if (r > 0) { got += r; t0 = millis(); } }
    else delay(2);
  }
  http.end();
  return got == want;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  io.begin();
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); while (true) delay(1000); }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nwifi up" : "\nwifi FAILED");
}

void loop() {
  if (io.leftPressed())  { view = (view + NVIEWS - 1) % NVIEWS; needFull = true; io.beep(); }
  if (io.rightPressed()) { view = (view + 1) % NVIEWS;          needFull = true; io.beep(); }
  if (io.refreshPressed()) { needFull = true; io.beep(); }

  if (needFull || millis() - lastFetch >= REFRESH_MS) {
    lastFetch = millis();
    if (fetchFrame(view)) {
      io.led(true);
      if (needFull || ++cyclesSinceFull >= FULL_EVERY) {
        epd.displayFull();                 // crisp 4-gray base
        cyclesSinceFull = 0; needFull = false;
        Serial.printf("view %d: full gray\n", view);
      } else {
        uint32_t ch = epd.refreshChanged(HEADER_ROWS);   // fast partial of changed regions
        Serial.printf("view %d: partial (%u px changed)\n", view, ch);
      }
      io.led(false);
    }
  }
  delay(10);
}
