# Showing a Windows PC's temps/load on the display

[LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) (LHM) reads
CPU/GPU/board sensors on Windows and can serve them as JSON over HTTP. The reTerminal can show
those — **two ways**, and you pick based on how much polish you want:

| | **A. Direct** (device parses the PC's JSON) | **B. Server app** (a small program renders a frame) |
|---|---|---|
| Setup | flash the **PCMonitor** example, set the URL | run a Python script on a PC/Pi |
| Looks | the numbers, simple | whatever you draw — bars, history, multiple PCs |
| Moving parts | just the device + LHM | device + LHM + the script |
| Good for | one PC, quick | a nicer dashboard, several PCs, mixing in other data |

> Why LibreHardwareMonitor and not the older Open Hardware Monitor? OHM's last release predates
> recent CPUs — on a 12th/13th/14th-gen Intel it can't read the CPU package temperature at all
> (the CPU node has load but no temperature). LHM reads them. If you only see motherboard
> "Temperature #N" sensors and no "CPU Package", you're on OHM — switch to LHM.

## 1. Set up LibreHardwareMonitor on the PC

1. Download the latest release zip from the
   [LHM releases page](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases),
   unzip it anywhere, and run **LibreHardwareMonitor.exe** (right-click → Run as administrator so
   it can read all sensors).
2. Turn on the web server: **Options → Remote Web Server → Port** (default **8085**), then
   **Options → Remote Web Server → Run**.
3. (Optional, recommended) **Options → Run On Windows Startup** and **Minimize On Startup** so it's
   always available.
4. **Open the firewall** so other devices on your LAN can reach it. In an *admin* PowerShell:
   ```powershell
   netsh advfirewall firewall add rule name="LibreHardwareMonitor 8085" dir=in action=allow protocol=TCP localport=8085 profile=any
   ```
   (Without this, the PC answers `localhost:8085` but LAN connections are silently dropped.)
5. Verify from another machine: open `http://<pc-ip>:8085/` in a browser (the live table) and
   `http://<pc-ip>:8085/data.json` (the raw tree the device reads). Find `<pc-ip>` with `ipconfig`.

## 2A. Direct — the PCMonitor example

Flash `examples/PCMonitor`, set `WIFI_SSID`/`WIFI_PASS` and
`LHM_URL = "http://<pc-ip>:8085/data.json"`, and you're done — it shows CPU + GPU temperature and
partial-refreshes the numbers as they change.

It finds sensors **by name** in the JSON. Defaults are `CPU Package` (Intel) and `GPU Core`; for
AMD it also tries `Core (Tctl/Tdie)`. To show other values, copy the `lhmTemp()` approach and
search for the exact `Text` you see in `/data.json` (e.g. `CPU Total` for load, `GPU Hot Spot`).
Two real gotchas:
- **Names repeat across categories** — `GPU Core` is a clock, a load *and* a temperature. The
  helper only accepts a match whose value is in °C, which disambiguates it.
- **Phantom readings** — some boards expose a stray sensor that spikes (e.g. a bogus 118 °C
  motherboard zone). If you graph values, sanity-clamp to a plausible range (0–110 °C).

## 2B. Server app — render a nicer frame (Python, recommended)

A small Python program polls LHM and produces an 800×480 frame for the **ThinClient** example, so
you can draw bars/history/multiple machines and change the layout without reflashing. We use a
**Python virtual environment (venv)** — it keeps these packages isolated from the rest of your
system and needs no admin rights.

**Why Python venv over Node here?** The drawing uses Pillow (same as [`SERVER.md`](SERVER.md)),
so it's one ecosystem and one `pip install`; Node would need a native canvas module (a compiler
toolchain). If you already live in Node, it's doable — but Python is the shorter road.

```bash
# one-time setup (any OS; use "py -m venv" / "Scripts\activate" on Windows)
python3 -m venv lhm-env
source lhm-env/bin/activate            # Windows: lhm-env\Scripts\activate
pip install --upgrade pip requests pillow
```

```python
# lhm_frame.py — poll LibreHardwareMonitor and serve an 800x480 gray frame for ThinClient.
# Run inside the venv:  python lhm_frame.py
import http.server, requests
from PIL import Image, ImageDraw, ImageFont

LHM = "http://localhost:8085/data.json"   # or another PC's IP
W, H = 800, 480

def find(node, name, unit="°C"):           # depth-first: value of the named sensor in `unit`
    if node.get("Text") == name and unit in node.get("Value", ""):
        return node["Value"]
    for c in node.get("Children", []):
        v = find(c, name, unit)
        if v: return v
    return None

def render() -> bytes:
    try:
        tree = requests.get(LHM, timeout=4).json()
        cpu = find(tree, "CPU Package") or find(tree, "Core (Tctl/Tdie)") or "--"
        gpu = find(tree, "GPU Core") or "--"
    except Exception:
        cpu = gpu = "offline"
    img = Image.new("L", (W, H), 255); d = ImageDraw.Draw(img); d.fontmode = "1"
    big = ImageFont.load_default(size=110); med = ImageFont.load_default(size=44)
    d.text((40, 30), "PC monitor", fill=0, font=med)
    d.text((40, 120), "CPU", fill=0, font=med); d.text((40, 170), str(cpu), fill=0, font=big)
    d.text((430, 120), "GPU", fill=0, font=med); d.text((430, 170), str(gpu), fill=0, font=big)
    return img.point(lambda p: (p * 3 + 127) // 255).tobytes()

class H_(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] != "/frame.raw": self.send_error(404); return
        b = render(); self.send_response(200)
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self, *a): pass

http.server.HTTPServer(("0.0.0.0", 8080), H_).serve_forever()
```

Point ThinClient's `FRAME_URL` at `http://<this-computer-ip>:8080/frame.raw`. The same pattern
extends to several PCs (poll each LHM URL) or mixing in other data — see
[`HOME_ASSISTANT.md`](HOME_ASSISTANT.md) for pulling Home Assistant entities the same way.
