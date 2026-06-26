// HelloColor — Seeed reTerminal E1002 (Spectra-6 colour) quickstart.
//
// Draws the six inks as bars plus live temp/humidity from the on-board SHT4x, using the normal
// Adafruit_GFX API on the GxEPD2 display. Colour e-paper is full-refresh only (~22-34 s).
//
// Requires the GxEPD2 library (Library Manager). Board: "XIAO_ESP32S3", PSRAM = "OPI PSRAM",
// "USB CDC On Boot = Enabled".
#include <GxEPD2_7C.h>          // pull GxEPD2 onto the include path (so the colour driver enables)
#include <ReTerminalEpaper.h>
using namespace ReTerminal;

RETERMINAL_COLOR_DISPLAY(display);   // GxEPD2 display wired to the board pins
Peripherals io;

void setup() {
  Serial.begin(115200);
  delay(200);
  io.begin();
  display.init(115200);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    const uint16_t bars[6] = { GxEPD_BLACK, GxEPD_RED, GxEPD_YELLOW, GxEPD_BLUE, GxEPD_GREEN, GxEPD_WHITE };
    for (int i = 0; i < 6; i++) display.fillRect(i * PANEL_W / 6, 0, PANEL_W / 6, 200, bars[i]);
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(4);
    display.setCursor(20, 250);
    display.print("reTerminal E1002");
    float t, h;
    if (io.readSHT4x(t, h)) {
      display.setTextSize(3);
      display.setCursor(20, 330);
      display.printf("%.1f C   %.0f %% RH", t, h);
    }
  } while (display.nextPage());
  display.hibernate();
  Serial.println("colour refresh done");
}

void loop() {}
