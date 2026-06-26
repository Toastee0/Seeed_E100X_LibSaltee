# ThinClient frame protocol

The `ThinClient` example is a *thin client*: your server renders the whole UI and the device
just fetches a frame and blits it, using the panel's partial refresh to redraw only what
changed. Change your layout server-side and the display updates with no reflash.

## The frame

A mono (E1001) frame is **exactly 384000 bytes** = `800 × 480`, row-major, **one byte per
pixel**, value `0..3`:

| value | level |
|---|---|
| 0 | black |
| 1 | dark gray |
| 2 | light gray |
| 3 | white |

No header, no compression — just the raw bytes. The device requests
`GET <FRAME_URL>?view=<N>` and expects `200 OK` with the 384000-byte body. `view` lets one
server offer several pages (the Left/Right buttons change `N`).

## Generating a frame (Python / Pillow)

```python
from PIL import Image
img = Image.new("L", (800, 480), 255)        # 8-bit grayscale canvas
# ...draw with ImageDraw...
# quantise to 4 levels (0..3) and emit raw bytes:
buf = bytes((p * 3 + 127) // 255 for p in img.getdata())   # 0..255 -> 0..3
open("frame.raw", "wb").write(buf)           # 384000 bytes
```

Serve `frame.raw` from any HTTP server (Flask, nginx, a 20-line script). For crisp text on
e-paper, render with anti-aliasing **off** so glyphs quantise cleanly.

## How the device decides full vs partial

- **Full 4-gray refresh** on boot, on a `view` change, and every `FULL_EVERY` cycles
  (anti-ghosting).
- **Partial** otherwise: the device diffs the new frame against its on-glass snapshot
  (`refreshChanged()`) and fast-refreshes only the changed bands. Because the diff is on the
  device, the server is completely stateless — just serve the current frame.

If you want *targeted* per-region partials (one box per logical field) rather than diff-derived
bands, compute the regions server-side and call `epd.partial(x, y, w, h)` per region instead —
the engine is the same.
