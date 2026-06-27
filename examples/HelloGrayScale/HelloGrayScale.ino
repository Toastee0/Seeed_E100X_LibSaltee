// HelloGrayScale — Seeed reTerminal E1001 quickstart.
//
// The E1001 is the "mono" (no-colour) panel; this driver renders gray levels via custom LUTs (shown
// as the swatch strip up top) and — its party trick — a FAST 1-bit PARTIAL refresh. To show that off,
// a little SD card bounces around DVD-screensaver style: each step we redraw only the union box of the
// card's old + new position, so the rest of the panel never flickers.
//
// (Driver class is ReTerminal::Mono — "mono" = the non-colour panel, paired with the E1002 colour
// driver — but its output is grayscale. Note: with the stock LUTs the two mid-grays are faint on this
// panel; crisp 4-level separation needs waveform tuning.)
//
// Board: "XIAO_ESP32S3", PSRAM = "OPI PSRAM", "USB CDC On Boot = Enabled".
#include <Adafruit_GFX.h>
#include <ReTerminalMono.h>
using namespace ReTerminal;

Mono epd;
GFXcanvas1 canvas(PANEL_W, PANEL_H);

const int SW_W = (PANEL_W - 40) / 4, SW_Y = 80, SW_H = 120;      // gray swatch strip (tall, not squished)
const int BAX = 16, BAY = 224, BAW = PANEL_W - 32, BAH = PANEL_H - BAY - 14;   // bounce arena below it
const int CW = 58, CH = 74;                                      // SD-card size
int cx = 60, cy = 260, vx = 17, vy = 13;                         // card position + velocity

static void fbFill(int x, int y, int w, int h, uint8_t g) {
  uint8_t* fb = epd.buffer();
  for (int yy = y; yy < y + h && yy < PANEL_H; yy++)
    for (int xx = x; xx < x + w && xx < PANEL_W; xx++) if (xx >= 0 && yy >= 0) fb[yy * PANEL_W + xx] = g;
}
static void blit(int x, int y, int w, int h) {
  uint8_t* fb = epd.buffer();
  for (int yy = y; yy < y + h && yy < PANEL_H; yy++)
    for (int xx = x; xx < x + w && xx < PANEL_W; xx++) if (xx >= 0 && yy >= 0) fb[yy * PANEL_W + xx] = canvas.getPixel(xx, yy) ? 0 : 3;
}

// SD-card silhouette into the canvas (1 = black body, 0 = white detail): chamfered top-right + "SD".
static void drawCard(int x, int y) {
  canvas.fillRect(x, y, CW, CH, 1);                              // body
  for (int i = 0; i < 16; i++) for (int j = 0; j < 16 - i; j++) canvas.drawPixel(x + CW - 1 - j, y + i, 0);  // chamfer
  for (int k = 0; k < 4; k++) canvas.fillRect(x + 8 + k * 11, y + 6, 6, 14, 0);   // white "contacts"
  canvas.setTextColor(0); canvas.setTextSize(2); canvas.setCursor(x + 12, y + 44); canvas.print("SD");
}

static void drawBase() {
  uint8_t* fb = epd.buffer();
  for (int i = 0; i < PANEL_W * PANEL_H; i++) fb[i] = 3;
  canvas.fillScreen(0); canvas.setTextColor(1);
  canvas.setTextSize(3); canvas.setCursor(16, 12); canvas.print("reTerminal E1001");
  canvas.setTextSize(2); canvas.setCursor(16, 56); canvas.print("grayscale + bouncing-SD partial refresh");
  const char* lbl[4] = {"BLACK 0", "DARK 1", "LIGHT 2", "WHITE 3"};
  for (int k = 0; k < 4; k++) { int x = 20 + k * SW_W; canvas.drawRect(x, SW_Y, SW_W - 8, SW_H, 1);
    canvas.setTextSize(1); canvas.setCursor(x + 4, SW_Y + SW_H + 4); canvas.print(lbl[k]); }
  canvas.drawRect(BAX, BAY, BAW, BAH, 1);                        // bounce-arena border
  for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) if (canvas.getPixel(x, y)) fb[y * PANEL_W + x] = 0;
  for (int k = 0; k < 4; k++) fbFill(20 + k * SW_W + 2, SW_Y + 2, SW_W - 12, SW_H - 4, (uint8_t)k);  // gray fills
}

void setup() {
  Serial.begin(115200); delay(200);
  if (!epd.begin()) { Serial.println("PSRAM alloc failed — enable OPI PSRAM"); while (true) delay(1000); }
  drawBase();
  epd.displayFull();
  drawCard(cx, cy); blit(cx, cy, CW, CH); epd.partial(cx, cy, CW, CH);
}

void loop() {
  int nx = cx + vx, ny = cy + vy;                               // DVD-bounce off the arena walls
  if (nx < BAX + 1)            { nx = BAX + 1;            vx = -vx; }
  if (nx + CW > BAX + BAW - 1) { nx = BAX + BAW - 1 - CW; vx = -vx; }
  if (ny < BAY + 1)            { ny = BAY + 1;            vy = -vy; }
  if (ny + CH > BAY + BAH - 1) { ny = BAY + BAH - 1 - CH; vy = -vy; }

  // union box of old + new position -> erase old, draw new, refresh just that box
  int ux = (cx < nx ? cx : nx) - 1, uy = (cy < ny ? cy : ny) - 1;
  int ux2 = (cx > nx ? cx : nx) + CW + 1, uy2 = (cy > ny ? cy : ny) + CH + 1;
  if (ux < BAX + 1) ux = BAX + 1; if (uy < BAY + 1) uy = BAY + 1;
  if (ux2 > BAX + BAW - 1) ux2 = BAX + BAW - 1; if (uy2 > BAY + BAH - 1) uy2 = BAY + BAH - 1;
  canvas.fillRect(ux, uy, ux2 - ux, uy2 - uy, 0);               // clear union (white)
  drawCard(nx, ny);
  blit(ux, uy, ux2 - ux, uy2 - uy);
  epd.partial(ux, uy, ux2 - ux, uy2 - uy);
  cx = nx; cy = ny;
  delay(200);
}
