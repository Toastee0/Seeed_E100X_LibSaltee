// ReTerminalEpaper.h — umbrella include for the reterminal-epaper library.
//
// Public firmware/driver set for the Seeed reTerminal E1001 (7.5" mono, UC8179) and
// E1002 (7.3" colour, Spectra-6) e-paper terminals. GPL-3.0-or-later.
//
//   #include <ReTerminalEpaper.h>
//   ReTerminal::Mono epd;   // or ReTerminal::Color (needs GxEPD2 installed)
//
// Pull in only what you need (ReTerminalMono.h / ReTerminalColor.h / ReTerminalPeripherals.h)
// to keep dependencies minimal — the mono driver is self-contained; the colour driver needs
// GxEPD2; the peripherals helpers need Adafruit's sensor libs only for the parts you call.
#pragma once
#include "ReTerminalPins.h"
#include "ReTerminalMono.h"
#include "ReTerminalPeripherals.h"
// Colour driver is opt-in (needs GxEPD2). It self-disables if GxEPD2 isn't installed.
#include "ReTerminalColor.h"
