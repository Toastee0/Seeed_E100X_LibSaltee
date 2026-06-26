// WiFiSetup — provision WiFi with NO code editing and NO serial console.
//
// The whole point: a non-technical person can set this up. On first boot (or if the saved
// network is gone) the device becomes its OWN WiFi hotspot and shows, ON ITS SCREEN, a QR to
// join that hotspot plus the address to open. You join, a web page lists nearby networks, you
// pick yours and type the password, and the device saves it and reconnects — everything you
// need to know appears on the e-paper, never on a serial port.
//
// Flash it once (see docs/SETUP.md for a no-Arduino-IDE / browser option). After that it just
// works; to change networks, hold the Refresh button at power-on to force setup mode again.
//
// Optional OTA: the setup form has a checkbox + a password field. Tick it and set YOUR OWN
// password to leave the device updatable over WiFi (Arduino IDE network port / espota) — so the
// USB cable is only ever needed for the very first flash. No password = no OTA; there is no default.
//
// Libraries: Adafruit GFX (Library Manager). Everything else (WiFi, WebServer, DNSServer,
// Preferences, QR encoder) is built into the ESP32 core. Board: XIAO_ESP32S3, OPI PSRAM,
// USB CDC On Boot = Enabled.
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
#include <ReTerminalQR.h>
#include <ReTerminalPeripherals.h>
using namespace ReTerminal;

Mono epd;
Peripherals io;
GFXcanvas1 canvas(PANEL_W, PANEL_H);
Preferences prefs;
WebServer web(80);
DNSServer dns;
String apName;
bool g_ota = false;          // ArduinoOTA running — enabled at enrollment WITH a user-set password

// ---- branded header drawn on every screen: product name + Seeed Studio + a rule ----
static void brandHeader() {
  canvas.setTextSize(3); canvas.setCursor(20, 12); canvas.print("reTerminal E1001");
  canvas.setTextSize(2); canvas.setCursor(596, 18); canvas.print("Seeed Studio");
  canvas.drawFastHLine(20, 46, PANEL_W - 40, 2);
}

// ---- draw a status screen: a big title, some body lines, and an optional QR on the right ----
static void screen(const String& title, const String* lines, int nlines,
                   const char* qr = nullptr, const char* footer = nullptr) {
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  brandHeader();                                         // "reTerminal E1001 ... Seeed Studio" + rule
  canvas.setTextSize(4); canvas.setCursor(30, 62); canvas.print(title);
  canvas.setTextSize(3);
  for (int i = 0; i < nlines; i++) { canvas.setCursor(30, 118 + i * 40); canvas.print(lines[i]); }
  if (footer) { canvas.setTextSize(2); canvas.setCursor(20, 454); canvas.print(footer); }  // small note, full width
  uint8_t* fb = epd.buffer();
  for (int y = 0; y < PANEL_H; y++)
    for (int x = 0; x < PANEL_W; x++)
      fb[y * PANEL_W + x] = canvas.getPixel(x, y) ? 0 : 3;
  if (qr) ReTerminal::drawQR(fb, epd.width(), qr, 560, 112, 200, 200);   // compact, like Seeed's layout
  epd.displayFull();
}

// ---- captive-portal web pages ----
static String formPage() {
  String s = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<style>body{font-family:sans-serif;max-width:420px;margin:24px auto;padding:0 12px}"
               "select,input,button{width:100%;font-size:1.1em;padding:10px;margin:6px 0;box-sizing:border-box}"
               "input[type=checkbox]{width:auto;margin:0 8px 0 0} .chk{display:flex;align-items:center}"
               ".hint{color:#666;font-size:.85em;margin:0}</style>"
               "<h2>reTerminal WiFi setup</h2><form method=POST action=/save>"
               "<label>Network</label><select name=ssid>");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) s += "<option>" + WiFi.SSID(i) + "</option>";
  s += F("</select><label>Password</label><input name=pass type=password>"
         "<label class=chk><input type=checkbox name=ota value=1> Enable wireless updates (OTA)</label>"
         "<label>OTA password</label><input name=otapass type=text autocomplete=off>"
         "<label class=chk><input type=checkbox name=otashow value=1> Show OTA password on the display</label>"
         "<p class=hint>Choose your own &mdash; only used if OTA is enabled. There is no default password.</p>"
         "<button type=submit>Save &amp; connect</button></form>");
  return s;
}

