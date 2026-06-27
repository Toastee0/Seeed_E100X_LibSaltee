// Dashboards — three good-looking 4-level-gray dashboards for the reTerminal E1001, one sketch.
//
// Same data (NTP clock + Open-Meteo weather + on-board SHT4x + battery) rendered three ways:
//   0  LCARS    — Star Trek TNG: black field, rounded "elbow" panels, gray blocks, caps labels
//   1  MAC      — classic Mac OS: white, a rounded window with a striped title bar, crisp 1-bit
//   2  CONSOLE  — Linux terminal: black CRT, monospace "status" output in a box-drawn frame
//
// LEFT/RIGHT cycle the style; REFRESH forces a clean full refresh. LCARS updates with quick 1-bit
// partials per minute (the clock + any data value that changed) and a clean 4-gray full page every 10 min.
// Drawing goes into a GFXcanvas8
// (1 byte/pixel) using gray levels 0(black)..3(white), then is copied to the panel buffer — so we get
// Adafruit_GFX rounded-rects and text in true grayscale.
//
// ZERO-CONFIG ONBOARDING (no creds in source): on first boot — or whenever the Refresh button is
// HELD at power-on — the device opens its own WiFi hotspot and shows an LCARS-styled QR + steps on
// the e-paper. Join it from a phone, a captive page lists nearby networks; pick yours, type the
// password, choose a city (or enter lat/long + a timezone). WiFi creds + location + POSIX TZ are
// saved to NVS, and every boot after that goes straight to the dashboard. To move it to a new
// network, hold Refresh at power-on to re-open setup.
// Board: XIAO_ESP32S3, OPI PSRAM, USB CDC On Boot = Enabled.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
#include <ReTerminalQR.h>
#include <ReTerminalPeripherals.h>
#include "regions.h"
using namespace ReTerminal;

const char* NTP_SERVER = "pool.ntp.org"; // time source ("HQ"); shown on the LCARS info line
const uint32_t WEATHER_EVERY_MS = 600000;

// Runtime configuration, loaded from NVS (namespace "dash") by loadConfig() / written by the setup
// portal. No network credentials or location are ever hardcoded here — defaults below only apply
// before onboarding, and the device won't reach the dashboard until cfgSsid is set.
String cfgSsid = "", cfgPass = "";
String cfgLoc  = "Toronto";              // shown on the LCARS rail
float  cfgLat  = 43.6532f, cfgLon = -79.3832f;
String cfgTz   = "EST5EDT,M3.2.0,M11.1.0";
String cfgCity = "";                      // a typed city name still pending online geocoding ("" = none)
bool   cfgOta  = false;                    // wireless (OTA) firmware updates enabled at enrollment?
String cfgOtaPass = "";                    // user-chosen OTA password (no default — OTA off without one)
bool   cfgOtaShow = false;                 // show the OTA password on the local e-paper screen?

Mono epd;
Peripherals io;
GFXcanvas8 cv(PANEL_W, PANEL_H);          // 4-gray drawing surface (color = level 0..3)
Preferences prefs;
WebServer  web(80);
DNSServer  dns;
String     apName;                        // SoftAP SSID shown during onboarding
bool       g_portal = false;              // true while the captive setup portal is running
bool       g_ota = false;                 // ArduinoOTA is running (enrolled WITH a password)

