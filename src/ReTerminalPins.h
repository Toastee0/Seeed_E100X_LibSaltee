// ReTerminalPins.h — pin map for the Seeed reTerminal E1001 / E1002 e-paper terminals.
//
// Both boards are the same XIAO-ESP32-S3-class platform; these pins are schematic-confirmed
// and identical across the mono (E1001) and color (E1002) units. Build target:
//   XIAO_ESP32S3 with PSRAM=opi, CDCOnBoot=cdc
// (CDCOnBoot=cdc routes Serial to the on-board CH340/UART0 — the native-USB CDC goes to an
//  unwired port, so without this your Serial logs vanish.)
//
// Part of reterminal-epaper. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace ReTerminal {

// ---- e-paper panel (SPI) ----
constexpr int PIN_EPD_SCK  = 7;
constexpr int PIN_EPD_MISO = 8;    // shared SPI bus MISO — the panel is write-only, but the on-board
                                   // microSD reads on this line (see SD pins below). Routing it costs
                                   // nothing for the EPD and lets SD share the bus.
constexpr int PIN_EPD_MOSI = 9;
constexpr int PIN_EPD_CS   = 10;
constexpr int PIN_EPD_DC   = 11;
constexpr int PIN_EPD_RES  = 12;
constexpr int PIN_EPD_BUSY = 13;   // HIGH = ready

// ---- on-board microSD (shares the EPD SPI bus: SCK 7 / MISO 8 / MOSI 9) ----
// Schematic (reTerminal E1001 V1.2, sheet 6 "SD Card"): the card is powered through a TPS22916
// load switch — SD_EN must be driven HIGH before SD.begin() or the card never powers up.
constexpr int PIN_SD_CS  = 14;
constexpr int PIN_SD_EN  = 16;     // load-switch enable: HIGH = card powered
constexpr int PIN_SD_DET = 15;     // card-detect (LOW = card inserted)

// ---- buttons (active-low; 10k pull-up + 100nF on-board) ----
// LEFT/RIGHT are pins 5/4 (swapped from silk so the physical direction matches page order).
constexpr int PIN_BTN_REFRESH = 3;
constexpr int PIN_BTN_LEFT    = 5;
constexpr int PIN_BTN_RIGHT   = 4;

// ---- misc on-board peripherals ----
constexpr int PIN_LED    = 6;      // green status LED, active-low
constexpr int PIN_BUZZER = 45;     // piezo (use tone())
constexpr int PIN_I2C_SDA = 19;    // I2C0 — SHT4x temp/hum @0x44, RTC PCF8563 @0x51
constexpr int PIN_I2C_SCL = 20;
constexpr int PIN_MIC_EN   = 38;   // TPS22916 enable: HIGH powers the PDM mic
constexpr int PIN_MIC_CLK  = 42;   // PDM clock
constexpr int PIN_MIC_DATA = 41;   // PDM data

constexpr uint8_t SHT4X_ADDR = 0x44;

// ---- battery (2000 mAh LiPo, measured via an ADC divider) ----
// Per Seeed's reference config: read GPIO1 at 12 dB attenuation and multiply by 2.0 (the
// divider ratio); GPIO21 must be driven HIGH to power the divider before reading.
constexpr int PIN_BATTERY_ADC    = 1;
constexpr int PIN_BATTERY_ENABLE = 21;
constexpr float BATTERY_DIVIDER  = 2.0f;
constexpr uint32_t BATTERY_CACHE_MS = 300000;   // re-read at most every 5 min (the read itself costs power)

// ---- panel geometry (both 7.x" panels are 800x480 landscape) ----
constexpr int PANEL_W = 800;
constexpr int PANEL_H = 480;

}  // namespace ReTerminal
