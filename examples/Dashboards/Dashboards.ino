// Dashboards — three good-looking 4-level-gray dashboards for the reTerminal E1001, one sketch.
//
// Same data (NTP clock + Open-Meteo weather + on-board SHT4x + battery) rendered three ways:
//   0  LCARS    — Star Trek TNG: black field, rounded "elbow" panels, gray blocks, caps labels
//   1  MAC      — classic Mac OS: white, a rounded window with a striped title bar, crisp 1-bit
//   2  CONSOLE  — Linux terminal: black CRT, monospace "status" output in a box-drawn frame
//
// LEFT/RIGHT cycle the style; REFRESH forces a clean full refresh. Drawing goes into a GFXcanvas8
// (1 byte/pixel) using gray levels 0(black)..3(white), then is copied to the panel buffer — so we get
// Adafruit_GFX rounded-rects and text in true grayscale. Fill in WIFI_SSID/WIFI_PASS + LAT/LONG.
// Board: XIAO_ESP32S3, OPI PSRAM, USB CDC On Boot = Enabled.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
#include <ReTerminalPeripherals.h>
using namespace ReTerminal;

const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const float LATITUDE  = 43.6532f;        // default: downtown Toronto
const float LONGITUDE = -79.3832f;
const char* LOCATION  = "Toronto";       // shown on the LCARS rail
const char* NTP_SERVER = "pool.ntp.org"; // time source ("HQ"); shown on the LCARS info line
const char* TZ = "EST5EDT,M3.2.0,M11.1.0";
const uint32_t WEATHER_EVERY_MS = 600000;

Mono epd;
Peripherals io;
GFXcanvas8 cv(PANEL_W, PANEL_H);          // 4-gray drawing surface (color = level 0..3)

float outTemp = NAN; float outHum = NAN; int wcode = -1; float inT = NAN, inH = NAN;
int style = 0, lastMin = -1; uint32_t lastWeather = 0;

static const char* weatherText(int c) {
  if (c < 0) return "";
  if (c == 0) return "Clear"; if (c <= 3) return "Cloudy"; if (c <= 48) return "Fog";
  if (c <= 67) return "Rain"; if (c <= 77) return "Snow"; if (c <= 82) return "Showers";
  if (c <= 99) return "Storm"; return "";
}

static void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(LATITUDE, 4) +
               "&longitude=" + String(LONGITUDE, 4) + "&current=temperature_2m,relative_humidity_2m,weather_code";
  if (!http.begin(client, url)) return;
  if (http.GET() == 200) {
    String b = http.getString();
    int c = b.indexOf("\"current\":"); if (c < 0) c = 0;       // skip the "current_units" block
    int ti = b.indexOf("\"temperature_2m\":", c), hi = b.indexOf("\"relative_humidity_2m\":", c), wi = b.indexOf("\"weather_code\":", c);
    if (ti >= 0) outTemp = b.substring(ti + 17).toFloat();
    if (hi >= 0) outHum  = b.substring(hi + 23).toFloat();
    if (wi >= 0) wcode  = b.substring(wi + 15).toInt();
  }
  http.end();
}

// ---- small gray-draw helpers on the canvas ----
static void tprint(int x, int y, uint8_t size, uint8_t lvl, const String& s) {
  cv.setTextSize(size); cv.setTextColor(lvl); cv.setCursor(x, y); cv.print(s);
}
static int twidth(uint8_t size, const String& s) { return s.length() * 6 * size; }
static String hhmm(const struct tm& t) { char b[8]; strftime(b, sizeof b, "%H:%M", &t); return b; }

// ============================== LCARS ==============================
static const int RAIL_W = 170;   // left-rail / tab width (horizontal proportions unchanged)
// 2-line rail pip: label fills the pip (size-2), white text on dark shades, black on light.
static void lcarsPip(int y, int h, uint8_t shade, const String& l1, const String& l2) {
  cv.fillRoundRect(0, y, RAIL_W, h, 12, shade);
  cv.fillRect(0, y, 12, h, shade);                        // square the left edge (no bevel at the screen edge)
  uint8_t tc = (shade <= 1) ? 3 : 0;
  cv.setTextColor(tc); cv.setTextSize(2);
  const int sp = 22, top = y + (h - (sp + 16)) / 2 - 1;  // two lines, centered, nudged up 1px
  cv.setCursor(12, top);      cv.print(l1);
  cv.setCursor(12, top + sp); cv.print(l2);
}
static int iround(float v) { return (int)lroundf(v); }

