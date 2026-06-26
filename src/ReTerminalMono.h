// ReTerminalMono.h — driver for the Seeed reTerminal E1001 (7.5" 800x480 mono, UC8179).
//
// This panel does TWO things stock GxEPD2 mono drivers don't expose together:
//   * true 4-level grayscale via custom VCOM/WW/KW/WK/KK LUTs (full refresh, ~4.2s), and
//   * a fast 1-bit PARTIAL refresh of an arbitrary window (~0.4-1.5s, force-temperature
//     waveform) for updating just the bits that changed.
// You own an 800x480 working framebuffer of gray levels (0=black .. 3=white). Draw into it
// however you like (raw bytes, or an Adafruit_GFX canvas — see examples/), then push it with
// displayFull() for a crisp grayscale page, or partial()/refreshChanged() to update a region
// in a fraction of a second. The class keeps a snapshot of what's on the glass so partials
// diff correctly.
//
// LUTs + init sequences derive from Seeed_GxEPD2 (GxEPD2_reTerminal_E1001_Gray4 / GDEY075T7),
// which is GPL — hence this library is GPL-3.0-or-later.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "ReTerminalPins.h"

namespace ReTerminal {

class Mono {
 public:
  Mono() : _spi(HSPI) {}

  // Allocate the PSRAM framebuffers and bring up SPI + control pins. Returns false if PSRAM
  // is unavailable (enable OPI PSRAM in the board menu). Call once in setup().
  bool begin();

  static constexpr int width()  { return PANEL_W; }
  static constexpr int height() { return PANEL_H; }

  // The working framebuffer: PANEL_W*PANEL_H bytes, one gray level 0..3 per pixel, row-major.
  // Draw here, then displayFull()/partial(). Never null after a successful begin().
  uint8_t* buffer() { return _frame; }

  void fill(uint8_t gray);                                   // fill the working buffer (0..3)

  // Full 4-level-gray refresh of the whole panel from the working buffer (~4.2s). Re-inits the
  // panel into gray mode. Snapshots the buffer as "what's on the glass".
  void displayFull();

  // Fast 1-bit PARTIAL refresh of a window from the working buffer. Pixels project to B&W as
  // (gray==3 -> white, else black). x is byte-aligned internally. Updates the on-glass snapshot
  // for that window so the next diff is correct. Returns false if the rect is empty/off-panel.
  bool partial(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  // Convenience: diff the working buffer against the snapshot and partial-refresh the changed
  // rows as one or more bands. Returns the number of pixels that differed (0 = nothing redrawn).
  // `headerRows` rows at the top are left untouched (handy if you keep grayscale chrome there
  // that a B&W partial would blacken). Pass 0 to consider the whole panel.
  // ANTI-GHOST: B&W partials slowly accumulate ghosting; every setFullEvery() cycles this does a
  // clean full gray refresh instead of a partial, to wipe it. Default 8 (0 = never auto-full).
  uint32_t refreshChanged(uint16_t headerRows = 0);

  // How many refreshChanged() partial cycles between automatic full (anti-ghost) refreshes.
  void setFullEvery(uint16_t cycles) { _fullEvery = cycles; }

  void sleep();                                              // deep sleep the panel (power off)

 private:
  void csLow()  { digitalWrite(PIN_EPD_CS, LOW); }
  void csHigh() { digitalWrite(PIN_EPD_CS, HIGH); }
  void checkBusy(uint32_t to = 40000);
  void cmd(uint8_t c);
  void data(uint8_t d);
  void writeLUT(uint8_t c, const uint8_t* lut, uint16_t len);
  void initGrayMode();
  void initBWMode();
  void setPartWin(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void streamWin(uint8_t c, const uint8_t* src, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void uploadGray4();
  static inline uint8_t bwbit(uint8_t g) { return (g & 3) == 3 ? 1 : 0; }

  SPIClass _spi;
  SPISettings _set{2000000, MSBFIRST, SPI_MODE0};
  uint8_t* _frame = nullptr;   // working buffer the caller draws into
  uint8_t* _disp  = nullptr;   // snapshot of what's currently on the glass (for partial diffs)
  int _mode = 0;               // 0 = uninit, 1 = gray, 2 = b&w
  uint16_t _fullEvery = 8;     // refreshChanged(): force a full gray refresh every N partials (anti-ghost)
  uint16_t _partialCycles = 0; // partials since the last full refresh
};

}  // namespace ReTerminal