float outTemp = NAN; float outHum = NAN; int wcode = -1; float inT = NAN, inH = NAN;
int style = 0, lastMin = -1; uint32_t lastWeather = 0;
// LCARS per-minute quick-refresh windows, set by renderLCARS(): one tight box per data value + the
// clock, each vertically centered about its text so the partial-refresh border sits symmetrically.
int lcVal[4][4];   // [i] = {x, y, w, h}
int lcClk[4];      // {x, y, w, h}
String lcValStr[4];     // last value string drawn in each box (to skip unchanged ones)
bool   lcValDirty[4];   // did box i change since the last draw?

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
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(cfgLat, 4) +
               "&longitude=" + String(cfgLon, 4) + "&current=temperature_2m,relative_humidity_2m,weather_code";
  if (!http.begin(client, url)) { Serial.println("weather: begin() failed"); return; }
  int code = http.GET();
  if (code == 200) {
    String b = http.getString();
    int c = b.indexOf("\"current\":"); if (c < 0) c = 0;       // skip the "current_units" block
    int ti = b.indexOf("\"temperature_2m\":", c), hi = b.indexOf("\"relative_humidity_2m\":", c), wi = b.indexOf("\"weather_code\":", c);
    if (ti >= 0) outTemp = b.substring(ti + 17).toFloat();
    if (hi >= 0) outHum  = b.substring(hi + 23).toFloat();
    if (wi >= 0) wcode  = b.substring(wi + 15).toInt();
  }
  Serial.printf("weather: %.4f,%.4f http=%d -> %.1fC %.0f%% code=%d\n",
                cfgLat, cfgLon, code, outTemp, outHum, wcode);
  http.end();
}

// ---- tiny JSON field readers (indexOf-based, matching fetchWeather's no-library style) ----
static String urlEncode(const String& s) {
  static const char* H = "0123456789ABCDEF"; String o;
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += ch;
    else { o += '%'; o += H[(ch >> 4) & 0xF]; o += H[ch & 0xF]; }
  }
  return o;
}
static String jsonStr(const String& b, const char* key, int from = 0) {   // key incl. quotes, e.g. "\"timezone\""
  int k = b.indexOf(key, from); if (k < 0) return "";
  int c = b.indexOf(':', k + strlen(key)); if (c < 0) return "";
  int q = b.indexOf('"', c + 1); if (q < 0) return "";
  int e = b.indexOf('"', q + 1); if (e < 0) return "";
  return b.substring(q + 1, e);
}
static float jsonNum(const String& b, const char* key, int from = 0) {
  int k = b.indexOf(key, from); if (k < 0) return NAN;
  int c = b.indexOf(':', k + strlen(key)); if (c < 0) return NAN;
  return b.substring(c + 1).toFloat();
}
static String ianaToPosix(const String& iana) {
  for (int i = 0; i < REGION_NTZMAP; i++) if (iana == REGION_TZMAP[i].iana) return REGION_TZMAP[i].posix;
  return "";
}

// Resolve a typed city NAME to lat/long (+ a DST-correct POSIX TZ where known) using the Open-Meteo
// geocoder. Runs at boot, AFTER WiFi is up — at portal time the device has no internet yet. On a hit
// it fills cfgLat/cfgLon/cfgLoc/cfgTz and returns true; cfgTz is left "auto" if the zone is unmapped.
static bool geocodeCity(const String& name) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(name) +
               "&count=1&language=en&format=json";
  if (!http.begin(client, url)) return false;
  int code = http.GET(); bool ok = false;
  if (code == 200) {
    String b = http.getString();
    int r = b.indexOf("\"results\"");
    if (r >= 0) {
      float la = jsonNum(b, "\"latitude\"", r), lo = jsonNum(b, "\"longitude\"", r);
      if (!isnan(la) && !isnan(lo)) {
        cfgLat = la; cfgLon = lo;
        String nm = jsonStr(b, "\"name\"", r); if (nm.length()) cfgLoc = nm;
        String iana = jsonStr(b, "\"timezone\"", r);
        String p = ianaToPosix(iana); cfgTz = p.length() ? p : "auto";
        ok = true;
      }
    }
  }
  Serial.printf("geocode '%s' http=%d ok=%d -> %.4f,%.4f tz=%s\n",
                name.c_str(), code, ok, cfgLat, cfgLon, cfgTz.c_str());
  http.end();
  return ok;
}

