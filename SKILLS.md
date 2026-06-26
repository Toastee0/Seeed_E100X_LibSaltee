# SKILLS.md — working on this library (notes for an AI coding agent)

You're editing **reterminal-epaper**, a GPL-3.0 Arduino library + examples for the Seeed
reTerminal **E1001** (7.5″ mono, UC8179) and **E1002** (7.3″ colour, Spectra-6). This file is the
fast path to being productive without re-deriving everything. Human contributors: this is also a
decent architecture tour.

## Mental model

- Target MCU: **ESP32-S3** with **8 MB OPI PSRAM**. FQBN:
  `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,CDCOnBoot=cdc`.
- The **mono driver is the crown jewel** and is fully self-contained (raw UC8179 SPI, no GxEPD2):
  4-level gray full refresh **and** fast 1-bit partial refresh of any window.
- The **colour driver is a thin wrapper over GxEPD2** (full-refresh only; colour e-paper has no
  partial refresh — that's hardware, don't try to add it).
- Examples should be runnable by non-technical users: **status goes on the e-paper, not Serial**;
  WiFi is provisioned via the captive portal in `examples/WiFiSetup`, not by editing code.

## Layout

```
src/ReTerminalPins.h          pin map (both boards) + battery/ADC constants
src/ReTerminalMono.{h,cpp}    ReTerminal::Mono — gray + partial engine (no deps)
src/ReTerminalColor.h         RETERMINAL_COLOR_DISPLAY() + drawRawFrame() (needs GxEPD2; self-disables)
src/ReTerminalPeripherals.{h,cpp}  buttons, LED, buzzer, SHT4x, battery
src/ReTerminalQR.h            drawQR() + wifiQRPayload() (uses the core's built-in esp_qrcode)
src/ReTerminalEpaper.h        umbrella include
examples/                     HelloMono, HelloColor, ThinClient, StandaloneDashboard, NetScan,
                              WiFiQR, WiFiSetup
docs/                         SETUP.md (flashing), SERVER.md (ThinClient server), HARDWARE.md, PROTOCOL.md
```

## API cheat-sheet

```cpp
ReTerminal::Mono epd;
epd.begin();                       // allocs two PSRAM buffers; false if PSRAM off
uint8_t* fb = epd.buffer();        // 800*480 bytes, each 0(black)..3(white), row-major
epd.fill(3);                       // whole buffer
epd.displayFull();                 // 4-gray full refresh (~4.2s); snapshots buffer as on-glass
epd.partial(x,y,w,h);              // fast B&W partial of a window (~0.4-1.5s)
epd.refreshChanged(headerRows=0);  // diff vs snapshot, partial the changed bands

ReTerminal::Peripherals io;  io.begin();
io.refreshPressed()/leftPressed()/rightPressed();  io.led(on);  io.beep();
io.readSHT4x(t,h);  io.batteryVolts();  io.batteryPercent();

#include <GxEPD2_7C.h>             // colour examples MUST include this directly (see gotchas)
RETERMINAL_COLOR_DISPLAY(display); // GxEPD2 object wired to board pins; use normal Adafruit_GFX
ReTerminal::drawRawFrame(display, idxBuf);   // blit an 800x480 6-ink-index buffer

ReTerminal::wifiQRPayload(buf, n, ssid, pass, "WPA");  // build WIFI:...;; string
ReTerminal::drawQR(fb, fbW, text, x, y, w, h);         // render QR into the mono buffer
```

## Build / flash / verify (command line)

```bash
# build an example to an output dir
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi,CDCOnBoot=cdc \
  --library <repo> --output-dir /tmp/build <repo>/examples/HelloMono

# flash the MERGED image at 0x0 (bootloader+partitions+app in one)
python3 -m esptool --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 /tmp/build/HelloMono.ino.merged.bin

# verify without a human: read the serial banner (115200). Examples also print status, e.g.
#   WiFiQR -> "QR: WIFI:S:...;; (N modules)";  WiFiSetup -> connection result.
# If a camera is pointed at the panel, grab a still (ffmpeg -f v4l2 -i /dev/videoN ...) and read it.
```

OTA note: ESP-IDF/espota OTA on these panels is flaky (the display refresh starves the upload
window). **Prefer USB/serial flashing.**

## Gotchas (each of these has cost someone time)

1. **PSRAM must be ON** (`PSRAM=opi`) or `Mono::begin()` returns false — two 800×480 buffers.
2. **`CDCOnBoot=cdc`** routes `Serial` to the on-board CH340; without it logs go to an unwired
   USB port and vanish.
3. **QR: use the core's built-in `esp_qrcode`**, NOT the ricmoo "QRCode" library — the ESP32 core
   ships its own `<qrcode.h>` (`espressif__qrcode`) that *shadows* ricmoo's, so the popular lib
   won't link. `ReTerminalQR.h` already uses the built-in one (callback API).
4. **Colour examples must `#include <GxEPD2_7C.h>` directly.** `ReTerminalColor.h` guards on
   `__has_include(<GxEPD2_7C.h>)`, but Arduino's library scanner only adds GxEPD2 to the include
   path if the *sketch* references it — so without the direct include the colour driver silently
   compiles to nothing (`RETERMINAL_HAS_COLOR == 0`).
5. **B&W partial projection is `gray==3 → white, else black`.** Mixing gray chrome with partials
   blackens it; use `refreshChanged(headerRows)` to fence off a header, or keep partial regions
   on white. (See the saltee-style "wifi pill" trick: only pure-white regions partial cleanly in
   the header.)
6. **No partial refresh on the colour panel.** Full refresh only, ~22–34 s.
7. **x is byte-aligned** in partials (the driver rounds to 8 px); don't assume pixel-exact x/w.

## Conventions

- Keep the **mono driver dependency-free**. New deps belong in examples, guarded by
  `__has_include` if they're optional.
- Examples target non-experts: prefer on-screen feedback; keep WiFi creds out of committed code
  (placeholders only) — real provisioning is `WiFiSetup`'s captive portal.
- Every example must compile clean for the FQBN above before committing.
- License is **GPL-3.0-or-later** (inherited from GxEPD2). Don't add incompatible-licensed deps.
