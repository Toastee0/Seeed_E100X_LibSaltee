# Seeed reTerminal E100X e-paper library

An Arduino/ESP32 driver library **+ ready-to-run examples** for the Seeed Studio
**reTerminal E1001** (7.5″ mono) and **E1002** (7.3″ colour) e-paper terminals.

The headline feature is on the mono **E1001**: true **4-level grayscale** *and* a **fast 1-bit
partial refresh** of an arbitrary window (~0.4–1.5 s) — so you can update just the bits that
changed instead of waiting ~4 s for a full refresh every time. The colour **E1002** (Spectra-6)
is driven through GxEPD2 with the board's pin map and a raw-frame blit helper.

> Born out of the [Saltee](https://github.com/Toastee0/saltee) lab-dashboard project and shared
> back to the community — Seeed put these panels in our hands, so here's a clean, reusable
> firmware set in return. GPL-3.0, same spirit as the GxEPD2 driver it builds on.

## Just want it running? (no serial console, no coding)

You don't need to be a programmer or touch a serial terminal — the device tells you what it's
doing **on its own screen**.

1. **Flash it once.** Easiest is straight from a Chrome/Edge browser, or one `pip install` and a
   command — full walkthrough in **[`docs/SETUP.md`](docs/SETUP.md)**. Start with the **WiFiSetup**
   example.
2. **Set up WiFi by phone.** On first boot the device becomes its own hotspot and shows a QR to
   join it plus on-screen instructions. Join, a web page lists nearby networks, you pick yours and
   type the password. That's it — no editing code, no serial.
3. If something's wrong (bad password, etc.) the **screen says so**. Serial is optional, for
   developers only.

The rest of this README is for developers building on the library.

## Boards

| | **E1001** (mono) | **E1002** (colour) |
|---|---|---|
| Panel | 7.5″ 800×480, **UC8179** | 7.3″ 800×480, **Spectra-6** (GDEP073E01) |
| Colours | 4-level gray + 1-bit | 6 inks (black/white/red/yellow/blue/green) |
| Refresh | gray full ~4.2 s · **B&W partial ~0.4–1.5 s** | full only ~22–34 s (no partial on colour e-paper) |
| MCU | ESP32-S3 (XIAO-class), 8 MB OPI PSRAM | same |
| Driver class | `ReTerminal::Mono` (self-contained) | `RETERMINAL_COLOR_DISPLAY(...)` via GxEPD2 |

## Install

1. **This library** — Sketch → Include Library → Add .ZIP Library (or clone into your
   `Arduino/libraries/`).
2. **Dependencies:**
   - *Mono only:* none — `ReTerminal::Mono` is self-contained.
   - *Colour:* [**GxEPD2**](https://github.com/ZinggJM/GxEPD2) (Library Manager).
   - *Examples with text/graphics:* **Adafruit GFX Library** (Library Manager).
3. **Board:** install the ESP32 core, select **XIAO_ESP32S3**, and set:
   - **PSRAM = OPI PSRAM**  (the 800×480 framebuffers live in PSRAM)
   - **USB CDC On Boot = Enabled**  (routes `Serial` to the on-board CH340 — without this your
     logs go to an unwired USB port and vanish)

## Quick start (mono)

```cpp
#include <ReTerminalMono.h>
ReTerminal::Mono epd;

void setup() {
  epd.begin();                       // allocates PSRAM buffers, brings up SPI
  uint8_t* fb = epd.buffer();        // 800*480 bytes, each 0(black)..3(white)
  for (int i = 0; i < 800*480; i++) fb[i] = (i % 800) * 4 / 800;   // a gradient
  epd.displayFull();                 // crisp 4-level gray (~4.2 s)
}

void loop() {
  uint8_t* fb = epd.buffer();
  /* ...change a region of fb... */
  epd.partial(0, 8, 800, 64);        // fast B&W refresh of just that window (~0.4 s)
  delay(2000);
}
```

`refreshChanged()` will diff the buffer against the glass and partial-refresh the changed
bands for you (optionally leaving the top *N* rows untouched if you keep gray chrome there).

## Examples

| Example | What it shows |
|---|---|
| **HelloMono** | grayscale full refresh + a block that steps across the screen via fast partials. No deps. |
| **HelloColor** | the six inks + live SHT4x reading on the E1002. Needs GxEPD2. |
| **ThinClient** | fetch an 800×480 gray frame from *your* server and partial-refresh only what changed — render server-side, change the UI with no reflash. |
| **StandaloneDashboard** | self-contained: NTP clock + Open-Meteo weather + on-board SHT4x + battery %, no server. |
| **NetScan** | ICMP ping-sweep of the device's own /24 (no extra library) → a live grid of which hosts are up, partial-refreshed. |
| **PCMonitor** | parse a Windows PC's **LibreHardwareMonitor** JSON on-device → live CPU/GPU temps, partial-refreshed. See [`docs/PC_MONITOR.md`](docs/PC_MONITOR.md). |
| **WiFiQR** | a "scan to join my WiFi" QR, generated **on-device** from the credentials it already knows (`WiFi.SSID()`/`WiFi.psk()`) — uses the ESP32 core's built-in QR encoder, no extra library. |
| **WiFiSetup** | **no-code, no-serial onboarding**: captive-portal WiFi setup with on-screen status + a QR to join the device's setup hotspot. Hold Refresh at boot to re-provision. |

## On-board peripherals

`ReTerminal::Peripherals` wraps the shared hardware: the three front buttons (debounced edge
detection), the green LED, the buzzer, the **SHT4x** temp/humidity sensor (read over I²C, no
extra library), and the **battery** — `batteryVolts()` / `batteryPercent()` enable the divider
(GPIO21), read GPIO1 at 12 dB, and apply the ×2 ratio + a LiPo curve (per Seeed's reference
calibration). The PDM mic is left to you — it wants a dedicated core-0 task to keep capturing
through the long colour refresh.

## Docs

- [`docs/SETUP.md`](docs/SETUP.md) — **start here to flash the board**: browser flashing, the
  `esptool` command line, or the Arduino IDE — for people who've never used an ESP32.
- [`docs/SERVER.md`](docs/SERVER.md) — a complete copy-paste server for the **ThinClient** example
  (exact Python version + install, and why).
- [`docs/PC_MONITOR.md`](docs/PC_MONITOR.md) — show a Windows PC's CPU/GPU temps: set up
  **LibreHardwareMonitor**, then either parse its JSON on-device or render via a small Python app.
- [`docs/HOME_ASSISTANT.md`](docs/HOME_ASSISTANT.md) — pull **Home Assistant** entities (token +
  REST API) and render them to the panel.
- [`docs/HARDWARE.md`](docs/HARDWARE.md) — pin map, peripherals, and the gotchas worth knowing.
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — the simple raw-frame format used by ThinClient.
- [`SKILLS.md`](SKILLS.md) — orientation for AI coding agents (and a quick architecture tour).

## Credit & licence

The UC8179 grayscale LUTs and init sequences derive from Seeed's fork of **GxEPD2**
(Jean-Marc Zingg), which is GPL. This library is therefore **GPL-3.0-or-later** — see
[`LICENSE`](LICENSE). Hardware by [Seeed Studio](https://www.seeedstudio.com/).