// Derive a POSIX TZ for the current lat/long via the forecast API's timezone=auto (used when a city
// was geolocated by the browser, or a typed city's zone isn't in the map). Prefers a DST-correct
// mapped POSIX string; otherwise synthesizes a fixed-offset zone from utc_offset_seconds.
static bool resolveTZAuto() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(cfgLat, 4) +
               "&longitude=" + String(cfgLon, 4) + "&current=temperature_2m&timezone=auto";
  if (!http.begin(client, url)) return false;
  int code = http.GET(); bool ok = false;
  if (code == 200) {
    String b = http.getString();
    String iana = jsonStr(b, "\"timezone\"");
    String p = ianaToPosix(iana);
    if (p.length()) { cfgTz = p; ok = true; }
    else {
      float off = jsonNum(b, "\"utc_offset_seconds\"");
      String ab = jsonStr(b, "\"timezone_abbreviation\"");
      if (!isnan(off)) {
        int sh = (int)off / 3600, sm = (abs((int)off) % 3600) / 60;   // POSIX offset has the OPPOSITE sign
        char buf[24];
        const char* nm = ab.length() ? ab.c_str() : "UTC";
        if (sm) snprintf(buf, sizeof buf, "%s%d:%02d", nm, -sh, sm);
        else    snprintf(buf, sizeof buf, "%s%d", nm, -sh);
        cfgTz = buf; ok = true;
      }
    }
  }
  Serial.printf("tzAuto http=%d ok=%d -> %s\n", code, ok, cfgTz.c_str());
  http.end();
  return ok;
}

// ---- small gray-draw helpers on the canvas ----
static void tprint(int x, int y, uint8_t size, uint8_t lvl, const String& s) {
  cv.setTextSize(size); cv.setTextColor(lvl); cv.setCursor(x, y); cv.print(s);
}
static int twidth(uint8_t size, const String& s) { return s.length() * 6 * size; }
static String hhmm(const struct tm& t) { char b[8]; strftime(b, sizeof b, "%H:%M", &t); return b; }
static bool validTime(const struct tm& t) { return t.tm_year >= 120; }   // >= year 2020 == NTP has synced

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
  const int vGlyphH = 7 * 5, vPadX = 8, vPadY = 7;           // size-5 ink height + symmetric padding
  for (int i = 0; i < 4; i++) {
    int cx = MX + i * cw;
    cv.setTextColor(0); cv.setTextSize(2);                       // black text on the gray bar (larger now)
    cv.setCursor(cx + (cw - twidth(2, h1[i])) / 2, TBY + 18); cv.print(h1[i]);   // two lines fill the tall bar (centered, up 1px)
    cv.setCursor(cx + (cw - twidth(2, h2[i])) / 2, TBY + 40); cv.print(h2[i]);
    cv.setTextColor(3); cv.setTextSize(5);                       // values stay white on the black field
    int vw = twidth(5, val[i]), vx = cx + (cw - vw) / 2;
    cv.setCursor(vx, valY); cv.print(val[i]);
    // quick-refresh window: tight box per value, vertically centered about its glyph
    lcVal[i][0] = vx - vPadX;          lcVal[i][1] = valY - vPadY;
    lcVal[i][2] = vw + 2 * vPadX;      lcVal[i][3] = vGlyphH + 2 * vPadY;
    lcValDirty[i] = (val[i] != lcValStr[i]); lcValStr[i] = val[i];   // only this changed since last draw?
  }
  // --- HQ bar: same row as the chrono tab, contiguous with it (like the header row + brand cap) ---
  cv.fillRoundRect(SB - 40, chronoY, PANEL_W - (SB - 40) - 8, PH, 14, 1);   // starts inside the rail (same shade)
  char sy[16]; if (validTime(t)) strftime(sy, sizeof sy, "%d-%m-%y", &t); else strcpy(sy, "--");   // "--" until NTP
  cv.setTextColor(3); cv.setTextSize(2); cv.setCursor(MX + 16, chronoY + (PH - 16) / 2 - 1);
  cv.print(String("connected to hq: via ") + NTP_SERVER + "  " + sy);
  // --- big clock, centered in the open region below the HQ bar ---
  String hm = validTime(t) ? hhmm(t) : "--:--";               // placeholder before the first NTP sync
  cv.setTextColor(3); cv.setTextSize(13);
  int clkX = MX + (MW - twidth(13, hm)) / 2;                   // center horizontally in the right region
  int clkY = (chronoY + PH + PANEL_H - 13 * 8) / 2;          // center vertically below the HQ bar
  cv.setCursor(clkX, clkY); cv.print(hm);
  { const int cgh = 7 * 13, cpadX = 8, cpadY = 10;            // clock quick-refresh box, centered about the glyph
    lcClk[0] = clkX - cpadX;            lcClk[1] = clkY - cpadY;
    lcClk[2] = twidth(13, hm) + 2 * cpadX; lcClk[3] = cgh + 2 * cpadY; }
  // --- left rail pips: 7 tabs evenly filling from the cap down to the bottom edge ---
  long rssi = WiFi.RSSI();
  lcarsPip(P0 + 0 * (PH + PG), PH, 2, "Location:",  cfgLoc);
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

