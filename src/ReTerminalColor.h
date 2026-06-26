// ReTerminalColor.h — driver helpers for the Seeed reTerminal E1002 (7.3" 800x480 Spectra-6).
//
// The colour panel is a GxEPD2 part (GxEPD2_730c_GDEP073E01). Rather than re-wrap GxEPD2's
// templated display, this header gives you:
//   * RETERMINAL_COLOR_DISPLAY(name) — declare a display object wired to the board pins,
//   * ReTerminal::INK6[] — the 6-ink palette (black,white,red,yellow,blue,green), and
//   * ReTerminal::drawRawFrame(display, idx) — blit an 800x480 buffer of ink indices (0..5).
// Use the `display` object with the normal Adafruit_GFX API (setCursor/print/fillRect/…).
//
// Spectra-6 is full-refresh only (~22-34 s); there is no partial refresh on colour e-paper.
// This header self-disables (compiles to nothing, RETERMINAL_HAS_COLOR==0) if GxEPD2 isn't
// installed, so mono-only projects don't need it. GPL-3.0-or-later.
#pragma once

#if defined(__has_include) && __has_include(<GxEPD2_7C.h>)
#define RETERMINAL_HAS_COLOR 1
#include <GxEPD2_7C.h>
#include "ReTerminalPins.h"

namespace ReTerminal {
// 6-ink index -> GxEPD colour. Index order matches a typical palette-quantised frame.
static const uint16_t INK6[6] = { GxEPD_BLACK, GxEPD_WHITE, GxEPD_RED, GxEPD_YELLOW, GxEPD_BLUE, GxEPD_GREEN };
using ColorPanel = GxEPD2_730c_GDEP073E01;
}

// GxEPD2 pages the 800x480 panel through a small RAM buffer (cap 16 KB).
#define RETERMINAL_COLOR_BUFFER 16000u
#define RETERMINAL_COLOR_MAXH(EPD) \
  (EPD::HEIGHT <= (RETERMINAL_COLOR_BUFFER) / (EPD::WIDTH / 2) \
     ? EPD::HEIGHT : (RETERMINAL_COLOR_BUFFER) / (EPD::WIDTH / 2))

// Declare a reTerminal E1002 display object `name`, wired to the board pins.
#define RETERMINAL_COLOR_DISPLAY(name) \
  GxEPD2_7C<ReTerminal::ColorPanel, RETERMINAL_COLOR_MAXH(ReTerminal::ColorPanel)> \
    name(ReTerminal::ColorPanel(ReTerminal::PIN_EPD_CS, ReTerminal::PIN_EPD_DC, \
                                ReTerminal::PIN_EPD_RES, ReTerminal::PIN_EPD_BUSY))

namespace ReTerminal {
// Full-refresh an 800x480 buffer of 6-ink indices (0..5, see INK6) to the colour panel.
template <typename Display>
void drawRawFrame(Display& d, const uint8_t* idx) {
  d.setFullWindow();
  d.firstPage();
  do {
    for (int y = 0; y < PANEL_H; y++)
      for (int x = 0; x < PANEL_W; x++)
        d.drawPixel(x, y, INK6[idx[(size_t)y * PANEL_W + x] % 6]);
  } while (d.nextPage());
}
}  // namespace ReTerminal

#else
#define RETERMINAL_HAS_COLOR 0
#endif
