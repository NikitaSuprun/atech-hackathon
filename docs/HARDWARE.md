# Hardware

Two **Atech 14-Port Motherboards** (`board: 14port`, MCU **ESP32-S3**, PlatformIO env
`esp32-s3-devkitc-1`, 8 MB flash / 512 KB RAM). Everything is stock Atech modules that click
into ports — no soldering. The board is portrait: left column = ports 1–6, one isolated
middle slot = port 7, right column = ports 9–14. **Port 8 (USB-C) and port 12 (Reset) are
reserved** and break column adjacency.

> The GPIO numbers below are fixed by the board's port→pin map and confirmed from the
> generated firmware. The console screen firmware (`screen_render`) drives the same 12
> WS2812 data lines the original Pong build did; the brain modules are unchanged. (Today's
> committed `screen/` and `controller/` projects still build the *old* Pong — swapping them
> to `console_os` + `screen_render` is the Integration stage; see [STATE.md](STATE.md).)

## The screen board — 108-pixel matrix

Twelve **3×3 "Light Grid" NeoPixel tiles** = a **6×18, 108-pixel** RGB matrix. Each tile is
9 WS2812 chips. The renderer's `NeoTile` sink drives a **single WS2812 line** per tile (the
stock `NeoPixelGrid` drives two lines A+B every `show()`; line B is dead on this panel, so
dropping it roughly doubles refresh headroom). Brightness is **hard-clamped to 51/255
(≈20%)** in the driver.

Tiles are indexed in **wiring/port order** = `[1,2,3,4,5,6,7,9,10,11,13,14]`; the WS2812
data (line-A) GPIO per tile:

| Tile idx | Port | Data GPIO | Column / rotation (`TILE_MAP`) |
|:---:|:---:|:---:|---|
| 0 | 1 | 9  | left, mounted 180° → rot 2 |
| 1 | 2 | 5  | left, rot 2 |
| 2 | 3 | 17 | left, rot 2 |
| 3 | 4 | 16 | left, rot 2 |
| 4 | 5 | 11 | left, rot 2 |
| 5 | 6 | 13 | left, rot 2 |
| 6 | 7 | 6  | right, upright → rot 0 |
| 7 | 9 | 40 | right, rot 0 |
| 8 | 10 | 1  | right, rot 0 |
| 9 | 11 | 43 | right, rot 0 |
| 10 | 13 | 39 | right, rot 0 |
| 11 | 14 | 36 | right, rot 0 |

Data pins in order: **`[9, 5, 17, 16, 11, 13, 6, 40, 1, 43, 39, 36]`**.

### Tiles → pixels: the compositor

`Compositor` (reused from `modules/pong_screen`) maps the logical 6×18 frame onto the 12
physical tiles via `TILE_MAP[12] = {tileRow, tileCol, rot}` plus a **serpentine** chip order
inside each tile, with a **dirty-tile cache** so only changed tiles are `show()`n. All
screen orientation lives here — the engine, the wire, and the LED driver are
orientation-transparent. `rot` is a pure quarter-turn (0/1/2/3), no mirroring. The
calibrated build (hardware, 2026-07-04): **left column ports 1–6 mounted 180° (`rot 2`),
right column ports 7,9,10,11,13,14 upright (`rot 0`)** — that 180° flip of half the tiles is
what makes twelve 3×3 tiles read as one continuous 6×18 screen. To re-derive on new hardware,
run the identify pattern (each tile = a solid hue + a WHITE corner marking its rotation) and
refill `TILE_MAP` in `modules/pong_screen/pong_config.h`.

## The brain board — controller

| Module | Instance | Ports | Constructor (GPIOs) |
|--------|----------|:-----:|---------------------|
| Rotary encoder (P1, left knob) | `knob_p1` | 1 + 2 | `RotaryEncoder(5, 4, 9, 8)` |
| Rotary encoder (P2, right knob) | `knob_p2` | 9 + 10 | `RotaryEncoder(40, 41, 1, 2)` |
| Speaker (I2S, MAX98357A) | `speaker_1` | 3 + 4 | `Speaker(15, 16, 18)` |
| ST7735 TFT, 160×80 color | `tft_1` | 13 + 14 | `ST7735_TFT(35, 38, 36, 39)` |

Each knob is a rotary encoder with a push-button and its own 12-LED ring. **Knob
acceleration must stay disabled** (`setAcceleration(false, 1)`) for predictable per-detent
feel, and code reads **levels** (`getPosition()`, `isPressed()`) — the bundled loop template
eats `wasPressed()` edges. Speaker volume starts gentle (`0.4` at the driver; the OS master
volume rides on top) — `1.0` clips the MAX98357A.

> Port note: `knob_p2` sits on the right-column pair `[9,10]`. If it physically lands
> elsewhere on hardware day, the only other valid right-column pair is `[10,11]` — one line
> in the project YAML, then re-validate.

## Power ⚠️

108 NeoPixels at full white would pull **≈6.5 A at 5 V**. The 20% brightness clamp keeps the
real draw far below that, but headroom matters: **use a 5 V supply rated ≥3 A** and watch for
**brownout** (`atech check` reports the reset reason — a `BROWNOUT` means the supply sagged).
A weak USB source will reboot the board under a bright frame.

## Board-to-board link

The transport is a swappable seam (`PongLink`: `begin/poll/sendRaw/recvRaw/isUp`).

- **Today — wired serial.** `LinkCobsSerial` sends binary **COBS-framed** frame packets over
  USB-CDC serial. For bring-up, `tools/serial_bridge.py` relays between the two boards
  through a laptop (controller port first), which also lets a laptop stand in for a board.
- **Roadmap — wired UART + ESP-NOW failover.** A direct wired UART between the boards (no
  laptop) with **ESP-NOW** as a wireless failover. The seam already anticipates it: a full
  108-px frame is 229 B, inside one 250-B ESP-NOW datagram (`static_assert`ed), and
  `LinkEspNow` is an in-tree stub (`isUp()` false) that a real `esp_now_init` /
  `esp_now_send` implementation drops into — the "two-file swap" the original build proved by
  swapping WiFi→USB in ~20 minutes. Datagram links carry the packet raw (no COBS); only the
  byte-stream serial link frames it.

## Flash flow

```bash
uv run atech ports                         # find the two boards (identical USB descriptors — label them)
uv run atech build   screen                # codegen + PlatformIO compile (first build downloads Adafruit libs)
uv run atech upload  screen --port <port>  # ALWAYS build first — upload reuses a stale firmware.bin otherwise
uv run atech build   controller
uv run atech upload  controller --port <port>
uv run atech check   --port <port>         # module health + reset reason (BROWNOUT watch)
# link the two boards for play (controller port first):
uv run --group dev python -u tools/serial_bridge.py <controller-port> <screen-port>
```

Both boards enumerate with **identical** USB descriptors, so re-identify by serial output,
not by port number, and stop the bridge (`pkill -f serial_bridge`) before any reflash — it
holds both ports.