// ============================== CONFIG (NVS) ==============================
static void loadConfig() {
  prefs.begin("dash", true);                                   // read-only
  cfgSsid = prefs.getString("ssid", "");
  cfgPass = prefs.getString("pass", "");
  cfgLoc  = prefs.getString("loc",  "Toronto");
  cfgLat  = prefs.getFloat ("lat",  43.6532f);
  cfgLon  = prefs.getFloat ("lon",  -79.3832f);
  cfgTz   = prefs.getString("tz",   "EST5EDT,M3.2.0,M11.1.0");
  cfgCity = prefs.getString("city", "");
  cfgOta  = prefs.getBool  ("ota", false);
  cfgOtaPass = prefs.getString("otapass", "");
  cfgOtaShow = prefs.getBool("otashow", false);
  prefs.end();
}

// Persist location values resolved at boot (geocode / tz-auto) so later boots skip the lookups.
static void saveResolved() {
  prefs.begin("dash", false);
  prefs.putFloat ("lat",  cfgLat); prefs.putFloat ("lon", cfgLon);
  prefs.putString("loc",  cfgLoc); prefs.putString("tz",  cfgTz);
  prefs.putString("city", cfgCity);
  prefs.end();
}

// One-time online resolution after WiFi is up: turn a typed city name into coordinates, and turn a
// still-"auto" timezone into a concrete POSIX TZ. Both are cached back to NVS so this only runs once.
static void resolveLocation() {
  bool dirty = false;
  if (cfgCity.length()) { if (geocodeCity(cfgCity)) { cfgCity = ""; dirty = true; } }
  if (cfgTz == "auto")  { if (resolveTZAuto())       dirty = true; }
  if (dirty) saveResolved();
}

// ============================== LCARS onboarding screens ==============================
// The setup/connecting/failed screens reuse the LCARS visual language (brand cap + top bar + rail
// pips) so onboarding looks like the product, not a bare debug page. Drawn in 4-gray on the canvas;
// the join-QR is composited straight into the panel buffer (pure black/white) before the full push.
static void lcChrome(const String& title) {
  cv.fillScreen(0);                                            // black field
  const int TBY = 6, CAPH = 76, SB = RAIL_W;
  cv.fillRoundRect(0, TBY, SB, CAPH, 26, 2);                  // brand cap
  cv.fillRect(0, TBY + 26, 26, CAPH - 26, 2);                 // square its bottom-left
  cv.fillRoundRect(SB - 40, TBY, PANEL_W - (SB - 40) - 8, CAPH, 14, 2);   // top bar, contiguous with cap
  cv.setTextColor(0); cv.setTextSize(3);
  cv.setCursor(16, TBY + 9);  cv.print("LCARS");
  cv.setCursor(16, TBY + 41); cv.print("SALT0");
  cv.setTextColor(0); cv.setTextSize(4);                       // screen title, black on the gray bar
  cv.setCursor(SB + 8, TBY + 24); cv.print(title);
}
static void lcFlush(const char* qr = nullptr, int qx = 540, int qy = 116, int qs = 220) {
  memcpy(epd.buffer(), cv.getBuffer(), (size_t)PANEL_W * PANEL_H);   // canvas levels -> panel buffer
  if (qr) ReTerminal::drawQR(epd.buffer(), epd.width(), qr, qx, qy, qs, qs);
  epd.displayFull();
}
// Body text lines in the open region, left of where the QR sits.
static void lcBody(const String* lines, int n, int x = RAIL_W + 8, int y0 = 130, int lh = 40) {
  cv.setTextColor(3); cv.setTextSize(3);
  for (int i = 0; i < n; i++) { cv.setCursor(x, y0 + i * lh); cv.print(lines[i]); }
}
static void lcFooter(const String& s) {
  cv.setTextColor(3); cv.setTextSize(2); cv.setCursor(8, 456); cv.print(s);   // full-width under the short rail
}

