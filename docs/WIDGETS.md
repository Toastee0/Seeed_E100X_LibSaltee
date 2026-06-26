# The widget model (Dashboard example)

The **Dashboard** example turns a layout into a list of **widgets**. Each widget owns a bounding
box and a `fast` flag, and the whole thing is built around the panel's fast partial refresh: when
a `fast` widget's value changes, **only its box is redrawn** (~0.4 s); everything else rides a
periodic full refresh. You design the layout in a browser (`extras/dashboard.html`), download a
`config.h`, and compile — no C by hand.

## A widget

```c
struct Widget { uint8_t type; int16_t x, y, w, h; bool fast;
                const char* source; const char* fmt; uint8_t size; };
```

| field | meaning |
|---|---|
| `type` | `W_BOX`, `W_BAR`, `W_VALUE`, `W_TEXT`, `W_BLOCK`, `W_QR` |
| `x,y,w,h` | the bounding box — also the partial-refresh region |
| `fast` | `true` → partial-refresh this box when its value changes; `false` → wait for the full refresh |
| `source` | `"scheme:arg"` — where the content comes from (see below) |
| `fmt` | VALUE: a `printf` float format (`"%.1f C"`); BAR: `"min:max"`; TEXT/BLOCK: a suffix |
| `size` | Adafruit_GFX text size |

## Object types

- **Box** — a rectangle outline. Static chrome / grouping. `fast=false`.
- **Bar** — a progress bar filled to `(value-min)/(max-min)`, `fmt="min:max"`. Good `fast` candidate.
- **Value** — a number formatted with `fmt` (a `printf` for one float, e.g. `"%.0f C"`, `"%.1f%%"`).
- **Text line** — one line; `value + fmt`. The natural fast widget (clock, status).
- **Text block** — word-wrapped into the box (~`w / (6·size)` chars per line). Usually `fast=false`.
- **QR** — encodes `source` (text / URL / a `WIFI:…` payload) as a QR sized to the box.

## Sources (and how to add your own)

`source` is `scheme:arg`. The built-in schemes:

| source | gives |
|---|---|
| `static:Some text` | a fixed string |
| `lhm:CPU Package` | a LibreHardwareMonitor sensor from `CFG_DATA_URL` (add `:%`/`:W`/`:M` to disambiguate same-named clock/load/temp) |
| `json:keyname` | the first `"keyname": value` scalar in `CFG_DATA_URL` (your own server, Home Assistant, …) |
| `clock:%H:%M` | NTP time via `strftime` (set your `TZ` in `Dashboard.ino`) |
| `batt` | battery percent |
| `temp` / `hum` | on-board SHT4x temperature / humidity |

**This is the extension point.** Adding a data source is one `if` in `resolve()` in
`Dashboard.ino`:

```cpp
if (scheme == "mqtt")  return myMqttLookup(arg);   // your scheme, your rules
```

That's the whole idea — the foundation stays small, and you grow it. The engine doesn't care
what a source *is*, only that `resolve()` hands back a string.

## Fast refresh, concretely

Each tick the engine re-renders every widget into an off-screen canvas, then:
- on the first tick and every `CFG_FULL_EVERY` ticks → **full gray refresh** (clears ghosting),
- otherwise → for each widget where `fast == true` **and** its value changed since last tick,
  `epd.partial(box)`.

So a clock ticking every second costs one ~0.4 s partial of a small box, not a 4 s full refresh —
and a sensor that didn't change costs nothing. Mark the things that change often as `fast`; leave
titles, boxes, and QR codes `fast=false`.

## Designing one

Open **`extras/dashboard.html`** in any browser (offline is fine). Add widgets, set each one's
type / source / format / box, tick **Fast** for the live ones (green outline in the preview), and
**Download config.h** into `examples/Dashboard/`. Paste your LibreHardwareMonitor `/data.json`
first to get a dropdown of your actual sensors for `lhm:` sources.
