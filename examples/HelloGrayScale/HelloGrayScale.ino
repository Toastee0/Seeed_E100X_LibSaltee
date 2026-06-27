// HelloMono — Seeed reTerminal E1001 (mono) quickstart.
//
// Shows the two things this panel does well: a 4-level GRAYSCALE base (a gradient strip), and a
// fast 1-bit PARTIAL refresh — a block that steps across a white band every ~2 s without
// redrawing the rest. The title is drawn with Adafruit_GFX.
//
// Board: "XIAO_ESP32S3", PSRAM = "OPI PSRAM", "USB CDC On Boot = Enabled".
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>

ReTerminal::Mono epd;
GFXcanvas1 canvas(ReTerminal::PANEL_W, ReTerminal::PANEL_H);
int blockX = 0;
const int STRIP_Y = 120, STRIP_H = 44;       // white band the block moves along

static void drawBase() {
  uint8_t* fb = epd.buffer();
  const int W = epd.width(), H = epd.height();
  for (int y = 0; y < H; y++)                  // white top area, 4-gray gradient below the strip
    for (int x = 0; x < W; x++)
      fb[y * W + x] = (y > STRIP_Y + STRIP_H + 20) ? (uint8_t)(x * 4 / W) : 3;
  canvas.fillScreen(0); canvas.setTextColor(1);
  canvas.setTextSize(5); canvas.setCursor(20, 22); canvas.print("reTerminal E1001");
  canvas.setTextSize(2); canvas.setCursor(20, 82); canvas.print("4-level gray + fast partial refresh");
  for (int y = 0; y < STRIP_Y; y++)            // stamp the title (black) onto the buffer
    for (int x = 0; x < W; x++) if (canvas.getPixel(x, y)) fb[y * W + x] = 0;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); while (true) delay(1000); }
  drawBase();
  epd.displayFull();                           // ~4.2 s, crisp 4-level gray
}

void loop() {
  uint8_t* fb = epd.buffer();
  const int W = epd.width();
  for (int y = STRIP_Y; y < STRIP_Y + STRIP_H; y++) for (int x = 0; x < W; x++) fb[y * W + x] = 3;
  for (int y = STRIP_Y; y < STRIP_Y + STRIP_H; y++)
    for (int x = blockX; x < blockX + 64 && x < W; x++) fb[y * W + x] = 0;
  epd.partial(0, STRIP_Y, W, STRIP_H);         // ~0.4 s — only the strip
  blockX = (blockX + 80) % (W - 64);
  delay(2000);
}