static void screenSetup() {                                   // SoftAP up: how to onboard + join QR
  lcChrome("WIFI SETUP");
  const int P0 = 88, PH = 52, PG = 4;
  lcarsPip(P0 + 0 * (PH + PG), PH, 2, "MODE",   "SETUP");
  lcarsPip(P0 + 1 * (PH + PG), PH, 3, "RADIO",  "HOTSPOT");
  lcarsPip(P0 + 2 * (PH + PG), PH, 1, "SECURITY:", "OPEN");
  lcarsPip(P0 + 3 * (PH + PG), PH, 3, "PORTAL", "1.4.1");
  String lines[] = {
    "1. On your phone,",
    "   join the WiFi:",
    "   " + apName,
    "   (or scan ->)",
    "2. A page opens;",
    "   pick your WiFi,",
    "   set your city.",
  };
  lcBody(lines, 7);
  lcFooter("Open hotspot - no password to join. Then browse to 192.168.4.1");
  char joinQR[64];
  ReTerminal::wifiQRPayload(joinQR, sizeof(joinQR), apName.c_str(), "", "nopass");
  lcFlush(joinQR);
}
static void screenConnecting(const String& ssid) {
  lcChrome("CONNECTING");
  const int P0 = 88, PH = 52, PG = 4;
  lcarsPip(P0 + 0 * (PH + PG), PH, 2, "MODE",  "JOIN");
  lcarsPip(P0 + 1 * (PH + PG), PH, 3, "STATE", "LINK");
  String lines[] = { "Network:", "   " + ssid, "", "Standby ..." };
  lcBody(lines, 4);
  lcFlush();
}
static void screenFailed(const String& ssid) {
  lcChrome("WIFI FAILED");
  const int P0 = 88, PH = 52, PG = 4;
  lcarsPip(P0 + 0 * (PH + PG), PH, 3, "STATE", "NO LINK");
  String lines[] = { "Could not join:", "   " + ssid, "", "Re-opening setup" };
  lcBody(lines, 4);
  lcFlush();
}

