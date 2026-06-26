# Running a server for the ThinClient example

The **ThinClient** example doesn't draw anything itself — it fetches a finished 800×480 image
from a small server you run on a computer (or a Raspberry Pi, NAS, etc.) on the same network.
That's the appeal: you change how the screen looks by editing the server, and the device updates
with no reflashing. This page gives you a complete server you can copy-paste and run.

## What to install, exactly, and why

- **Python 3.8 or newer** (3.11+ is nice but anything ≥ 3.8 works).
  - *Why this version:* the script uses modern f-strings and the standard-library web server, both
    stable since 3.8. Older Pythons (2.x, or 3.6/3.7) are end-of-life and may choke on the syntax.
  - macOS/Linux usually already have it: run `python3 --version`. Windows: install from
    <https://python.org> and tick **"Add Python to PATH"** during setup.
- **Pillow** (the imaging library), installed with pip:
  ```
  python3 -m pip install --upgrade pillow
  ```
  - *Why Pillow and nothing else:* drawing text/shapes into an image needs an imaging library, and
    Pillow is the de-facto one. The web server itself is Python's built-in `http.server`, so there
    is **no web framework to install** (no Flask, no Node, no npm) — one `pip install` and you're done.
  - *Why not Node?* You can use Node if you prefer (see the note at the bottom), but Python + Pillow
    is the shortest path for most people and matches the frame-drawing examples elsewhere in this repo.

## The server

Save this as `frameserver.py` and run `python3 frameserver.py`. It serves an 800×480 4-level-gray
frame (a live clock + counter) at `http://<your-computer-ip>:8080/frame.raw`. Point the ThinClient
example's `FRAME_URL` at that address.

```python
#!/usr/bin/env python3
# frameserver.py — serve an 800x480, 4-level-gray frame to the reTerminal ThinClient example.
# Requires: Python 3.8+ and Pillow  (python3 -m pip install pillow)
import http.server, datetime
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480

def render(view: int) -> bytes:
    img = Image.new("L", (W, H), 255)          # 8-bit grayscale, white
    d = ImageDraw.Draw(img)
    d.fontmode = "1"                            # no anti-aliasing -> crisp on e-paper
    f_big = ImageFont.load_default(size=140)    # Pillow 10+: load_default takes a size
    f_med = ImageFont.load_default(size=40)
    now = datetime.datetime.now()
    d.text((40, 60),  now.strftime("%H:%M:%S"), fill=0, font=f_big)
    d.text((40, 260), now.strftime("%A, %d %B %Y"), fill=0, font=f_med)
    d.text((40, 340), f"view {view} - edit frameserver.py", fill=80, font=f_med)
    d.text((40, 388), "to change what's shown here", fill=80, font=f_med)
    # quantise 0..255 -> 0..3 (0=black .. 3=white), the format the device expects
    q = img.point(lambda p: (p * 3 + 127) // 255)
    return q.tobytes()                          # exactly 800*480 = 384000 bytes

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] != "/frame.raw":
            self.send_error(404); return
        view = 0
        if "view=" in self.path:
            try: view = int(self.path.split("view=")[1].split("&")[0])
            except ValueError: pass
        body = render(view)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a): pass             # quiet

if __name__ == "__main__":
    print("serving http://0.0.0.0:8080/frame.raw  (Ctrl-C to stop)")
    http.server.HTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
```

## Wiring it to the device

1. Run the server: `python3 frameserver.py`.
2. Find your computer's LAN IP (`ipconfig` on Windows, `ip addr` / `ifconfig` on macOS/Linux) —
   something like `192.168.x.y`.
3. In `examples/ThinClient/ThinClient.ino`, set `FRAME_URL` to
   `http://192.168.x.y:8080/frame.raw` and your WiFi credentials, then flash.
4. The device fetches once a minute, full-refreshes the first time, and **partial-refreshes only
   the digits that changed** after that.

The frame format (one byte per pixel, `0..3`) is documented in [`PROTOCOL.md`](PROTOCOL.md).

## Prefer Node?

Node works too — serve a 384000-byte `Buffer` from `http.createServer`, generating the image with
a canvas library (e.g. `npm i canvas`) and quantising each pixel to `0..3`. Use **Node 18 LTS or
newer** (older versions are end-of-life). The Python version above is recommended only because it's
one `pip install` with no build step; there's nothing device-specific about the choice.
