// WiFiQR — show a scannable "join my WiFi" QR, generated ON-DEVICE from the credentials the
// device already knows. Point any phone camera at it and tap the prompt to join — no typing.
//
// The ESP32 keeps the station SSID and PSK, so we read them back with WiFi.SSID() / WiFi.psk()
// and encode WIFI:S:<ssid>;T:WPA;P:<psk>;; — nothing is hardcoded into the QR. Provide creds
// once below (or leave blank to reuse whatever was last saved to NVS); after that the device
// regenerates the QR from its own stored network. A QR is pure black/white, so the mono panel
// renders it crisp. QR encoding uses the ESP32 core's built-in encoder (no extra library);
// needs Adafruit GFX for the text. Board: XIAO_ESP32S3, OPI PSRAM, USB CDC On Boot = Enabled.
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
#include <ReTerminalQR.h>
using namespace ReTerminal;

// First-time provisioning only — leave blank ("") to reuse the SSID/PSK already saved in NVS.
const char* PROVISION_SSID = "";
const char* PROVISION_PASS = "";

Mono epd;
GFXcanvas1 canvas(PANEL_W, PANEL_H);

static void show(const String& ssid, const String& psk, const String& note) {
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setTextSize(5); canvas.setCursor(40, 40);  canvas.print("Scan to join WiFi");
  canvas.setTextSize(3); canvas.setCursor(40, 120); canvas.print("Network: " + ssid);
  canvas.setTextSize(2); canvas.setCursor(40, 440); canvas.print(note);

  uint8_t* fb = epd.buffer();
  for (int y = 0; y < PANEL_H; y++)
    for (int x = 0; x < PANEL_W; x++)
      fb[y * PANEL_W + x] = canvas.getPixel(x, y) ? 0 : 3;

  if (psk.length()) {
    char payload[160];
    ReTerminal::wifiQRPayload(payload, sizeof(payload), ssid.c_str(), psk.c_str(), "WPA");
    int n = ReTerminal::drawQR(fb, epd.width(), payload, 380, 150, 380, 280);
    Serial.printf("QR: %s  (%d modules)\n", payload, n);
  }
  epd.displayFull();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); while (true) delay(1000); }

  WiFi.mode(WIFI_STA);
  if (strlen(PROVISION_SSID)) WiFi.begin(PROVISION_SSID, PROVISION_PASS);  // saves to NVS
  else                        WiFi.begin();                               // reuse saved creds
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }

  String ssid = WiFi.SSID(), psk = WiFi.psk();    // what the device actually knows
  if (!ssid.length()) ssid = "(none saved)";
  bool ok = WiFi.status() == WL_CONNECTED;
  Serial.printf("\nssid=%s connected=%d\n", ssid.c_str(), ok);

  show(ssid, psk,
       psk.length() ? "Point your phone camera here, then tap the prompt."
                    : "No saved WiFi — set PROVISION_SSID/PASS once and reflash.");
}

void loop() {}