// ============================== captive-portal web pages ==============================
static String formPage() {
  String s = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>reTerminal setup</title><style>"
               "body{font-family:system-ui,sans-serif;max-width:430px;margin:20px auto;padding:0 14px;"
               "background:#000;color:#fc9}"
               "h2{color:#f90;border-bottom:3px solid #f90;padding-bottom:6px}"
               "label{display:block;margin:12px 0 2px;color:#9cf;font-weight:600}"
               "select,input{width:100%;font-size:1.05em;padding:9px;box-sizing:border-box;"
               "border:0;border-radius:6px;background:#222;color:#fff}"
               "button{width:100%;font-size:1.1em;padding:12px;margin:18px 0;border:0;border-radius:18px;"
               "background:#f90;color:#000;font-weight:700}"
               ".hint{color:#888;font-size:.82em;margin:2px 0}"
               ".chk{display:flex;align-items:center;color:#fc9;font-weight:400;margin:10px 0}"
               ".chk input{width:auto;margin:0 8px 0 0}"
               "#custom,#otapw{display:none}</style>"
               "<h2>reTerminal WiFi setup</h2><form method=POST action=/save>"
               "<label>Your WiFi network</label><select name=ssid>");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) s += "<option>" + WiFi.SSID(i) + "</option>";
  s += F("</select><label>WiFi password</label><input name=pass type=password>"
         "<h3 style='color:#f90;margin:18px 0 4px'>Location</h3>"
         "<p class=hint>For weather &amp; the clock.</p>"
         "<label>Type a city</label><input name=cityname placeholder='e.g. Paris, Tokyo, Denver'>"
         "<label>...or pick from the list</label>"
         "<select name=city onchange=\"document.getElementById('custom').style.display="
         "this.value=='custom'?'block':'none'\"><option value=''>-- choose --</option>");
  for (int i = 0; i < REGION_NCITIES; i++)
    s += "<option value=" + String(i) + ">" + REGION_CITIES[i].name + "</option>";
  s += F("<option value=custom>Custom (enter lat/long)...</option></select>"
         "<div id=custom><label>Place name</label><input name=loc>"
         "<label>Latitude</label><input name=lat type=number step=any placeholder='43.65'>"
         "<label>Longitude</label><input name=lon type=number step=any placeholder='-79.38'>"
         "<p class=hint>Tip: long-press your spot in a maps app to copy its coordinates.</p>"
         "<label>Timezone</label><select name=tz>");
  for (int i = 0; i < REGION_NZONES; i++)
    s += "<option value=" + String(i) + ">" + REGION_ZONES[i].label + "</option>";
  s += F("</select></div>"
         "<h3 style='color:#f90;margin:18px 0 4px'>Wireless updates (optional)</h3>"
         "<label class=chk><input type=checkbox name=ota value=1 onchange=\""
         "document.getElementById('otapw').style.display=this.checked?'block':'none'\">"
         " Update this firmware over WiFi (OTA)</label>"
         "<div id=otapw><label>OTA password</label>"
         "<input name=otapass type=text autocomplete=off placeholder='choose your own'>"
         "<label class=chk><input type=checkbox name=otashow value=1> Show this password on the display</label>"
         "<p class=hint>Lets the Arduino IDE flash new firmware over WiFi (network port). "
         "Used only if enabled &mdash; there is no default password.</p></div>"
         "<button type=submit>Save &amp; connect</button></form>");
  return s;
}

static void handleSave() {
  String ssid = web.arg("ssid"), pass = web.arg("pass");
  // Location precedence: browser geolocation > typed city (geocoded at boot) > preset city > custom.
  // "auto" TZ means "resolve it online once connected" (see resolveLocation()).
  String loc = cfgLoc, tz = cfgTz; float lat = cfgLat, lon = cfgLon;
  String pendingCity = "";
  String glat = web.arg("lat"), glon = web.arg("lon");
  String cityname = web.arg("cityname"); cityname.trim();
  String city = web.arg("city");
  if (cityname.length()) {
    pendingCity = cityname; loc = cityname; tz = "auto";          // typed city -> lat/long resolved at boot
  } else if (city.length() && city != "custom") {
    int i = city.toInt();
    if (i >= 0 && i < REGION_NCITIES) {
      loc = REGION_CITIES[i].name; lat = REGION_CITIES[i].lat;
      lon = REGION_CITIES[i].lon;  tz  = REGION_CITIES[i].tz;
    }
  } else if (city == "custom") {
    loc = web.arg("loc"); if (!loc.length()) loc = "Custom";
    lat = glat.toFloat(); lon = glon.toFloat();
    int z = web.arg("tz").toInt();
    if (z >= 0 && z < REGION_NZONES) tz = REGION_ZONES[z].tz;
  }
  prefs.begin("dash", false);
  prefs.putString("ssid", ssid); prefs.putString("pass", pass);
  prefs.putString("loc",  loc);  prefs.putFloat("lat", lat);
  prefs.putFloat ("lon",  lon);  prefs.putString("tz",  tz);
  prefs.putString("city", pendingCity);
  prefs.putBool  ("ota",  web.hasArg("ota"));          // checkbox present == ticked
  prefs.putString("otapass", web.arg("otapass"));      // user-chosen; no default ever
  prefs.putBool  ("otashow", web.hasArg("otashow"));   // show it on the local screen?
  prefs.end();
  Serial.printf("saved: ssid=%s loc=%s lat=%.4f lon=%.4f tz=%s pending=%s ota=%d\n",
                ssid.c_str(), loc.c_str(), lat, lon, tz.c_str(), pendingCity.c_str(), web.hasArg("ota"));
  web.send(200, "text/html",
           F("<meta name=viewport content='width=device-width'><body style='font-family:system-ui;"
             "background:#000;color:#fc9;text-align:center;margin-top:60px'>"
             "<h2 style='color:#f90'>Saved &mdash; connecting&hellip;</h2>"
             "<p>You can close this page. The display will show the dashboard shortly.</p>"));
  delay(800);
  ESP.restart();
}