static void renderLCARS(const struct tm& t) {
  cv.fillScreen(0);                                            // black field
  const int SB = RAIL_W, MX = SB + 16, MW = PANEL_W - MX - 8;
  const int TBY = 6;
  const int CAPH = 76;                                         // top row height (brand cap + header bar share it)
  const int TBH = CAPH;                                        // header bar = full cap height -> clean rectangular top
  const int P0 = TBY + CAPH + 6, PH = 52, PG = 4;             // rail tabs: first top, height, gap
  const int chronoY = P0 + 2 * (PH + PG);                      // chrono tab row (3rd tab)
  // --- top frame: tall brand cap sweeping into the UNBROKEN top bar ---
  cv.fillRoundRect(0, TBY, SB, CAPH, 26, 2);
  cv.fillRect(0, TBY + 26, 26, CAPH - 26, 2);                 // square bottom-left; keep the UI's top-left corner rounded
  // start the bar inside the cap (same shade) so there is no gap at the vertical column
  cv.fillRoundRect(SB - 40, TBY, PANEL_W - (SB - 40) - 8, TBH, 14, 2);
  // brand in the corner: "LCARS" over "SALT0" (the line break is implicit)
  cv.setTextColor(0); cv.setTextSize(3);
  cv.setCursor(16, TBY + 9);  cv.print("LCARS");             // centered, nudged up 1px
  cv.setCursor(16, TBY + 41); cv.print("SALT0");
  // --- 4-column data strip: headers ON the top bar, values centered in the space below ---
  const char* h1[4] = {"EXTERNAL", "EXTERNAL", "INTERNAL", "INTERNAL"};
  const char* h2[4] = {"TEMP", "HUMIDITY", "TEMP", "HUMIDITY"};
  String val[4] = { isnan(outTemp) ? "--" : String(iround(outTemp)) + "c",
                    isnan(outHum)  ? "--" : String(iround(outHum))  + "%",
                    isnan(inT)     ? "--" : String(iround(inT))     + "c",
                    isnan(inH)     ? "--" : String(iround(inH))     + "%" };
  int cw = MW / 4;
  const int valY = (TBY + TBH + chronoY - 5 * 8) / 2;        // centered between top bar & HQ bar
  for (int i = 0; i < 4; i++) {
    int cx = MX + i * cw;
    cv.setTextColor(0); cv.setTextSize(2);                       // black text on the gray bar (larger now)
    cv.setCursor(cx + (cw - twidth(2, h1[i])) / 2, TBY + 18); cv.print(h1[i]);   // two lines fill the tall bar (centered, up 1px)
    cv.setCursor(cx + (cw - twidth(2, h2[i])) / 2, TBY + 40); cv.print(h2[i]);
    cv.setTextColor(3); cv.setTextSize(5);                       // values stay white on the black field
    cv.setCursor(cx + (cw - twidth(5, val[i])) / 2, valY); cv.print(val[i]);
  }
  // --- HQ bar: same row as the chrono tab, contiguous with it (like the header row + brand cap) ---
  cv.fillRoundRect(SB - 40, chronoY, PANEL_W - (SB - 40) - 8, PH, 14, 1);   // starts inside the rail (same shade)
  char sy[16]; strftime(sy, sizeof sy, "%d-%m-%y", &t);        // date only, no time
  cv.setTextColor(3); cv.setTextSize(2); cv.setCursor(MX + 16, chronoY + (PH - 16) / 2 - 1);
  cv.print(String("connected to hq: via ") + NTP_SERVER + "  " + sy);
  // --- big clock, centered in the open region below the HQ bar ---
  String hm = hhmm(t);
  cv.setTextColor(3); cv.setTextSize(13);
  int clkX = MX + (MW - twidth(13, hm)) / 2;                   // center horizontally in the right region
  int clkY = (chronoY + PH + PANEL_H - 13 * 8) / 2;          // center vertically below the HQ bar
  cv.setCursor(clkX, clkY); cv.print(hm);
  // --- left rail pips: 7 tabs evenly filling from the cap down to the bottom edge ---
  long rssi = WiFi.RSSI();
  lcarsPip(P0 + 0 * (PH + PG), PH, 2, "Location:",  LOCATION);
  lcarsPip(P0 + 1 * (PH + PG), PH, 3, "Weather:",   weatherText(wcode));
  lcarsPip(P0 + 2 * (PH + PG), PH, 1, "Chrono:",    "RTC/NTP");
  lcarsPip(P0 + 3 * (PH + PG), PH, 3, "WiFi RSSI:", String(rssi) + " db");
  lcarsPip(P0 + 4 * (PH + PG), PH, 2, "WiFi SSID:", WiFi.SSID());
  lcarsPip(P0 + 5 * (PH + PG), PH, 3, "Battery:",   String(io.batteryPercent()) + " %");   // single % only
  lcarsPip(P0 + 6 * (PH + PG), PH, 1, "Node IP:",   WiFi.localIP().toString());
}

