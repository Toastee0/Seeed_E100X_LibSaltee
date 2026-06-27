// ReTerminalMono.cpp — see ReTerminalMono.h. GPL-3.0-or-later.
// LUTs + UC8179 init/refresh sequences derive from Seeed_GxEPD2 (GPL).
#include "ReTerminalMono.h"

namespace ReTerminal {

// UC8179 4-gray LUTs (verbatim from Seeed_GxEPD2 GxEPD2_reTerminal_E1001_Gray4).
static const uint8_t LUT_VCOM[] = {
  0x00,0x00,0x06,0x08,0x07,0x01, 0x00,0x06,0x0A,0x0B,0x0A,0x01, 0x00,0x03,0x03,0x00,0x00,0x03,
  0x00,0x05,0x09,0x06,0x06,0x01, 0x00,0x02,0x02,0x0A,0x0A,0x01, 0x00,0x0A,0x11,0x06,0x07,0x01,
  0x00,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t LUT_WW[] = {
  0x15,0x00,0x06,0x08,0x07,0x01, 0x54,0x06,0x0A,0x0B,0x0A,0x01, 0x90,0x03,0x03,0x00,0x00,0x03,
  0x2A,0x05,0x09,0x06,0x06,0x01, 0xAA,0x02,0x02,0x0A,0x0A,0x01, 0x00,0x0A,0x11,0x06,0x07,0x01,
  0x28,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t LUT_KW[] = {
  0x2A,0x00,0x06,0x08,0x07,0x01, 0x59,0x06,0x0A,0x0B,0x0A,0x01, 0x90,0x03,0x03,0x00,0x00,0x03,
  0x5A,0x05,0x09,0x06,0x06,0x01, 0xA8,0x02,0x02,0x0A,0x0A,0x01, 0x45,0x0A,0x11,0x06,0x07,0x01,
  0xA8,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t LUT_WK[] = {
  0x16,0x00,0x06,0x08,0x07,0x01, 0xA0,0x06,0x0A,0x0B,0x0A,0x01, 0x90,0x03,0x03,0x00,0x00,0x03,
  0x99,0x05,0x09,0x06,0x06,0x01, 0xA0,0x02,0x02,0x0A,0x0A,0x01, 0x40,0x0A,0x11,0x06,0x07,0x01,
  0x20,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t LUT_KK[] = {
  0x26,0x00,0x06,0x08,0x07,0x01, 0x6A,0x06,0x0A,0x0B,0x0A,0x01, 0x90,0x03,0x03,0x00,0x00,0x03,
  0x65,0x05,0x09,0x06,0x06,0x01, 0x50,0x02,0x02,0x0A,0x0A,0x01, 0x10,0x0A,0x11,0x06,0x07,0x01,
  0x10,0x02,0x01,0x02,0x01,0x01,
};
static const uint8_t USER_GRAY[] = { 0x17, 0x3F, 0x3F, 0x07, 0x06, 0x12 };

bool Mono::begin() {
  pinMode(PIN_EPD_CS, OUTPUT);  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_RES, OUTPUT); pinMode(PIN_EPD_BUSY, INPUT);
  csHigh(); digitalWrite(PIN_EPD_DC, HIGH);
  // Deselect the on-board microSD: it sits on this same SPI bus, and if its CS floats it corrupts the
  // panel's data (notably the 4-gray bit-planes -> collapses to 2 levels). Keep it deselected + powered
  // down here; a sketch that wants SD re-enables it (drive SD_EN high) AFTER epd.begin().
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_SD_EN, OUTPUT); digitalWrite(PIN_SD_EN, LOW);
  _spi.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);  // MISO routed so the SD card can share this bus
  const size_t n = (size_t)PANEL_W * PANEL_H;
  _frame = (uint8_t*)ps_malloc(n);
  _disp  = (uint8_t*)ps_malloc(n);
  if (!_frame || !_disp) return false;     // need OPI PSRAM
  memset(_frame, 3, n);                    // white
  memset(_disp,  3, n);
  return true;
}

void Mono::checkBusy(uint32_t to) {
  delay(10); uint32_t t0 = millis();
  while (!digitalRead(PIN_EPD_BUSY)) { if (millis() - t0 > to) { Serial.println(F("[uc8179] BUSY timeout")); return; } delay(10); }
}
void Mono::cmd(uint8_t c) {
  _spi.beginTransaction(_set); digitalWrite(PIN_EPD_DC, LOW); csLow();
  _spi.transfer(c); csHigh(); digitalWrite(PIN_EPD_DC, HIGH); _spi.endTransaction();
}
void Mono::data(uint8_t d) {
  _spi.beginTransaction(_set); csLow(); _spi.transfer(d); csHigh(); _spi.endTransaction();
}
void Mono::writeLUT(uint8_t c, const uint8_t* lut, uint16_t len) {
  cmd(c); for (uint16_t i = 0; i < len; i++) data(lut[i]);
}

void Mono::initGrayMode() {
  digitalWrite(PIN_EPD_RES, LOW); delay(10); digitalWrite(PIN_EPD_RES, HIGH); delay(10); checkBusy();
  cmd(0x01); data(0x07); data(USER_GRAY[0]); data(USER_GRAY[1]); data(USER_GRAY[2]); data(USER_GRAY[3]);
  cmd(0x30); data(USER_GRAY[4]);
  cmd(0x82); data(USER_GRAY[5]);
  cmd(0x06); data(0x27); data(0x27); data(0x28); data(0x17);
  cmd(0x04); delay(100); checkBusy();                        // power on
  cmd(0x00); data(0x3F);                                     // panel setting (gray)
  cmd(0xE3); data(0x88);
  cmd(0x50); data(0x10); data(0x07);
  cmd(0x52); data(0x00);
  cmd(0x61); data(PANEL_W >> 8); data(PANEL_W & 0xFF); data(PANEL_H >> 8); data(PANEL_H & 0xFF);
  writeLUT(0x20, LUT_VCOM, sizeof(LUT_VCOM)); checkBusy();
  writeLUT(0x21, LUT_WW, sizeof(LUT_WW)); checkBusy();
  writeLUT(0x22, LUT_KW, sizeof(LUT_KW)); checkBusy();
  writeLUT(0x23, LUT_WK, sizeof(LUT_WK));
  writeLUT(0x24, LUT_KK, sizeof(LUT_KK));
  _mode = 1;
}

// B&W (OTP LUT) config for fast partial refresh — switches the panel out of gray mode.
void Mono::initBWMode() {
  cmd(0x00); data(0x1F);
  cmd(0x01); data(0x07); data(0x07); data(0x3F); data(0x3F); data(0x09);
  cmd(0x06); data(0x17); data(0x17); data(0x28); data(0x17);
  cmd(0x61); data(PANEL_W >> 8); data(PANEL_W & 0xFF); data(PANEL_H >> 8); data(PANEL_H & 0xFF);
  cmd(0x15); data(0x00);
  cmd(0x50); data(0x29); data(0x07);
  cmd(0x60); data(0x22);
  cmd(0xE3); data(0x22);
  cmd(0x04); delay(100); checkBusy();
  _mode = 2;
}

void Mono::setPartWin(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint16_t xe = (x + w - 1) | 0x0007, ye = y + h - 1; x &= 0xFFF8;
  cmd(0x90);
  data(x >> 8); data(x & 0xFF); data(xe >> 8); data(xe & 0xFF);
  data(y >> 8); data(y & 0xFF); data(ye >> 8); data(ye & 0xFF);
  data(0x01);
}

void Mono::streamWin(uint8_t c, const uint8_t* src, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  cmd(0x91); setPartWin(x, y, w, h); cmd(c);
  _spi.beginTransaction(_set); csLow();
  for (uint16_t row = y; row < y + h; row++) {
    const uint8_t* rp = src + (size_t)row * PANEL_W;
    for (uint16_t cc = x; cc < x + w; cc += 8) {
      uint8_t out = 0;
      for (int b = 0; b < 8; b++) if (bwbit(rp[cc + b])) out |= (0x80 >> b);
      _spi.transfer(out);
    }
  }
  csHigh(); _spi.endTransaction();
  cmd(0x92);
}

void Mono::uploadGray4() {
  for (int plane = 0; plane < 2; plane++) {                  // 0 -> DTM1 (low bit), 1 -> DTM2 (high bit)
    cmd(plane == 0 ? 0x10 : 0x13);
    _spi.beginTransaction(_set); csLow();
    for (int row = 0; row < PANEL_H; row++) {
      const uint8_t* rp = _frame + (size_t)row * PANEL_W;
      for (int col8 = 0; col8 < PANEL_W / 8; col8++) {
        uint8_t out = 0;
        for (int bit = 0; bit < 8; bit++) {
          uint8_t inv = 3 - (rp[col8 * 8 + bit] & 0x03);     // panel polarity is inverted
          if (inv & (plane == 0 ? 0x01 : 0x02)) out |= (0x80 >> bit);
        }
        _spi.transfer(out);
      }
    }
    csHigh(); _spi.endTransaction();
  }
}

void Mono::fill(uint8_t gray) { if (_frame) memset(_frame, gray & 3, (size_t)PANEL_W * PANEL_H); }

void Mono::displayFull() {
  initGrayMode();
  uploadGray4();
  cmd(0x12); delay(100); checkBusy();                        // refresh
  memcpy(_disp, _frame, (size_t)PANEL_W * PANEL_H);
  _partialCycles = 0;                                        // a full refresh clears accumulated ghosting
}

bool Mono::partial(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (w == 0 || h == 0 || x >= PANEL_W || y >= PANEL_H) return false;
  if (x + w > PANEL_W) w = PANEL_W - x;
  if (y + h > PANEL_H) h = PANEL_H - y;
  uint16_t x0 = x & 0xFFF8, x1 = (uint16_t)(x + w - 1) | 0x0007;
  uint16_t aw = x1 - x0 + 1;
  if (_mode != 2) initBWMode();
  streamWin(0x10, _disp,  x0, y, aw, h);                     // old plane
  streamWin(0x13, _frame, x0, y, aw, h);                     // new plane
  cmd(0x91); setPartWin(x0, y, aw, h);
  cmd(0xE0); data(0x02);                                     // CCSET TSFIX -> force-temperature
  cmd(0xE5); data(0x5A);                                     // TSSET 90C   -> ~0.4s instead of ~4s
  cmd(0x12); checkBusy(); cmd(0x92);
  for (uint16_t r = y; r < y + h; r++)                       // snapshot the refreshed window
    memcpy(_disp + (size_t)r * PANEL_W + x0, _frame + (size_t)r * PANEL_W + x0, aw);
  return true;
}

uint32_t Mono::refreshChanged(uint16_t headerRows) {
  // anti-ghost: once we've partialed enough times, the next visible change gets a clean full
  // refresh instead of more partials (which would keep accumulating ghosting).
  bool wantFull = _fullEvery && _partialCycles >= _fullEvery;
  uint32_t changed = 0;
  int y = headerRows;
  while (y < PANEL_H) {
    const uint8_t* a = _frame + (size_t)y * PANEL_W;
    const uint8_t* b = _disp  + (size_t)y * PANEL_W;
    int mn = PANEL_W, mx = -1;
    for (int x = 0; x < PANEL_W; x++) if (bwbit(a[x]) != bwbit(b[x])) { if (x < mn) mn = x; if (x > mx) mx = x; }
    if (mx < 0) { y++; continue; }                           // this row clean -> not a band start
    int y0 = y, gmn = mn, gmx = mx;                          // grow a band of consecutive dirty rows
    while (y < PANEL_H) {
      const uint8_t* aa = _frame + (size_t)y * PANEL_W;
      const uint8_t* bb = _disp  + (size_t)y * PANEL_W;
      int rmn = PANEL_W, rmx = -1;
      for (int x = 0; x < PANEL_W; x++) if (bwbit(aa[x]) != bwbit(bb[x])) { if (x < rmn) rmn = x; if (x > rmx) rmx = x; }
      if (rmx < 0) break;
      if (rmn < gmn) gmn = rmn; if (rmx > gmx) gmx = rmx; changed += (rmx - rmn + 1); y++;
    }
    if (!wantFull) partial(gmn, y0, gmx - gmn + 1, y - y0);  // skip per-band partials if we'll full-refresh
  }
  if (changed) {
    if (wantFull) displayFull();                             // one clean full instead of many partials
    else _partialCycles++;
  }
  return changed;
}

void Mono::sleep() {
  cmd(0x02); checkBusy();                                    // power off
  cmd(0x07); data(0xA5);                                     // deep sleep
  _mode = 0;
}

}  // namespace ReTerminal