static void startPortal() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName.c_str());                       // open AP, no password (easy to join)
  dns.start(53, "*", WiFi.softAPIP());               // captive: send every lookup to us
  web.onNotFound([] { web.send(200, "text/html", formPage()); });
  web.on("/", [] { web.send(200, "text/html", formPage()); });
  web.on("/save", HTTP_POST, [] {
    prefs.begin("wifi", false);
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    prefs.putBool("ota", web.hasArg("ota"));           // checkbox present == ticked
    prefs.putString("otapass", web.arg("otapass"));    // user-chosen; no default ever
    prefs.putBool("otashow", web.hasArg("otashow"));   // display the password on the local screen?
    prefs.end();
    web.send(200, "text/html", F("<meta name=viewport content='width=device-width'>"
                                 "<body style='font-family:sans-serif;text-align:center;margin-top:60px'>"
                                 "<h2>Saved &mdash; connecting&hellip;</h2><p>You can close this page.</p>"));
    delay(800);
    ESP.restart();
  });
  web.begin();

  // tell the user, on the screen, exactly what to do — with a QR to join this very hotspot
  char joinQR[64];
  ReTerminal::wifiQRPayload(joinQR, sizeof(joinQR), apName.c_str(), "", "nopass");
  String lines[] = {                                    // kept short so they clear the QR on the right
    "1. On your phone,",
    "   join the WiFi:",
    "   " + apName,
    "   (or scan ->)",
    "2. Open the page,",
    "   pick your WiFi.",
  };
  screen("WiFi setup", lines, 6, joinQR,
         "Once connected, this screen shows a QR to share the WiFi.");
}

void setup() {
  Serial.begin(115200);                              // optional — everything also shows on screen
  if (!epd.begin()) { while (true) delay(1000); }
  io.begin();
  uint8_t mac[6]; WiFi.macAddress(mac);
  apName = "reTerminal-" + String(mac[4], HEX) + String(mac[5], HEX);

  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
  prefs.end();
  bool forceSetup = (digitalRead(PIN_BTN_REFRESH) == LOW);   // hold Refresh at boot to re-provision

  if (ssid.length() && !forceSetup) {
    String l1[] = { "Network: " + ssid };
    screen("Connecting...", l1, 1);
    WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) { delay(250); }
    if (WiFi.status() == WL_CONNECTED) {
      prefs.begin("wifi", true);                       // bring up OTA only if enabled WITH a password
      bool otaOn = prefs.getBool("ota", false);
      bool otaShow = prefs.getBool("otashow", false);  // show the password on the (local-only) screen?
      String otaPass = prefs.getString("otapass", "");
      prefs.end();
      if (otaOn && otaPass.length()) {
        ArduinoOTA.setHostname(apName.c_str());
        ArduinoOTA.setPassword(otaPass.c_str());
        ArduinoOTA.begin();
        g_ota = true;
      }
      char joinQR[160];                                // share the network it just joined as a QR
      ReTerminal::wifiQRPayload(joinQR, sizeof(joinQR), WiFi.SSID().c_str(), WiFi.psk().c_str(), "WPA");
      // This screen is only visible to whoever's at the device — showing the password is opt-in.
      String otaNote = !g_ota  ? String("OTA DISABLED. Set during enrollment to enable.")
                     : otaShow ? ("OTA password: " + otaPass)
                               : String("OTA password: HIDDEN");
      String ok[] = { "Network: " + WiFi.SSID(), "IP: " + WiFi.localIP().toString(),
                      "", "Others can scan", "to join ->" };
      screen("Connected", ok, 5, joinQR, otaNote.c_str());
      return;                                          // your app would carry on from here
    }
    String fail[] = { "Could not join:", ssid, "Starting setup instead..." };
    screen("WiFi failed", fail, 3);
    delay(1500);
  }
  startPortal();                                       // no creds / failed / forced -> onboarding
}

void loop() {
  if (g_ota) { ArduinoOTA.handle(); return; }          // connected + OTA enabled: just service updates
  dns.processNextRequest();                            // else we're in the captive portal
  web.handleClient();
}
