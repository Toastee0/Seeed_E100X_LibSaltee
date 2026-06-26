// ReTerminalQR.h — render QR codes into the mono framebuffer (great for WiFi onboarding).
//
// Uses the QR encoder built into the ESP32 Arduino core (ESP-IDF `esp_qrcode`) — NO external
// library, nothing to install. (The popular ricmoo "QRCode" library can't be used here: the
// core ships its own <qrcode.h>, which shadows it.)
//
//   #include <ReTerminalQR.h>
//   char buf[160];
//   ReTerminal::wifiQRPayload(buf, sizeof(buf), WiFi.SSID().c_str(), WiFi.psk().c_str());
//   ReTerminal::drawQR(epd.buffer(), epd.width(), buf, 380, 150, 380, 280);   // centred in a box
//
// GPL-3.0-or-later.
#pragma once

#if defined(__has_include) && __has_include(<qrcode.h>)
#define RETERMINAL_HAS_QR 1
#include <Arduino.h>
#include <qrcode.h>          // ESP-IDF esp_qrcode (built into the ESP32 core)
#include <string.h>

namespace ReTerminal {

// Build a WiFi-join payload: WIFI:S:<ssid>;T:<auth>;P:<pass>;H:<hidden>;;  (auth = WPA/WEP/nopass).
// Escapes the QR-reserved characters \ ; , : " in the SSID/password. Returns the length written.
inline size_t wifiQRPayload(char* out, size_t cap, const char* ssid, const char* pass,
                            const char* auth = "WPA", bool hidden = false) {
  auto put = [&](size_t& i, char c) { if (i + 1 < cap) out[i++] = c; };
  auto field = [&](size_t& i, const char* s) {
    for (; *s; s++) { if (strchr("\\;,:\"", *s)) put(i, '\\'); put(i, *s); }
  };
  size_t i = 0;
  const char* p = "WIFI:S:"; while (*p) put(i, *p++);
  field(i, ssid);
  p = ";T:"; while (*p) put(i, *p++); while (*auth) put(i, *auth++);
  p = ";P:"; while (*p) put(i, *p++); field(i, pass);
  p = ";H:"; while (*p) put(i, *p++); put(i, hidden ? 't' : 'f');
  p = ";;";  while (*p) put(i, *p++);
  out[i] = 0;
  return i;
}

namespace detail {
struct QRCtx { uint8_t* fb; int fbW, x, y, w, h, quiet, modules; uint8_t fg, bg; };
inline QRCtx& qrctx() { static QRCtx c; return c; }   // shared with the esp_qrcode callback

inline void qrRender(esp_qrcode_handle_t qr) {
  QRCtx& c = qrctx();
  const int n = esp_qrcode_get_size(qr);
  c.modules = n;
  const int span = n + 2 * c.quiet;
  const int scale = (c.w < c.h ? c.w : c.h) / span;
  if (scale < 1) return;
  const int qpx = span * scale;
  const int ox = c.x + (c.w - qpx) / 2, oy = c.y + (c.h - qpx) / 2;
  for (int py = 0; py < qpx; py++) {                   // background incl. quiet zone
    uint8_t* row = c.fb + (size_t)(oy + py) * c.fbW + ox;
    for (int px = 0; px < qpx; px++) row[px] = c.bg;
  }
  for (int my = 0; my < n; my++)
    for (int mx = 0; mx < n; mx++)
      if (esp_qrcode_get_module(qr, mx, my)) {
        const int bx = ox + (c.quiet + mx) * scale, by = oy + (c.quiet + my) * scale;
        for (int dy = 0; dy < scale; dy++) {
          uint8_t* row = c.fb + (size_t)(by + dy) * c.fbW + bx;
          for (int dx = 0; dx < scale; dx++) row[dx] = c.fg;
        }
      }
}
}  // namespace detail

// Render `text` as a QR into framebuffer `fb` (width fbW, values 0..3), centred in box (x,y,w,h)
// and scaled as large as fits. fg = dark-module level (0 = black), bg = light (3 = white).
// Returns module count, or 0 on failure (text too long for the box / encoder error).
inline int drawQR(uint8_t* fb, int fbW, const char* text, int x, int y, int w, int h,
                  uint8_t fg = 0, uint8_t bg = 3, int quiet = 2) {
  detail::qrctx() = { fb, fbW, x, y, w, h, quiet, 0, fg, bg };
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = detail::qrRender;
  cfg.max_qrcode_version = 11;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  if (esp_qrcode_generate(&cfg, text) != ESP_OK) return 0;
  return detail::qrctx().modules;
}

}  // namespace ReTerminal

#else
#define RETERMINAL_HAS_QR 0
#endif