// ============================== CLASSIC MAC ==============================
static void renderMac(const struct tm& t) {
  cv.fillScreen(3);                                            // white desktop
  cv.drawRoundRect(18, 16, PANEL_W - 36, PANEL_H - 32, 14, 0);
  cv.drawRoundRect(19, 17, PANEL_W - 38, PANEL_H - 34, 13, 0);
  // striped title bar
  for (int y = 24; y < 52; y += 4) cv.drawFastHLine(22, y, PANEL_W - 44, 0);
  int tw = twidth(2, "reTerminal");
  cv.fillRect((PANEL_W - tw) / 2 - 12, 22, tw + 24, 32, 3);   // clear box for the title
  tprint((PANEL_W - tw) / 2, 30, 2, 0, "reTerminal");
  cv.fillRect(34, 30, 16, 16, 3); cv.drawRect(34, 30, 16, 16, 0);   // close box
  // clock centred
  String hm = hhmm(t); int cw = twidth(10, hm);
  tprint((PANEL_W - cw) / 2, 84, 10, 0, hm);
  char d[40]; strftime(d, sizeof d, "%A, %d %B %Y", &t);
  int dw = twidth(2, d); tprint((PANEL_W - dw) / 2, 196, 2, 0, d);
  // two sunken fields
  for (int i = 0; i < 2; i++) {
    int x = 44 + i * 360, y = 248, w = 332, h = 150;
    cv.drawRect(x, y, w, h, 0);
    tprint(x + 14, y + 12, 2, 0, i == 0 ? "Outdoor" : "Indoor");
    cv.drawFastHLine(x + 12, y + 38, w - 24, 0);
    String big = i == 0 ? (isnan(outTemp) ? "--" : String(outTemp, 1) + " C")
                        : (isnan(inT)     ? "--" : String(inT, 1)     + " C");
    tprint(x + 14, y + 52, 5, 0, big);
    String sub = i == 0 ? String(weatherText(wcode)) : (isnan(inH) ? "" : String((int)inH) + "% RH");
    tprint(x + 14, y + 116, 2, 0, sub);
  }
  tprint(PANEL_W - 150, PANEL_H - 36, 2, 0, "Batt " + String(io.batteryPercent()) + "%");
}

// ============================== LINUX CONSOLE ==============================
// A serial-terminal WINDOW: double-height title bar ("re-terminal:" + big clock where the window
// controls sit), then a boxless 80-col terminal body — black screen, phosphor-light text.
static void renderConsole(const struct tm& t) {
  cv.fillScreen(0);                                            // terminal body (CRT black)
  const int TB = 64;                                          // title bar = ~2x a normal WM title bar
  cv.fillRect(0, 0, PANEL_W, TB, 2);                          // gray window chrome
  cv.drawFastHLine(0, TB, PANEL_W, 0);
  tprint(16, 20, 3, 0, "re-terminal:");                       // window name, left
  String hm = hhmm(t);
  tprint(PANEL_W - 16 - twidth(5, hm), 12, 5, 0, hm);         // clock, top-right (where min/max/close go)

  const uint8_t FG = 3, DIM = 2; const int LH = 30; int y = TB + 18; char ln[80];
  tprint(16, y, 2, DIM, "re-terminal login: admin"); y += LH;
  tprint(16, y, 2, FG, "admin@re-terminal:~$ status"); y += LH + 8;
  char dt[28]; strftime(dt, sizeof dt, "%H:%M:%S  %a %d %b %Y", &t);
  snprintf(ln, sizeof ln, "time     %s", dt); tprint(16, y, 2, FG, ln); y += LH;
  snprintf(ln, sizeof ln, "outdoor  %s C   %s", isnan(outTemp) ? "--" : String(outTemp, 1).c_str(), weatherText(wcode)); tprint(16, y, 2, FG, ln); y += LH;
  snprintf(ln, sizeof ln, "indoor   %s C   %s%% RH", isnan(inT) ? "--" : String(inT, 1).c_str(), isnan(inH) ? "--" : String((int)inH).c_str()); tprint(16, y, 2, FG, ln); y += LH;
  snprintf(ln, sizeof ln, "battery  %d%%", io.batteryPercent()); tprint(16, y, 2, FG, ln); y += LH;
  snprintf(ln, sizeof ln, "uptime   %lu min", millis() / 60000); tprint(16, y, 2, DIM, ln); y += LH + 8;
  tprint(16, y, 2, FG, "admin@re-terminal:~$");
  cv.fillRect(16 + twidth(2, "admin@re-terminal:~$ "), y, 13, 22, FG);   // cursor block
}

static void draw(const struct tm& t) {
  if (io.readSHT4x(inT, inH)) {} else { inT = NAN; inH = NAN; }
  if (style == 1) renderMac(t); else if (style == 2) renderConsole(t); else renderLCARS(t);
  memcpy(epd.buffer(), cv.getBuffer(), (size_t)PANEL_W * PANEL_H);   // canvas levels -> panel buffer
  epd.displayFull();
}

void setup() {
  Serial.begin(115200); delay(200);
  io.begin();
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); while (true) delay(1000); }
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  configTzTime(TZ, NTP_SERVER, "time.nist.gov");
  fetchWeather(); lastWeather = millis();
  struct tm t; if (getLocalTime(&t, 8000)) { draw(t); lastMin = t.tm_min; }
}

void loop() {
  bool change = false;
  if (io.leftPressed())  { style = (style + 2) % 3; change = true; }
  if (io.rightPressed()) { style = (style + 1) % 3; change = true; }
  if (io.refreshPressed()) change = true;
  struct tm t;
  if (!getLocalTime(&t, 500)) { delay(50); return; }
  if (millis() - lastWeather >= WEATHER_EVERY_MS) { fetchWeather(); lastWeather = millis(); }
  if (change || t.tm_min != lastMin) { draw(t); lastMin = t.tm_min; }
  delay(50);
}