static void startPortal() {
  g_portal = true;
  WiFi.mode(WIFI_AP_STA);                                      // AP for the portal, STA so we can scan
  WiFi.softAP(apName.c_str());                                 // open AP — easy to join
  dns.start(53, "*", WiFi.softAPIP());                         // captive: every lookup -> us
  web.onNotFound([] { web.send(200, "text/html", formPage()); });
  web.on("/", [] { web.send(200, "text/html", formPage()); });
  web.on("/save", HTTP_POST, handleSave);
  web.begin();
  screenSetup();                                               // tell the user what to do, on-screen
}

// ============================== OTA (optional wireless updates) ==============================
// An LCARS screen that shows the OTA host + password on the panel (opt-in via "show on display").
// Only whoever is physically at the device can read it.
static void screenOTAInfo() {
  lcChrome("OTA ENABLED");
  const int P0 = 88, PH = 52, PG = 4;
  lcarsPip(P0 + 0 * (PH + PG), PH, 2, "UPDATES", "ON");
  lcarsPip(P0 + 1 * (PH + PG), PH, 3, "NET PORT", "3232");
  String lines[] = { "Wireless firmware", "updates are ON.", "",
                     "Host: " + apName, "Password:", "   " + cfgOtaPass };
  lcBody(lines, 6);
  lcFooter("Arduino IDE -> Port -> network port. Shown here for you only.");
  lcFlush();
}

// Bring up ArduinoOTA iff enrollment enabled it AND set a password (never a default/blank password).
static void beginOTA() {
  if (!cfgOta || !cfgOtaPass.length()) return;
  ArduinoOTA.setHostname(apName.c_str());
  ArduinoOTA.setPassword(cfgOtaPass.c_str());
  // Keep these callbacks LIGHT — serial only. A blocking ~4s e-paper full refresh here stalls the OTA
  // data phase long enough for the uploader (espota) to time out mid-transfer. So the dashboard just
  // freezes during an update and the device reboots into the new image when it completes.
  ArduinoOTA.onStart([] { Serial.println("OTA: update starting"); });
  ArduinoOTA.onEnd  ([] { Serial.println("OTA: complete, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA: error %u\n", e); });
  ArduinoOTA.begin();
  g_ota = true;
  // Print the OTA password to serial on every boot. Anyone with the serial console already has
  // physical/USB access (they can reflash at will), so hiding it only locks out the legitimate
  // owner who just wants the credential back — give up the goods rather than make them break in.
  Serial.printf("OTA enabled: host=%s port=3232 password=%s\n", apName.c_str(), cfgOtaPass.c_str());
}

static void draw(const struct tm& t, bool full) {
  if (io.readSHT4x(inT, inH)) {} else { inT = NAN; inH = NAN; }
  if (style == 1) renderMac(t); else if (style == 2) renderConsole(t); else renderLCARS(t);
  memcpy(epd.buffer(), cv.getBuffer(), (size_t)PANEL_W * PANEL_H);   // canvas levels -> panel buffer
  if (full || style != 0) {
    epd.displayFull();                                              // crisp 4-gray full page
  } else {
    // LCARS off-minute: quick 1-bit partials — only the data values that CHANGED, plus the clock,
    // each centered about its text so the refresh border sits symmetrically. No full-page flash.
    // All windows are white-on-black only, so the B&W partial projection never blackens any gray.
    for (int i = 0; i < 4; i++) if (lcValDirty[i])             // skip values that didn't change
      epd.partial(lcVal[i][0], lcVal[i][1], lcVal[i][2], lcVal[i][3]);
    epd.partial(lcClk[0], lcClk[1], lcClk[2], lcClk[3]);       // clock refreshes every minute
  }
}

