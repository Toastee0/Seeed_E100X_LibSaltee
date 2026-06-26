# Showing Home Assistant data on the display

Home Assistant (including Home Assistant OS / HAOS) has a REST API. A small program reads the
entities you care about and renders an 800×480 frame for the **ThinClient** example — so the
panel becomes a clean, glanceable HA dashboard that you restyle server-side, no reflashing.

Same approach as [`PC_MONITOR.md`](PC_MONITOR.md): poll → draw → serve. The device stays dumb; all
the Home Assistant logic lives in one Python script you run on any always-on machine (a PC, a
Raspberry Pi, or HA itself).

## 1. Get a token and your entity IDs

1. In Home Assistant, click your **user name** (bottom-left) → scroll to **Long-Lived Access
   Tokens** → **Create Token**. Copy it now — you can't see it again. Treat it like a password.
2. Find the entities you want: **Developer Tools → States**. Each row is an `entity_id` like
   `sensor.living_room_temperature`, `binary_sensor.front_door`, `weather.home`. Note the ones
   you want on the screen.
3. Your HA address is something like `http://homeassistant.local:8123` or `http://<ha-ip>:8123`.

Test it from a terminal (replace the address, token, and entity):
```bash
curl -s -H "Authorization: Bearer YOUR_TOKEN" \
  http://homeassistant.local:8123/api/states/sensor.living_room_temperature
```
You'll get JSON with a `"state"` field and `"attributes"`.

## 2. The renderer (Python venv, recommended)

A virtual environment keeps these two packages isolated and needs no admin rights:
```bash
python3 -m venv ha-env
source ha-env/bin/activate              # Windows: ha-env\Scripts\activate
pip install --upgrade pip requests pillow
```

```python
# ha_frame.py — render selected Home Assistant entities to an 800x480 frame for ThinClient.
# Run inside the venv:  python ha_frame.py
import http.server, requests
from PIL import Image, ImageDraw, ImageFont

HA    = "http://homeassistant.local:8123"
TOKEN = "PASTE_YOUR_LONG_LIVED_TOKEN_HERE"
# (label, entity_id, unit-to-append)
ROWS = [
    ("Living room", "sensor.living_room_temperature", " C"),
    ("Humidity",    "sensor.living_room_humidity",    " %"),
    ("Front door",  "binary_sensor.front_door",       ""),
    ("Outside",     "sensor.outdoor_temperature",     " C"),
]
W, H = 800, 480
HDRS = {"Authorization": f"Bearer {TOKEN}"}

def state(entity):
    try:
        r = requests.get(f"{HA}/api/states/{entity}", headers=HDRS, timeout=4)
        return r.json().get("state", "?")
    except Exception:
        return "offline"

def render() -> bytes:
    img = Image.new("L", (W, H), 255); d = ImageDraw.Draw(img); d.fontmode = "1"
    title = ImageFont.load_default(size=40); lab = ImageFont.load_default(size=34)
    val = ImageFont.load_default(size=48)
    d.text((30, 24), "Home", fill=0, font=title); d.line((30, 80, W-30, 80), fill=0)
    y = 110
    for label, entity, unit in ROWS:
        d.text((40, y), label, fill=80, font=lab)
        d.text((430, y-6), str(state(entity)) + unit, fill=0, font=val)
        y += 88
    return img.point(lambda p: (p * 3 + 127) // 255).tobytes()

class H_(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] != "/frame.raw": self.send_error(404); return
        b = render(); self.send_response(200)
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def log_message(self, *a): pass

http.server.HTTPServer(("0.0.0.0", 8080), H_).serve_forever()
```

Point ThinClient's `FRAME_URL` at `http://<this-computer-ip>:8080/frame.raw`. Edit `ROWS` to add
whatever entities you like; the renderer only fetches the ones listed, so it's cheap.

## Running it on Home Assistant itself

If you'd rather not keep a separate machine on, run `ha_frame.py` on the HA host — for HAOS, the
clean way is a small **custom add-on** (a Dockerfile that `pip install`s requests+pillow and runs
the script) or via the **Advanced SSH & Web Terminal** add-on with a Python venv. On a supervised
/ container install you can just run it alongside HA. The script itself is identical; only where
it runs changes.

## Why a server (not direct on-device) for Home Assistant?

For one or two PC temperatures, parsing JSON on the device (see PCMonitor) is fine. Home Assistant
responses are bigger and you usually want several entities laid out nicely with fonts and icons —
that's exactly what a render step is good at, and it keeps your token on a real computer instead of
baked into the device's flash.
