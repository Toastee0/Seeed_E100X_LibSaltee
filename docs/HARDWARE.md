# reTerminal E1001 / E1002 — hardware notes

Both boards are the same ESP32-S3 (XIAO-class) platform with 8 MB OPI PSRAM; the pin map below
is identical across the mono (E1001) and colour (E1002) units and is schematic-confirmed. All
of it is in [`src/ReTerminalPins.h`](../src/ReTerminalPins.h).

## Pin map

| Function | GPIO | Notes |
|---|---|---|
| EPD SCK | 7 | SPI (HSPI) |
| EPD MOSI | 9 | |
| EPD CS | 10 | |
| EPD DC | 11 | |
| EPD RES | 12 | |
| EPD BUSY | 13 | HIGH = ready |
| Button — Refresh | 3 | active-low, 10k pull-up + 100 nF |
| Button — Left | 5 | (5/4 swapped vs silk so left/right match page order) |
| Button — Right | 4 | |
| Green LED | 6 | active-low |
| Buzzer | 45 | piezo, use `tone()` |
| I²C SDA | 19 | SHT4x @0x44, RTC PCF8563 @0x51 |
| I²C SCL | 20 | |
| Mic power-enable | 38 | HIGH powers the PDM mic (TPS22916) |
| Mic PDM CLK | 42 | |
| Mic PDM DATA | 41 | |

## Build settings (Arduino IDE / arduino-cli)

- Board: **XIAO_ESP32S3**
- **PSRAM = OPI PSRAM** — the two 800×480 framebuffers (working + on-glass snapshot) are
  `ps_malloc`'d. `Mono::begin()` returns `false` if PSRAM is off.
- **USB CDC On Boot = Enabled** — on this board the native-USB CDC goes to an unwired port;
  `CDCOnBoot=cdc` puts `Serial` on the on-board CH340 so your logs actually appear.
- arduino-cli FQBN: `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,CDCOnBoot=cdc`

## Flashing

- **USB (recommended, reliable):** the board enumerates as a **CH340** serial port. Flash with
  esptool over that port:
  `python -m esptool --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 <merged>.bin`
- **Serial monitor:** 115200 baud on the same CH340 port (toggle DTR/RTS to reset).

## Mono panel quirks (E1001 / UC8179)

- **4-gray full refresh** uses custom VCOM/WW/KW/WK/KK LUTs (panel setting `0x3F`). Pixel
  values are 0..3; the panel polarity is inverted, handled in `uploadGray4()`.
- **Fast 1-bit partial** switches the panel to the OTP B&W LUT (`0x1F`), writes the old (`0x10`)
  and new (`0x13`) planes for the window, and triggers a **force-temperature** waveform
  (`0xE0=0x02`, `0xE5=0x5A`) so the partial takes ~0.4 s instead of ~4 s.
- B&W projection is `gray == 3 → white, else black`. Keep that in mind if you mix grayscale
  chrome with partial-updated regions: a partial of a gray area renders it B&W. (The
  `refreshChanged(headerRows)` argument lets you fence off a gray header.)
- Partial refresh time scales with changed-pixel area — tight regions are fast, full-width busy
  graphs approach ~1.5 s.

## Colour panel quirks (E1002 / Spectra-6)

- Full-refresh only (~22–34 s); **partial refresh does not exist on colour e-paper** — it's a
  hardware limitation, not firmware.
- Driven via GxEPD2 (`GxEPD2_730c_GDEP073E01`), paged through a 16 KB RAM buffer.
- If you also run the PDM mic, capture on a **dedicated core-0 task** so it keeps sampling
  through the multi-second refresh that blocks the drawing core.
