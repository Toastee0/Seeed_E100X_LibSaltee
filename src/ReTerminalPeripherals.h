// ReTerminalPeripherals.h — on-board peripheral helpers for the reTerminal E1001/E1002.
//
// Covers the bits both boards share: the three front buttons (active-low, debounced edge
// detection), the green status LED (active-low), the piezo buzzer, and the SHT4x temp/hum
// sensor on I2C0. PDM mic capture is intentionally left to the examples (it wants a dedicated
// core-0 task to survive the long colour refresh — see examples/). Dependency-free: SHT4x is
// read over Wire directly, no extra sensor library. GPL-3.0-or-later.
#pragma once
#include <Arduino.h>
#include "ReTerminalPins.h"

namespace ReTerminal {

class Peripherals {
 public:
  // Configure button/LED/buzzer pins and start Wire on the board's I2C0. Pass powerMic=true to
  // assert the mic power-enable rail (TPS22916) if you intend to capture audio.
  void begin(bool powerMic = false);

  // Edge-triggered: each returns true ONCE per physical press (call often, e.g. every loop).
  bool refreshPressed() { return pressedEdge(PIN_BTN_REFRESH, _lastRefresh); }
  bool leftPressed()    { return pressedEdge(PIN_BTN_LEFT,    _lastLeft); }
  bool rightPressed()   { return pressedEdge(PIN_BTN_RIGHT,   _lastRight); }

  void led(bool on) { digitalWrite(PIN_LED, on ? LOW : HIGH); }     // active-low
  void beep(uint16_t freq = 2200, uint16_t ms = 45) { tone(PIN_BUZZER, freq, ms); }

  // High-precision SHT4x read (command 0xFD). Returns true on success (with CRC check).
  bool readSHT4x(float& tempC, float& humidityPct);

  // ---- battery (2000 mAh LiPo) ----
  // Reads are CACHED (BATTERY_CACHE_MS, 5 min) because enabling the divider draws current —
  // calling these every frame would needlessly drain the battery. Pass force=true for a fresh read.
  float batteryVolts(bool force = false);
  // Battery state of charge 0..100 %, from a LiPo discharge curve (Seeed calibration anchors).
  uint8_t batteryPercent(bool force = false);

 private:
  bool pressedEdge(int pin, bool& lastHigh);
  static uint8_t crc8(const uint8_t* d, int n);

  bool _lastRefresh = true, _lastLeft = true, _lastRight = true;    // true = released (HIGH)
  float _battCache = -1.0f; uint32_t _battStamp = 0;               // cached battery volts + timestamp
};

}  // namespace ReTerminal
