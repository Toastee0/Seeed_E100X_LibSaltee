// DashboardsColor — a colour dashboard for the Seeed reTerminal E1002 (7.3" Spectra-6, 6 inks).
//
// This is the COLOUR sibling of the mono `Dashboards` example. Colour e-paper is full-refresh only
// (~22-34 s, no partial), so the design leans into colour and an ANALOG clock — which reads as
// naturally approximate, so an infrequent refresh looks intentional rather than laggy. Temperature
// is mapped to ink (blue=cold .. red=hot), the weather gets a coloured icon, humidity is blue.
//
// NOTE: this is the VISUAL DRAFT — sample data, one render in setup(). WiFi onboarding + NTP +
// Open-Meteo + SHT4x get wired in next, reusing the mono Dashboards flow. Full-refresh only.
//
// Requires the GxEPD2 library. Board: XIAO_ESP32S3, OPI PSRAM, USB CDC On Boot = Enabled.
#include <GxEPD2_7C.h>
#include <ReTerminalEpaper.h>
#include <ReTerminalPeripherals.h>
using namespace ReTerminal;

RETERMINAL_COLOR_DISPLAY(display);
Peripherals io;

// ---- sample data (stand-in until WiFi/NTP/weather/SHT4x are wired in) ----
const char* LOC = "Kitchener";
const char* DATESTR = "Fri 27 Jun 2026";
float outT = 26, outH = 43;  int wcode = 0;     // 0 = clear
float inT  = 27, inH  = 43;
int hh = 17, mm = 54, batt = 100;

// Map a temperature (C) to one of the six inks — cold blue .. hot red.
static uint16_t tempColor(float t) {
  if (isnan(t)) return GxEPD_BLACK;
  if (t <  2)  return GxEPD_BLUE;
  if (t < 13)  return GxEPD_GREEN;
  if (t < 23)  return GxEPD_BLACK;     // mild = neutral ink so it stays legible
  if (t < 29)  return GxEPD_YELLOW;
  return GxEPD_RED;
}
static const char* weatherText(int c) {
  if (c == 0) return "Clear"; if (c <= 3) return "Cloudy"; if (c <= 48) return "Fog";
  if (c <= 67) return "Rain"; if (c <= 77) return "Snow"; if (c <= 82) return "Showers";
  if (c <= 99) return "Storm"; return "";
}

// ---- text helpers ----
static int tw(uint8_t size, const char* s) { return strlen(s) * 6 * size; }
static void text(int x, int y, uint8_t size, uint16_t col, const char* s) {
  display.setTextSize(size); display.setTextColor(col); display.setCursor(x, y); display.print(s);
}
static void textC(int cx, int y, uint8_t size, uint16_t col, const char* s) { text(cx - tw(size, s) / 2, y, size, col, s); }

// ---- analog clock ----
// A tapered hand drawn as a triangle from a base segment across the hub to the tip.
static void hand(int cx, int cy, int len, float deg, int halfW, int back, uint16_t col) {
  float th = deg * PI / 180.0f, dx = sinf(th), dy = -cosf(th), px = cosf(th), py = sinf(th);
  int tx = cx + dx * len, ty = cy + dy * len;
  int b1x = cx + px * halfW - dx * back, b1y = cy + py * halfW - dy * back;
  int b2x = cx - px * halfW - dx * back, b2y = cy - py * halfW - dy * back;
  display.fillTriangle(b1x, b1y, b2x, b2y, tx, ty, col);
}
static void clock(int cx, int cy, int r, int H, int M) {
  display.fillCircle(cx, cy, r, GxEPD_WHITE);
  for (int t = 0; t < 4; t++) display.drawCircle(cx, cy, r - t, GxEPD_BLACK);   // thick black rim
  display.drawCircle(cx, cy, r - 8, GxEPD_BLUE);                                // inner colour ring
  for (int i = 0; i < 12; i++) {                                               // hour ticks (quarters in red)
    float th = i * 30 * PI / 180.0f, dx = sinf(th), dy = -cosf(th);
    int r1 = r - 14, r2 = (i % 3 == 0) ? r - 32 : r - 24;
    uint16_t tc = (i % 3 == 0) ? GxEPD_RED : GxEPD_BLACK;
    for (int o = -2; o <= 2; o++) {                                            // thicken
      int ox = (int)(dy * o), oy = (int)(-dx * o);
      display.drawLine(cx + dx * r1 + ox, cy + dy * r1 + oy, cx + dx * r2 + ox, cy + dy * r2 + oy, tc);
    }
  }
  hand(cx, cy, r * 0.52f, (H % 12 + M / 60.0f) * 30.0f, 9, 16, GxEPD_RED);     // hour
  hand(cx, cy, r * 0.80f, M * 6.0f,                     6, 18, GxEPD_BLUE);    // minute
  display.fillCircle(cx, cy, 11, GxEPD_BLACK);
  display.fillCircle(cx, cy, 5,  GxEPD_RED);
}

