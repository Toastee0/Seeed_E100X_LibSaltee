// HelloMono — Seeed reTerminal E1001 (mono) quickstart, no external libraries.
//
// Shows the two things this panel does well: a 4-level GRAYSCALE full refresh, then a fast
// 1-bit PARTIAL refresh of a small region (a block that steps across the screen ~every 2 s
// without redrawing the rest).
//
// Board: "XIAO_ESP32S3", with PSRAM = "OPI PSRAM" and "USB CDC On Boot = Enabled" set so the
// 800x480 framebuffers fit and Serial reaches the on-board CH340.
#include <ReTerminalMono.h>

ReTerminal::Mono epd;
int blockX = 0;

// Paint a horizontal greyscale gradient + labels directly into the working buffer (0..3).
static void drawGradient() {
  uint8_t* fb = epd.buffer();
  const int W = epd.width(), H = epd.height();
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      fb[y * W + x] = (uint8_t)(x * 4 / W);          // 0,1,2,3 left->right (black->white)
  // a white strip across the top to hold the moving block (gray 3 = white)
  for (int y = 0; y < 80; y++)
    for (int x = 0; x < W; x++) fb[y * W + x] = 3;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); while (true) delay(1000); }
  drawGradient();
  epd.displayFull();                                  // ~4.2 s, crisp 4-level gray
  Serial.println("full gray refresh done");
}

void loop() {
  // Move a 64x64 black block one step along the white top strip, partial-refreshing only the
  // strip it touches. Each update is ~0.4 s vs ~4 s for a full refresh.
  uint8_t* fb = epd.buffer();
  const int W = epd.width();
  for (int y = 8; y < 72; y++) for (int x = 0; x < W; x++) fb[y * W + x] = 3;  // clear strip
  for (int y = 8; y < 72; y++) for (int x = blockX; x < blockX + 64 && x < W; x++) fb[y * W + x] = 0;
  epd.partial(0, 8, W, 64);                           // refresh just the strip
  blockX = (blockX + 80) % (W - 64);
  delay(2000);
}
