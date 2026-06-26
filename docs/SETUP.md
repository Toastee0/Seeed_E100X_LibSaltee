# Getting it onto the board (start here if you've never used an ESP32)

You do **not** need to be a programmer, and you do **not** need a serial console — the device
shows what it's doing on its own screen. You just need to get the firmware *onto* the board
once. There are three ways, easiest first. Pick one.

What you need either way:
- the reTerminal (E1001 or E1002) and a **USB-C cable that carries data** (not a charge-only one),
- a computer (Windows, macOS, or Linux).

---

## Option A — flash from your web browser (no install)

The simplest path: a Chromium-based browser (Chrome or Edge) can flash an ESP32 over USB with
nothing installed.

1. Plug the board into your computer with the USB-C cable.
2. Open a **prebuilt firmware** page in Chrome/Edge and click **Connect / Install** (these pages
   are built with [ESP Web Tools](https://esphome.github.io/esp-web-tools/)). Seeed's own
   browser flasher for stock demos is the **reTerminal E-Series Firmware Hub** — a good place to
   confirm your board works before loading custom firmware.
3. When asked, pick the serial port that appears, and let it write. Done.

> Firefox and Safari can't do USB flashing — use Chrome or Edge. If no port appears, see
> **Drivers** at the bottom.

To publish your *own* build this way you only need to host the merged `.bin` plus a small JSON
manifest; that's optional and covered in ESP Web Tools' docs.

---

## Option B — command line, no Arduino IDE (`esptool`)

If you have a prebuilt `.bin` and just want it flashed, this is reliable and scriptable. It uses
**Python**.

1. Install Python **3.8 or newer** (3.11+ recommended) from <https://python.org> (on macOS/Linux
   it's usually already there — check with `python3 --version`).
2. Install the flasher:
   ```
   python3 -m pip install --upgrade esptool
   ```
3. Flash the merged image at offset 0 (replace the port and filename):
   ```
   # find the port first:  esptool --chip esp32s3 flash-id        (it prints the port it used)
   esptool --chip esp32s3 --port <PORT> --baud 460800 write-flash 0x0 firmware.bin
   ```
   - `<PORT>` is like `COM7` on Windows, `/dev/tty.usbserial-XXXX` on macOS, `/dev/ttyUSB0` on Linux.
   - A *merged* `.bin` already contains the bootloader + partitions + app, so it goes at `0x0`.

---

## Option C — build it yourself in the Arduino IDE

Use this if you want to change an example or build from source.

1. Install the **Arduino IDE 2.x** (<https://www.arduino.cc/en/software>).
2. **Add the ESP32 boards:** *File → Preferences → Additional boards manager URLs* →
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` → then *Tools → Board →
   Boards Manager*, search **esp32** (by Espressif), Install.
3. **Add this library:** *Sketch → Include Library → Add .ZIP Library…* and select the ZIP of
   this repo (or `git clone` it into your `Arduino/libraries/` folder).
4. **Add the example's libraries** via *Tools → Manage Libraries*: **Adafruit GFX**, and (for the
   colour E1002 only) **GxEPD2**. The mono driver and the QR/ping examples need nothing else.
5. **Select the board & settings** — *Tools*:
   - **Board:** `XIAO_ESP32S3`
   - **PSRAM:** `OPI PSRAM`  ← required; the 800×480 image buffers live in PSRAM
   - **USB CDC On Boot:** `Enabled`  ← so the USB serial port behaves (optional but recommended)
6. *File → Examples → ReTerminal Epaper →* pick one (start with **WiFiSetup** or **HelloMono**),
   then click **Upload**.

---

## Board settings, in one place

| Setting | Value | Why |
|---|---|---|
| Board | `XIAO_ESP32S3` | the reTerminal E100X is this ESP32-S3 class |
| PSRAM | `OPI PSRAM` | two 800×480 buffers (~750 KB) live in external PSRAM |
| USB CDC On Boot | `Enabled` | puts the USB serial on the cable; without it serial logs go nowhere |
| arduino-cli FQBN | `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,CDCOnBoot=cdc` | for the command-line build |

## Do I need the serial console? No.

Serial is only for developers watching debug logs. **Every example reports its status on the
e-paper screen** — "Connecting…", "WiFi failed", the device's IP address, setup instructions.
If you never open a serial monitor, nothing is lost.

## Drivers (if the board doesn't show up)

The board talks over a **CH340** USB-serial chip. Windows 10/11 and modern macOS/Linux usually
install the driver automatically. If no port appears, install WCH's CH340 driver
(<https://www.wch-ic.com/downloads/CH341SER_EXE.html>) and replug. Also double-check the cable
carries data — many USB-C cables are charge-only.