// ---- weather icon (coloured, ~r px) ----
static void weatherIcon(int cx, int cy, int r, int code) {
  const char* w = weatherText(code);
  bool cloud = (code >= 1 && code <= 48) || (code >= 51);
  if (code == 0) {                                                            // clear -> sun
    for (int i = 0; i < 12; i++) { float th = i * 30 * PI / 180.0f;
      for (int o = -1; o <= 1; o++) display.drawLine(cx + cosf(th) * (r + 4), cy + sinf(th) * (r + 4) + o,
                                                     cx + cosf(th) * (r + 16), cy + sinf(th) * (r + 16) + o, GxEPD_YELLOW); }
    display.fillCircle(cx, cy, r, GxEPD_YELLOW);
    return;
  }
  if (cloud) {                                                               // cloud body (white w/ black outline)
    display.fillCircle(cx - r / 2, cy, r / 2, GxEPD_WHITE); display.drawCircle(cx - r / 2, cy, r / 2, GxEPD_BLACK);
    display.fillCircle(cx + r / 2, cy, r / 2, GxEPD_WHITE); display.drawCircle(cx + r / 2, cy, r / 2, GxEPD_BLACK);
    display.fillCircle(cx, cy - r / 3, r * 2 / 3, GxEPD_WHITE); display.drawCircle(cx, cy - r / 3, r * 2 / 3, GxEPD_BLACK);
    display.fillRect(cx - r / 2, cy, r, r / 2, GxEPD_WHITE);
    bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    bool snow = (code >= 71 && code <= 77);
    bool storm = (code >= 95);
    if (rain)  for (int i = -1; i <= 1; i++) display.drawLine(cx + i * (r / 2), cy + r / 2, cx + i * (r / 2) - 6, cy + r, GxEPD_BLUE);
    if (snow)  for (int i = -1; i <= 1; i++) display.fillCircle(cx + i * (r / 2), cy + r / 2 + 8, 3, GxEPD_BLUE);
    if (storm) display.fillTriangle(cx - 6, cy + r / 2, cx + 10, cy + r / 2, cx - 2, cy + r, GxEPD_RED);
  }
}

static void render() {
  display.fillScreen(GxEPD_WHITE);
  // header bar
  display.fillRect(0, 0, PANEL_W, 58, GxEPD_BLUE);
  display.setTextSize(3); display.setTextColor(GxEPD_WHITE);
  display.setCursor(16, 18); display.print(LOC);
  text(PANEL_W - tw(2, DATESTR) - 16, 22, 2, GxEPD_WHITE, DATESTR);

  // analog clock (left) — vertically centered in the band between the header (58) and footer (440)
  clock(232, 248, 188, hh, mm);

  // right column: outdoor + indoor cards
  const int X = 452, W = PANEL_W - X - 12;
  // outdoor card
  display.drawRect(X, 74, W, 200, GxEPD_BLACK); display.drawRect(X + 1, 75, W - 2, 198, GxEPD_BLACK);
  text(X + 16, 86, 2, GxEPD_BLACK, "OUTDOOR");
  weatherIcon(X + 60, 165, 34, wcode);
  { char b[16]; snprintf(b, sizeof b, "%d", (int)lroundf(outT));
    text(X + W - tw(7, b) - 50, 120, 7, tempColor(outT), b);
    text(X + W - 44, 120, 4, tempColor(outT), "C"); }
  textC(X + 65, 230, 2, GxEPD_BLACK, weatherText(wcode));
  { char b[16]; snprintf(b, sizeof b, "%d%% RH", (int)lroundf(outH)); text(X + W - tw(2, b) - 16, 240, 2, GxEPD_BLUE, b); }

  // indoor card
  display.drawRect(X, 286, W, 144, GxEPD_BLACK); display.drawRect(X + 1, 287, W - 2, 142, GxEPD_BLACK);
  text(X + 16, 298, 2, GxEPD_BLACK, "INDOOR");
  { char b[16]; snprintf(b, sizeof b, "%d", (int)lroundf(inT));
    text(X + 20, 330, 7, tempColor(inT), b);
    text(X + 20 + tw(7, b), 330, 4, tempColor(inT), "C"); }
  { char b[16]; snprintf(b, sizeof b, "%d%% RH", (int)lroundf(inH)); text(X + W - tw(2, b) - 16, 398, 2, GxEPD_BLUE, b); }

  // footer: battery (coloured) + wifi placeholder
  uint16_t bc = batt > 50 ? GxEPD_GREEN : batt > 20 ? GxEPD_YELLOW : GxEPD_RED;
  display.fillRect(0, PANEL_H - 40, PANEL_W, 40, GxEPD_BLACK);
  { char b[24]; snprintf(b, sizeof b, "BATT %d%%", batt); text(16, PANEL_H - 30, 2, bc, b); }
  text(PANEL_W - tw(2, "WiFi: --") - 16, PANEL_H - 30, 2, GxEPD_WHITE, "WiFi: --");
}

void setup() {
  Serial.begin(115200); delay(200);
  io.begin();
  display.init(115200);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do { render(); } while (display.nextPage());
  display.hibernate();
  Serial.println("colour dashboard rendered");
}

void loop() {}