void setup() {
  Serial.begin(115200); delay(200);
  io.begin();
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); while (true) delay(1000); }

  uint8_t mac[6]; WiFi.macAddress(mac);
  apName = "reTerminal-" + String(mac[4], HEX) + String(mac[5], HEX);
  loadConfig();
  bool forceSetup = (digitalRead(PIN_BTN_REFRESH) == LOW);     // hold Refresh at boot to re-onboard

  if (!cfgSsid.length() || forceSetup) { startPortal(); return; }   // no creds / forced -> onboarding

  screenConnecting(cfgSsid);
  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);       // scan every channel, then...
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);   // ...join the STRONGEST AP for this SSID, not just the
                                                   // first one found. On mesh/multi-AP networks the default
                                                   // fast-scan can latch onto a weak node — a poor link still
                                                   // passes ping/weather but starves a sustained OTA transfer.
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  if (WiFi.status() != WL_CONNECTED) { screenFailed(cfgSsid); delay(1500); startPortal(); return; }

  resolveLocation();                                          // geocode a typed city / resolve "auto" TZ (online)
  const char* tzApply = (cfgTz == "auto") ? "UTC0" : cfgTz.c_str();
  configTzTime(tzApply, NTP_SERVER, "time.nist.gov");         // kick off NTP (non-blocking)

  // Fetch weather BEFORE the first paint. A full refresh is ~4s and blocks, so painting first would
  // strand the weather fetch behind it (that was the temp/humidity lag). The SHT4x indoor read runs
  // inside draw(), so the very first paint already carries outdoor weather AND indoor temp/hum —
  // only the clock waits on NTP, showing "--:--" until it syncs.
  fetchWeather(); lastWeather = millis();
  beginOTA();                                                // optional wireless updates (quick to start)
  if (g_ota && cfgOtaShow) { screenOTAInfo(); delay(4500); } // opt-in: flash the OTA password on the panel

  struct tm t0; bool have0 = getLocalTime(&t0, 0);
  draw(t0, true);                                            // dashboard on screen NOW: data present, clock "--:--"
  lastMin = have0 ? t0.tm_min : -1;                           // -1 => loop forces a full paint once NTP lands
  if (!have0) { struct tm t; if (getLocalTime(&t, 8000)) { draw(t, true); lastMin = t.tm_min; } }   // clock once synced
}

void loop() {
  if (g_portal) { dns.processNextRequest(); web.handleClient(); return; }   // onboarding: just serve the portal
  if (g_ota) ArduinoOTA.handle();                                           // service wireless updates, if enabled
  bool change = false, forceFull = false;
  if (io.leftPressed())  { style = (style + 2) % 3; change = true; forceFull = true; }
  if (io.rightPressed()) { style = (style + 1) % 3; change = true; forceFull = true; }
  if (io.refreshPressed()) { change = true; forceFull = true; }
  struct tm t;
  if (!getLocalTime(&t, 500)) { delay(50); return; }
  if (millis() - lastWeather >= WEATHER_EVERY_MS) { fetchWeather(); lastWeather = millis(); }
  if (change || t.tm_min != lastMin) {
    // LCARS: quick partials every minute, clean full page every 10 min (and on style/refresh).
    // The very first paint MUST be full: if setup() couldn't draw (NTP not synced within its 8s
    // window, common right after onboarding), the panel still shows the CONNECTING screen — a
    // partial would only patch the value/clock boxes and leave that chrome behind.
    bool firstPaint = (lastMin == -1);
    draw(t, forceFull || firstPaint || (t.tm_min % 10 == 0));
    lastMin = t.tm_min;
  }
  delay(50);
}
