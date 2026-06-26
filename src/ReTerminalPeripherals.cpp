// ReTerminalPeripherals.cpp — see ReTerminalPeripherals.h. GPL-3.0-or-later.
#include "ReTerminalPeripherals.h"
#include <Wire.h>

namespace ReTerminal {

void Peripherals::begin(bool powerMic) {
  pinMode(PIN_BTN_REFRESH, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT,    INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT,   INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);  digitalWrite(PIN_LED, HIGH);    // off (active-low)
  pinMode(PIN_BUZZER, OUTPUT);
  if (powerMic) { pinMode(PIN_MIC_EN, OUTPUT); digitalWrite(PIN_MIC_EN, HIGH); }
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
}

// Simple debounced edge: fire when the pin transitions released(HIGH) -> pressed(LOW).
bool Peripherals::pressedEdge(int pin, bool& lastHigh) {
  bool high = digitalRead(pin);                              // HIGH = released (pull-up)
  bool fired = false;
  if (lastHigh && !high) { fired = true; delay(15); }        // falling edge + tiny debounce
  lastHigh = high;
  return fired;
}

uint8_t Peripherals::crc8(const uint8_t* d, int n) {         // SHT4x CRC-8 (poly 0x31, init 0xFF)
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

bool Peripherals::readSHT4x(float& tempC, float& humidityPct) {
  Wire.beginTransmission(SHT4X_ADDR);
  Wire.write(0xFD);                                          // measure, high precision
  if (Wire.endTransmission() != 0) return false;
  delay(10);                                                 // ~8.3 ms conversion
  if (Wire.requestFrom((int)SHT4X_ADDR, 6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return false;
  uint16_t rawT = (b[0] << 8) | b[1];
  uint16_t rawH = (b[3] << 8) | b[4];
  tempC       = -45.0f + 175.0f * (rawT / 65535.0f);
  humidityPct = -6.0f  + 125.0f * (rawH / 65535.0f);
  if (humidityPct < 0) humidityPct = 0;
  if (humidityPct > 100) humidityPct = 100;
  return true;
}

}  // namespace ReTerminal
