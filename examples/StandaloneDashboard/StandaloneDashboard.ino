// StandaloneDashboard — a self-contained wall dashboard, no server required.
//
// Shows an NTP clock + date, the local outdoor temperature & weather code from the free
// Open-Meteo API (no key), and the indoor temp/humidity from the on-board SHT4x. Renders into
// an Adafruit_GFX 1-bit canvas, copies it to the mono framebuffer, and uses the panel's fast
// PARTIAL refresh every minute (full 4-gray refresh every 10 min to clear ghosting).
//
// Fill in WIFI_SSID/WIFI_PASS and your LATITUDE/LONGITUDE. Needs the Adafruit GFX library.
// Board: XIAO_ESP32S3, OPI PSRAM, USB CDC On Boot = Enabled.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
#include <ReTerminalPeripherals.h>
using namespace ReTerminal;

// ---- configure me ----
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const float LATITUDE  = 43.6532f;      // your location (default: downtown Toronto)
const float LONGITUDE = -79.3832f;
const char* TZ        = "EST5EDT,M3.2.0,M11.1.0";   // POSIX TZ string
const long  WEATHER_EVERY_MS = 600000; // refresh outdoor weather every 10 min

Mono epd;
Peripherals io;
GFXcanvas1 canvas(PANEL_W, PANEL_H);   // 1-bit (black/white) drawing surface
float outTemp = NAN; int wcode = -1; uint32_t lastWeather = 0; int minuteShown = -1;

static const char* weatherText(int c) {
  if (c <= 0) return "Clear";
  if (c <= 3) return "Partly cloudy";
  if (c <= 48) return "Fog";
  if (c <= 67) return "Rain";
  if (c <= 77) return "Snow";
  if (c <= 82) return "Showers";
  return "Storm";
}

static void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(LATITUDE, 4) +
               "&longitude=" + String(LONGITUDE, 4) + "&current=temperature_2m,weather_code";
  if (!http.begin(client, url)) return;
  if (http.GET() == 200) {
    String b = http.getString();
    int ti = b.indexOf("\"temperature_2m\":");
    int wi = b.indexOf("\"weather_code\":");
    if (ti >= 0) outTemp = b.substring(ti + 17).toFloat();
    if (wi >= 0) wcode  = b.substring(wi + 15).toInt();
  }
  http.end();
}

static void render(const struct tm& t) {
  canvas.fillScreen(0);                                  // 0 = white (we map drawn=black)
  canvas.setTextColor(1);
  // clock (centered)
  char hm[8]; strftime(hm, sizeof(hm), "%H:%M", &t);
  canvas.setTextSize(12);
  int16_t bx, by; uint16_t bw, bh; canvas.getTextBounds(hm, 0, 0, &bx, &by, &bw, &bh);
  canvas.setCursor((PANEL_W - bw) / 2, 40); canvas.print(hm);
  // date
  canvas.setTextSize(3); canvas.setCursor(28, 230);
  char d[32]; strftime(d, sizeof(d), "%A, %d %B %Y", &t); canvas.print(d);
  canvas.drawFastHLine(24, 300, PANEL_W - 48, 1);
  // outdoor
  canvas.setTextSize(3); canvas.setCursor(24, 330); canvas.print("Outdoor");
  canvas.setTextSize(6); canvas.setCursor(24, 365);
  if (!isnan(outTemp)) { canvas.print(outTemp, 1); canvas.print(" C"); } else canvas.print("--");
  canvas.setTextSize(2); canvas.setCursor(24, 440); canvas.print(weatherText(wcode));
  // indoor (SHT4x)
  float it, ih;
  canvas.setTextSize(3); canvas.setCursor(470, 330); canvas.print("Indoor");
  canvas.setTextSize(6); canvas.setCursor(470, 365);
  if (io.readSHT4x(it, ih)) { canvas.print(it, 1); canvas.print(" C"); }
  else canvas.print("--");
  canvas.setTextSize(3); canvas.setCursor(470, 440);
  if (io.readSHT4x(it, ih)) { canvas.print((int)ih); canvas.print("% RH"); }
  // copy 1-bit canvas -> 4-gray framebuffer (drawn bit -> black(0), else white(3))
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
  configTzTime(TZ, "pool.ntp.org", "time.nist.gov");
  fetchWeather(); lastWeather = millis();
  struct tm t; if (getLocalTime(&t, 8000)) { render(t); epd.displayFull(); minuteShown = t.tm_min; }
}

void loop() {
  struct tm t;
  if (!getLocalTime(&t, 1000)) { delay(500); return; }
  if (millis() - lastWeather >= (uint32_t)WEATHER_EVERY_MS) { fetchWeather(); lastWeather = millis(); }
  if (t.tm_min != minuteShown) {                          // once a minute
    render(t);
    if (t.tm_min % 10 == 0) epd.displayFull();             // anti-ghost full refresh every 10 min
    else epd.refreshChanged();                             // else fast partial of what changed
    minuteShown = t.tm_min;
  }
  delay(500);
}
