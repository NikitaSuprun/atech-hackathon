# Two-Board Pong on Atech Hardware — Master Plan

> Status 2026-07-04: plan finalized & approved. No game code written yet.
> Companion doc: [HANDOVER.md](HANDOVER.md) (operational quick-start).
> Everything below was verified against the installed SDK source — paths included so nothing needs re-exploring.

## Context

atech.dev hackathon: two-player Pong across two Atech 14-Port boards (ESP32-S3) over WiFi.

- **Screen board** — **12× Light Grid** (`neopixel`, 3×3 WS2812): ports 1–6 plugged directly (tile column 0), ports 7,9,10,11,13,14 mounted **flipped 180°** alongside (tile column 1) → **6 px × 18 px wall** (108 px, 2 tile-cols × 6 tile-rows). Authoritative game engine + **WiFi SoftAP** + UDP server. All 12 usable ports occupied.
- **Controller board** ("console") — knob P1 (1,2), knob P2 (9,10)*, I2S **speaker** (4,5), 160×80 **TFT** (13,14), virtual `pong_control` module on port 7 (free: 3, 6, 11). WiFi STA + UDP client; hosts audio/score/ring presenters.

\* User recalled "8,9" but port_8 = reserved USB-C; (9,10) is the valid neighbor pair (alternative [10,11]; [13,14] taken by TFT). One-line yaml fix on hardware day; `atech validate`/`monitor` reveals instantly.

**Locked decisions:** first to **3**; portrait, paddles on top/bottom short edges, **paddle 2 px, ball 1 px**; **no standalone Button** — start / continue-after-every-point / rematch all require **both players holding their knob push-buttons**; modular state-machine engine; state/event-driven music; TFT shows per-player points.

## Ground truth (verified in installed SDK source; two design agents re-verified independently)

SDK `atech 1.0.0a3` at `.venv/lib/python3.11/site-packages/atech/` in this repo; PlatformIO S3 toolchain cached in `~/.platformio`; Adafruit NeoPixel/GFX/ST7735 libs download on first build → **run one full build before leaving good WiFi**.

- `project.yaml` → `uv run atech validate/build/upload` → generated Arduino C++ (`esp32-s3-devkitc-1`). All game logic on-board C++; Python only builds/flashes/monitors.
- **Custom modules** (`modules_path: ../modules`): folder copied **flat** (top-level files only, no recursion — `atech/catalog/loader.py:iter_module_files`) to `lib/atech_<id>/`, header auto-included in main.cpp, Jinja `globals/setup/loop` templates stitched in **add-order** (`atech/codegen.py`); templates may reference earlier instances by name; PlatformIO LDF resolves cross-lib includes; same-id external module **overrides bundled** (`atech/project.py:_catalog`) — escape hatch to patch the neopixel driver.
- **SDK ships zero networking** (verified in source + atech.dev/docs + PyPI README) → hand-rolled SoftAP + WiFiUDP (arduino-espressif32 core, no lib_deps).
- ⚠ Bundled knob loop template runs before ours and **consumes `wasPressed()/wasReleased()` edges** → we transmit **levels** (`isPressed()`, `getPosition()`) — which the hold-to-ready mechanic wants anyway. Their serial JSON prints = free `atech monitor` diagnostics.
- ⚠ Knob **acceleration is applied inside the ISR** — multiplied into the cumulative count, irreversible downstream → `setAcceleration(false, 1)` in our setup template is mandatory.
- ⚠ Knob ring = **single glowing dot with trail** (`setRingPosition(float 0..12)` CCW from 12 o'clock; NAN = re-couple to knob), NOT an addressable arc — all ring effects are (color, brightness, dot-position) motions.
- ⚠ `atech send` actions are dead in this alpha (codegen emits no serial action dispatcher) — not used; all control is UDP.
- Codegen auto-resolves size-2 pin orders incl. the left-column 180° swap: emits e.g. `Speaker speaker_1(10,11,15)`, `ST7735_TFT tft_1(35,38,36,39)` — **never hand-wire pins**.
- Driver APIs (source: `.venv/.../atech/catalog/data/modules/<id>/*.h`):
  - `NeoPixelGrid` — setPixel(i∈0-8,r,g,b) row-major (i=row*3+col), setPixelXY, setAll, setRow, setColumn, clear(), show() (pushes BOTH line-A RGB and line-B RGBW strips), setBrightness **hard-clamped ≤51/255 (20%)**, getPixelColor.
  - `RotaryEncoder` — interrupt-driven; getPosition() int32 cumulative, DETENTS_PER_REV=18; isPressed()/wasPressed(); setAcceleration(bool,max=5); ring: setRingColor(r,g,b), setRingBrightness(0-255), setRingPosition(float), enableRing(bool); update() each loop (bundled template does it).
  - `Speaker` (MAX98357A I2S) — ALL non-blocking (bg FreeRTOS task): playTone(freq,ms), playNote(freq,ms) chainable (85% sound/15% gap), playChord(float*,n≤6,ms), playRTTTL("name:d=4,o=5,b=140:c,e,g"), stop(), isPlaying(), setVolume(0-1, keep ≤0.5; bundled setup sets 0.4). NOTE_C3..NOTE_B6 constants; sweet spot C4-B5. Never play on boot; never unguarded in loop().
  - `ST7735_TFT` — 160×80 canvas mode: clear() → setCursor/setTextSize/setTextColor/print(one arg) / displayText(str,x,y,size,color) / fillRect / drawRect / circles / lines → display(); COLOR_BLACK/_WHITE/_RED/_GREEN/_BLUE/_CYAN/_MAGENTA/_YELLOW/_ORANGE (RGB565); setRotation(0-3); size-N char = 6N×8N px. Full display() ≈ 6-10 ms SPI → **event-driven redraws only**.
- 14-port board (`.venv/.../atech/catalog/data/boards/14port.yaml`): left col ports 1-6 top→bottom, middle port 7 top + USB-C (port 8, **reserved**) bottom, right col 9,10,11,[port 12 = Reset, **reserved**],13,14. Valid size-2 pairs: (1,2)(2,3)(3,4)(4,5)(5,6)(9,10)(10,11)(13,14). GPIO map in that yaml.

## Architecture

```
┌────────── SCREEN (server, SoftAP) ────────────────┐   ┌───────── CONTROLLER (console, STA) ─────────────┐
│ 11× neopixel (g1..g13, port-named) + pong_screen  │   │ knob_p1(1,2) knob_p2(9,10)                      │
│ module on port 14 = hosts 12th grid + engine      │◄──┤ speaker_1(4,5) tft_1(13,14) pong_control(7)     │
│  • pong::Engine — pure C++ state machine, 6×18    │UDP│  • sends levels @50 Hz: knobPos[2] + heldBits   │
│  • Compositor: 12-tile rotation LUT + dirty cache │──►│  • LinkUdpClient: STA + 5 s reconnect kick      │
│  • LinkUdpServer: SoftAP + reply-to-source        │   │  • FeedbackTracker (pure) → exactly-once Cues   │
│  • identify pattern during LINK_WAIT              │   │  • presenters: AudioDirector / ScoreDisplay /   │
│ INPUT 24 B @50 Hz  ·  FEEDBACK 24 B @20 Hz        │   │    RingFX — dumb consumers of (snapshot, Cues)  │
└───────────────────────────────────────────────────┘   └─────────────────────────────────────────────────┘
```

Loss/reboot-proof by construction:
- Knob positions travel **absolute** (cumulative int32); server applies **clamped deltas** (over-rotation at walls discarded → first reverse detent responds instantly; no drift, no dead-knob; first packet anchors without a jump).
- Button **levels** re-established by any packet; ready timing is server-clocked → loss/bounce-immune.
- One-shot cues = **wrapping uint8 counters**; receiver fires on `uint8(new−last) > 0` then latches (duplicates fire nothing; a lost packet's cue fires on the next). **Cold-start/gap resync adopts counters without firing** → no jingle replay after reconnect.
- Seq reorder guards both directions; controller reboot detected via uptimeMs regression → paddle re-anchor + counter resync (no phantom presses).
- Reply-to-source feedback (server learns each controller's ip:port; DHCP just works; controllerId 0..3 → a future 2nd controller board joins with zero protocol change).
- Server → LINK_WAIT after 1 s input silence (ball frozen; resume → READY_CHECK = natural pause). Controller flags link-lost after 2.5 s feedback silence. Boot-order independent (SoftAP synchronous in setup; STA re-kick every 5 s).
- `WiFi.persistent(false)` + `WiFi.setSleep(false)` both sides (sleep adds 100 ms-class jitter); nothing ever blocks loop() (watchdog-safe; worst screen loop ≈ 10 ms incl. 12-tile repaint, worst controller loop ≈ 12 ms on a TFT redraw tick).

## Screen-board module trick (all 12 ports are grids — no spare slot)

`pong_screen` **is the 12th grid**: placed on port 14, its templates instantiate BOTH that tile's `NeoPixelGrid` and the engine; `pong_screen.h` does `#include "neopixel.h"` (LDF resolves). Grid instances are **port-named** (`g1..g13`) so hardware-day remapping edits ONLY the TILE_MAP table, never project.yaml:

```yaml
# modules/pong_screen/module.yaml — category display, size 1, health_check "{{ instance }}.ok()"
templates:
  globals: |
    NeoPixelGrid {{ instance }}_grid({{ pin_a }}, {{ pin_b }});
    PongScreen {{ instance }};
  setup: |
    {{ instance }}_grid.begin();
    {
      NeoPixelGrid* _t[12] = { &g1, &g2, &g3, &g4, &g5, &g6,
                               &g7, &g9, &g10, &g11, &g13, &{{ instance }}_grid };
      {{ instance }}.begin(_t, 12);   // index order = port order 1..7,9..11,13,14
    }
  loop: "{{ instance }}.tick();"
```

`screen/project.yaml`: 11× neopixel `g1..g6` (ports 1-6), `g7`(7), `g9..g11`(9-11), `g13`(13), then `pong_screen` instance `pong` port 14 **last** (add-order!). `health_check` is safe because `WiFi.softAP()` returns synchronously → `atech check` proves the AP came up.

`modules/pong_control/module.yaml` — category `connectivity`, size 1, **no health_check** (STA not associated by end of setup(); a false would make `atech check` cry wolf):

```yaml
templates:
  globals: "PongControl {{ instance }};"
  setup: |
    knob_p1.setAcceleration(false, 1);
    knob_p2.setAcceleration(false, 1);
    {{ instance }}.begin(&knob_p1, &knob_p2, &speaker_1, &tft_1);
  loop: |
    {{ instance }}.service(
        knob_p1.getPosition(), knob_p1.isPressed(),
        knob_p2.getPosition(), knob_p2.isPressed());
```

`controller/project.yaml`: rotary_encoder knob_p1 [1,2] · knob_p2 [9,10] (comment: valid alternate [10,11]) · speaker speaker_1 [4,5] · st7735_tft tft_1 [13,14] · pong_control `pongc` port 7 **last**. Speaker's bundled setup already runs `begin()` + `setVolume(0.4f)`; speaker & TFT bundled loop templates are empty (no edge-eating there).

## Repo layout

```
modules/pong_screen/   module.yaml · pong_screen.h/.cpp (glue: hosts 12th grid, net pump,
                       fixed-step engine driver, wall push, feedback tx, identify pattern) ·
                       link_udp_server.h/.cpp
                       PURE: pong_engine.h/.cpp · pong_frame.h (W=6,H=18) · compositor.h/.cpp
                       (12-tile rotation LUT + dirty cache) · pong_config.h (tunables + TILE_MAP)
                       SHARED CANONICAL: pong_proto.h · pong_link.h · net_config.h
modules/pong_control/  module.yaml · pong_control.h/.cpp (sampler, 50 Hz sender, presenter host) ·
                       link_udp_client.h/.cpp · feedback_tracker.h (PURE: Cues, exactly-once) ·
                       audio_director.h/.cpp · score_display.h/.cpp · ring_fx.h/.cpp ·
                       SYMLINKS → ../pong_screen/{pong_proto,pong_link,net_config}.h
                       (loader is plain pathlib → follows symlinks; ci.sh asserts; macOS+git safe)
screen/project.yaml · controller/project.yaml   (build/ dirs gitignored, never edited)
sim/main.cpp + Makefile   (g++ -I ../modules/pong_screen; POSIX only)
tools/fake_controller.py  (laptop joins SoftAP; keyboard knobs + hold keys; prints feedback cues)
tools/ci.sh               (validate+build both projects + build sim + symlink assertions)
```

Purity rule: engine/frame/compositor/proto/link/net_config/feedback_tracker include only `<stdint.h>/<stddef.h>/<math.h>/<string.h>` — same files compile on ESP32 and desktop g++. Arduino appears only in glue + link impls + presenter bindings.

## Wire protocol v2 (pong_proto.h — packed LE both ends, static_assert sizes)

```
PongInputPacket (24 B, controller→screen, 50 Hz):
  magic 'PONG' u32 · version=2 u8 · type=0x01 u8 · controllerId u8 (0; future 1..3)
  heldBits u8 (bit0=P1 held, bit1=P2) · seq u32 · uptimeMs u32 · knobPos[2] i32 (ABSOLUTE)

PongFeedbackPacket (24 B, screen→controller, 20 Hz):
  magic/version/type=0x02/targetControllerId · gameState u8 · seq u32 · score[2] u8
  heldBits echo u8 · readyProgress[2] u8 (0..255 = holdMs/READY_HOLD_MS)
  event counters (wrapping u8): paddleHitSeq · goalSeq + goalBy · winSeq + winner
                                 · serveSeq + servingPlayer   (+ eventFlags bit0 = wall bounce)
```

`enum PongGameState : uint8_t { GS_LINK_WAIT, GS_READY_CHECK, GS_COUNTDOWN, GS_PLAYING, GS_POINT_FLASH, GS_GAME_OVER }` · `PONG_WIN_SCORE = 3`.

Validation: exact size + magic + version + type, else silent drop. Controller-derived values (NOT in packet, computed by FeedbackTracker): per-player lock events (readyProgress crossing 255), both-locked (state → GS_COUNTDOWN), countdown ticks (local timer inside GS_COUNTDOWN), rallyHits (count of paddleHitSeq deltas since last serveSeq change — drives hit-blip pitch creep).

net_config.h: SSID `atech-pong` / WPA2 `pong4242`, channel 6 (retune on site: least-busy of 1/6/11), server 192.168.4.1:47420, client binds 47421, INPUT_PERIOD 20 ms, FEEDBACK_PERIOD 50 ms, INPUT_TIMEOUT 1000 ms, FEEDBACK_TIMEOUT 2500 ms, STA_REKICK 5000 ms, **READY_HOLD_MS 500**. `PongLink` interface (begin/poll/sendRaw/recvRaw/isUp) = ESP-NOW swap seam (same 24 B payloads; broadcast discovery then unicast; costs the laptop fake-controller debug path, so UDP stays primary). Total traffic ≈ 14 kbit/s.

## Game state machine (engine-internal; server-authoritative)

`GS_LINK_WAIT` (never-linked → identify pattern; mid-game → frozen frame + breathe) → `GS_READY_CHECK` (start / after EVERY point / rematch gate) → `GS_COUNTDOWN` (~0.8-1.5 s; ball glued to server's paddle face, moves with it — teaches the mapping; blink 4 Hz; serve = conceder of last point, first serve random; launch angle ±(0.35|0.70)·60° random, never pure vertical, toward the roomier side; speed resets) → `GS_PLAYING` → goal → `GS_POINT_FLASH` (~1.2 s: 2 full-wall scorer-color flashes then score pips) → score<3 → READY_CHECK; score=3 → `GS_GAME_OVER` (winner-color wave from winner's edge + score bars; ≥3 s) → both-hold → scores 0-0 → **straight to COUNTDOWN** (no double gate).

**Ready mechanic (server-clocked = inherently debounced, loss-immune):** in READY_CHECK, per player `holdMs += dt` while `held[i] && linked`, **reset to 0 on release**; progress caps and *stays* capped while held (first player locks in and waits; lock event fires once on crossing); both capped → advance + serveSeq++. During stale input, holds freeze then decay after 1 s. Idle in READY_CHECK > ~20-30 s → attract demo rally (engine AI: track predicted ball x + wobble, capped speed, forced miss every ~3rd rally; ×0.45-0.6 palette) until any hold begins.

**Physics (pure floats, 6×18, tick 50 Hz):** ball = point, radius 0.5 cells; render px = floor. Paddle rows y=0 (P2) / y=17 (P1); reflection planes at y=1.5 / y=16.5 (ball never overlaps paddle row; a miss visibly whiffs past for ~2 frames before the goal fires at y<0 / y>18). Swept crossing test per tick (no tunneling; max step ≈ 0.44 cells/tick at cap):

```cpp
// bottom paddle (P1), plane 16.5, ball moving down:
if (vy > 0 && by <= 16.5f && ny > 16.5f) {
  float t = (16.5f - by) / (ny - by);
  float xc = foldX(bx + vx*DT*t);                       // triangle-fold into [0,W]
  float u  = (xc - padCenter(P1)) / (PADDLE_W*0.5f + PADDLE_GRACE);
  if (fabsf(u) <= 1.0f) bounceOffPaddle(P1, clampf(u,-1,1));
}
// english with hard speed control (speed magnitude stays exactly on the ramp):
speed = fminf(speed * SPEEDUP_PER_HIT, BALL_SPEED_MAX);
float ux = clampf(u * ENGLISH_GAIN, -MAX_UX, MAX_UX);   // MAX_UX = sqrt(1 - VY_MIN²)
float uy = sqrtf(1.0f - ux*ux);                         // ≥ VY_MIN → never stalls sideways
vx = ux*speed;  vy = (p==P1 ? -uy : uy) * speed;
```

ENGLISH_GAIN 0.85, VY_MIN 0.55, PADDLE_GRACE 0.35 (edge-graze = extreme angle = the skill move; hit offset spans ~5 usable zones on a 2-px paddle). Collision uses the **rounded render position** — hitbox is exactly what you see. Speeds for 18-tall: start 7.0 cells/s (first crossing ≈ 2.1 s), ×1.07/hit, cap 22 (crossing ≈ 0.7-1.3 s) — settle in sim.

**Knob→paddle:** clamped delta accumulator (`int32` two's-complement subtraction = wrap-safe); DETENTS_PER_CELL 2.0 → full 4-cell sweep = 8 detents ≈ 160° (tune 1.5–3.0 in sim); KNOB_SIGN[2] config (flip per side on hardware day in 5 s); paddle float clamps [0, W−PADDLE_W] so over-travel builds no hidden offset.

**Engine contract (frozen):** `reset(seed)` · `tick(EngineInputs{knobPos[2], held[2], controllerLinked, controllerRebooted}, dtMs)` — fixed 16-20 ms steps, cap 5/loop · `render(Frame&)` 6×18 RGB · `status()` → EngineStatus ≡ feedback payload fields (glue memcpys) · xorshift32 RNG (glue seeds esp_random(), sim time(0)) · zero Arduino/wall-clock deps; state handlers as enterX()/tickX() pairs (switch dispatch, ≤40 lines each); `draw()` repaints the framebuffer from state every tick (no incremental-render bugs).

**Palette for the hard 51/255 clamp** — effective PWM = v/5, values <25 quantize to garbage → only two intensity tiers: accents (saturated, ≥200) and dims (70–90), nothing between. P1 cyan (0,200,255) bottom · P2 amber (255,120,0) top (CVD-safe blue/orange; avoids pure blue = dimmest-reading channel) · ball white (255,255,255) · net: 2 dim dots (70,70,70) at mid-field row 9, cols 1 & 4, suppressed under ball, config-flagged · concede flash red · attract ×0.45–0.6 · stale indicator: amber blink at pixel (0,0). **No ball trail** (at this resolution a trail is a second ball; speed-tinted ball is the polish alternative). Score pips: 3-segment bars (each point = 2 px wide) row 2 amber / row 15 cyan; newest blinks; full bar = match point, leader's row breathes.

**READY_CHECK wall UI:** not-held → player's paddle row pulses their color (0.5 Hz, FB 70↔255); holding → row fills column-by-column left→right (6 cols = 6 steps of the 500 ms) with bright cursor at fill edge; locked → one white full-row blink then steady; both → countdown.

**Compositor:** `TILE_MAP[12] {tileRow 0-5, tileCol 0-1, rot}` in pong_config.h, index = port order (1,2,3,4,5,6,7,9,10,11,13,14). **Default from known geometry: ports 1-6 → col 0 rows 0-5 rot 0; ports 7,9,10,11,13,14 → col 1 rows 0-5 rot 180°.** LED local (lr,lc) on a tile rotated CW by θ displays framebuffer region cell: θ=0 → (lr,lc) · 90° → (lc, 2−lr) · 180° → (2−lr, 2−lc) · 270° → (2−lc, lr). Startup LUT `lut[12][9] = fbByteOffset`; runtime = 108 array reads + per-tile 27-B memcmp vs cache → show() only dirty tiles (rally touches 1–3; full repaint 12 tiles ≈ 7 ms, fits the 20 ms tick) + force repaint on state change + **2 s heartbeat repaint** (WS2812s glitch pixels from electrical noise at parties; heartbeat heals invisibly).

**Calibration (~10 min, one photo):** identify mode runs during never-linked LINK_WAIT: tile i (port order) solid identity color — 6 hues (R,G,B,Y,M,C @180) reused per column (columns spatially unambiguous) — with **local pixel 0 = WHITE** (rotation corner: physically TL=0°, TR=90°, BR=180°, BL=270°) and **local pixel 1 = dim gray** (must sit edge-adjacent to white along the tile's local top row — guards against diagonal misreads); white pixel blinks (tile-index+1) times every 3 s as a wiring-order double check. Fill TILE_MAP (legend printed as comment above it) → rebuild → **verify mode**: full-wall glyph asymmetric on both axes (an "F" in the top half + a down-arrow in the bottom half, P2 color top row / P1 color bottom row) — any scrambled 3×3 patch pinpoints the one wrong entry. Doubles as the **RMT go/no-go for 24 strip objects**.

## Controller presenters (dumb consumers of `(snapshot, Cues)`; all dedupe in FeedbackTracker)

`FeedbackTracker` (pure): `feed(buf,len,now)` validates + latches; `poll(now)` → `Cues{stateChanged(+prev), scoreChanged, lockP1, lockP2, bothLocked, paddleHit(+rallyHits), wallBounce, goal+goalBy, win+winner, serve+servingPlayer, linkLost, linkRestored}`; adopt-without-firing on first packet after boot or a feedback gap.

**AudioDirector** — priority WIN > POINT > READY/SERVE > blips; `stop()` before any RTTTL; suppress blips while a jingle plays (`isPlaying()` + own flag); never on boot; volume ≤0.5:

| Cue | Sound |
|---|---|
| lockP1 | `playNote(NOTE_C5,70); playNote(NOTE_E5,110);` |
| lockP2 | `playNote(NOTE_E5,70); playNote(NOTE_G5,110);` (per-player identity) |
| bothLocked | `float c[3]={NOTE_C5,NOTE_E5,NOTE_G5}; playChord(c,3,350);` |
| countdown tick (local timer) | `playTone(NOTE_G4,60);` · launch: `playTone(NOTE_G5,140);` |
| paddleHit | `playTone(880 + 15*min(rallyHits,10), 30)` P1 / base 659 Hz P2 — pitch creeps with rally length, ear tracks direction |
| wallBounce (flag) | `playTone(NOTE_E4,20);` — config `WALL_BLIP_ENABLED`, cut-first |
| goal (you scored) | `playRTTTL("p1:d=16,o=5,b=200:c,e,g,8c6")` rising C-major ≈0.37 s |
| goal (conceded) | `playRTTTL("p2:d=16,o=5,b=200:e,g,b,8e6")` same shape E-minor — whose point, by ear |
| match point reached | `playRTTTL("mp:d=16,o=5,b=120:8p,g,8g#")` half-step tension sting (polish) |
| win | `playRTTTL("champ:d=8,o=5,b=180:g,c6,e6,4g6,8e6,2g6")` — "Charge!", ≈1.7 s |
| linkLost / linkRestored | `playTone(NOTE_C4,120)` low / `playNote(NOTE_C5,60); playNote(NOTE_G5,60)` |
| attract | silence (optional 45 s chirp behind flag, default off) |
| rally pulse (optional, default OFF) | `playTone(NOTE_C3,25)` heartbeat, interval lerp 600→250 ms with rally length; suppressed 150 ms after any blip |

**ScoreDisplay** (TFT; redraw ONLY when quantized TftModel changes: state/ctx/scores/holdProgress÷16/rallyHits/link — `display()` ≈6-10 ms, ≤10 Hz worst case). Score line pattern: P1 digit (44,8) size 4 cyan · ":" (68,8) white · P2 digit (92,8) size 4 amber:

| State | Layout |
|---|---|
| ATTRACT | "PONG" (44,10,3,white) · "HOLD BOTH KNOBS" (35,48,1,gray) |
| READY (match start) | "HOLD TO START" (41,6,1,white) · hold bars: drawRect(10,30,60,12,P1)+fillRect(10,30,w,12,P1) / (90,30,60,12,P2) · "P1"(34,46,1) "P2"(114,46,1) · locked → bar full + "READY" |
| READY (next point) | score line · "HOLD FOR NEXT POINT" (23,42,1,white) · mini bars (10,56,60,12)/(90,56,60,12) |
| READY (rematch) | score line · "HOLD FOR REMATCH" (32,42,1,winner color) · mini bars |
| COUNTDOWN | score line · big "3/2/1" (74,44,3,white) |
| PLAYING | score line · "RALLY n" (59,60,1,gray) on hits |
| POINT_FLASH | fillScreen(scorer color) · "POINT P1!" (26,32,2,black) |
| GAME_OVER | "P1 WINS" (17,12,3,winner color) · score line (44,44) |
| link lost | "SEARCHING WALL…" (29,36,1,amber) + last score small at top |

Boot frame "PONG / connecting" drawn in begin() (drawing on boot fine; sound not).

**RingFX** (dot choreography; only owner of ring calls; permanent override during game — never let it snap back to knob-follow unintentionally):

| Phase | Ring behavior |
|---|---|
| ATTRACT | dim white, brightness sine 10↔50 |
| READY not-held | player color, brightness 40, dot slow-rotating 1 rev/4 s ("touch me") |
| READY holding | dot = clock hand: pos = readyProgress×12/255 sweeping to 12 o'clock |
| locked | brightness pop 255 for 150 ms → steady 120 at pos 12 |
| COUNTDOWN | dot steps 12→8→4 with ticks, snap 0 at launch |
| PLAYING | score dial: parked at score×4, dim (b 40), player color |
| goal conceded | red, 3× 200 ms brightness flashes → back to dial |
| goal scored | one fast full sweep 0→12 in 300 ms ("+1") landing on new score pos |
| GAME_OVER winner / loser | spin 2 rev/s full brightness / dim red static |
| link lost | slow red blink both |

## Tuning table (pong_config.h — one file = whole game feel)

TICK_HZ 50 · GRID 6×18 · NUM_TILES 12 · PADDLE_W 2 (3 = casual mode) · PADDLE_GRACE 0.35 · DETENTS_PER_CELL 2.0 · KNOB_SIGN {+1,−1} · BALL_SPEED_START 7.0 · SPEEDUP_PER_HIT 1.07 · BALL_SPEED_MAX 22 · ENGLISH_GAIN 0.85 · VY_MIN 0.55 · SERVE_U {0.35,0.70} · WIN_SCORE 3 · READY_HOLD_MS 500 · COUNTDOWN_MS 1500 (3 ticks à 500) · POINT_FLASH_MS 1200 · WIN_CELEBRATION_MS 3000 · IDLE_TO_ATTRACT_MS 30000 · STALE_HOLD_RESET_MS 1000 · palette above · RING_BRIGHT 40/20/60-120 · WALL_BRIGHTNESS 40 (≤51) · TILE_MAP default col0 rot0 / col1 rot180 · HEARTBEAT_REPAINT_S 2 · flags: NET_DOTS on · WALL_BLIP on · RALLY_PULSE off · ATTRACT_CHIRP off · SPEED_TINT_BALL off · CALIBRATION_MODE off.

## Desktop simulator (sim/main.cpp — tune everything today, zero hardware)

POSIX only, ~150 lines: termios raw (−ICANON −ECHO, VMIN=0), fixed 50 Hz (clock_gettime + sleep remainder, catch-up cap 4), `\x1b[H` home (no clear = flicker-free), each px `\x1b[48;2;R;G;Bm  ` (2 spaces), 18 rows + side panel: state, scores, hold %, speed, last 5 audio cues with sound description (`♪ HIT_P1 880Hz 30ms`), `TFT| …` model line, `[HOLD]` badges. Keys: `a/z` P1 knob ∓2 detents, `k/m` P2, **`s`/`l` toggle P1/P2 held** (raw mode has no key-release), `t` toggle stale, `q` quit. Keyboard feeds the same int32-counter → delta path as hardware → knob feel constants transfer directly. Optional `--tiles`: render through the Compositor LUT into 12 spaced pseudo-tiles (verify rotation math + the calibration glyph pre-hardware). Optional SIM_DIM flag: ×51/255 with 0.5-gamma preview of real-wall brightness.

## Parallel workstreams (7 conflict-free lanes after contracts freeze)

**Phase 0** (~1 h, ONE owner, blocks all): pong_frame.h · pong_engine.h (API + EngineStatus) · pong_proto.h · pong_link.h · net_config.h · feedback_tracker.h (Cues API) · both project.yaml + module.yaml. Append-only afterward; version byte guards the wire.

| Lane | Files | Note |
|---|---|---|
| A engine | pong_engine.cpp | pairs with G's sim for tuning |
| B screen glue | pong_screen.*, link_udp_server.*, compositor.*, identify/verify modes | compiles day-0 vs stub engine (test pattern) |
| C controller core | pong_control.*, link_udp_client.*, tracker impl, symlinks | ships presenter **stubs** → `atech build controller` green day-0 |
| D AudioDirector | audio_director.* | drops in as file-pair swap |
| E ScoreDisplay | score_display.* | 〃 |
| F RingFX | ring_fx.* | 〃 |
| G sim + tools | sim/*, tools/*, .gitignore | fake_controller.py proves server before controller board exists |

Integration: contracts → all lanes parallel → A into screen, D/E/F into controller → `tools/ci.sh` green = pre-hardware done → hardware day. Implementation fans lanes out to parallel subagents.

## Verification

Pre-hardware: `uv run atech validate/build screen|controller` clean · sim full match (score to 3, both-hold via toggles, cues fire exactly once, counters monotonic) · ci.sh green · **one full build on good WiFi** (Adafruit lib downloads).

Hardware bring-up (kills biggest unknowns first):
1. Flash screen (`uv run atech upload screen --port <SCREEN>`) → identify pattern = confirm/fix TILE_MAP **+ RMT go/no-go for 24 strips** → rebuild → verify-glyph then attract-rally continuity check.
2. `uv run atech check --port <SCREEN>` → 13 modules ok (12 grids init + `pong.ok()` proves SoftAP).
3. Laptop joins `atech-pong` → `python tools/fake_controller.py`: pass READY_CHECK with two hold keys, score a point, watch printed cues — **server + protocol + state machine + rendering proven with zero controller hardware**.
4. Flash controller → immediately observable alone: TFT boot frame + "SEARCHING" banner + red-blink rings; `uv run atech monitor --port <CTRL>` streams knob detents/presses (bundled templates — resolves the (9,10) question empirically); `atech check` (speaker/TFT init).
5. End-to-end: both boot orders; both-hold 0.5 s → countdown ticks + serve jingle; full match to 3; goal/win jingles exactly once; TFT tracks.
6. Robustness: kill controller mid-rally (LINK_WAIT ≤1 s; return → READY_CHECK), button spam during play (hold-gate only, no effect), violent knob spins.
7. Power/sound: full match at brightness → `atech check` reset_reason ≠ BROWNOUT (screen ≈1.3 A worst-case all-white @20%; controller feeds 3 W amp + TFT + rings — volume ≤0.5, real 5V/2A+ supplies per board, never a laptop hub).

## Risks

**24 strip objects vs 4 RMT channels (S3)** — Adafruit acquires/releases RMT per show(), shows are sequential → should work; step-1 is the go/no-go; fallback: local `modules/neopixel/` override (same id wins) dropping the unused line-B RGBW strip → 12 objects, ~3.5 ms repaint · **TFT display() 6-10 ms** — event-driven redraws only · **audio task + WiFi on core 0** — speaker designed for it; short motifs; if crackle → shorten, never block · **feedback loss** — wrapping counters + latch = exactly-once; 2.5 s timeout = 50 missed packets · **ready misfires** — server-clocked 500 ms of 50 Hz levels = inherently debounced; progress echoed so players see server truth · **venue 2.4 GHz** — channel picked on site; 14 kbit/s; ESP-NOW seam last resort · **knob-2 port** — one-line fix · **watchdog** — nothing blocks, bounded datagram drains (≤8/loop), engine step cap (≤5×) · **brownout** — sparse frames, WALL_BRIGHTNESS knob, step 7 · **first build needs internet** — pre-cache · **two identical boards** — `atech check` lists modules; tape labels.

## Cut list if behind (reverse priority)

net dots → wall-bounce blip → TFT rally counter → ring FX beyond hold-sweep+score-dial → GAME_OVER wave (→ solid winner color) → attract AI (→ breathing paddle rows) → match-point sting → SCORE pips on wall (TFT has them). **Never cut:** clamped-delta knob mapping, english, speed ramp, both-hold READY_CHECK, calibration mode, exactly-once cue tracker, the sim.

## Polish list if ahead (cheap + loud first)

paddle-cell flash on contact (1 bright frame) · speed-tinted ball · goal wipe from conceded edge · match-point sting + blinking leader bar · gamma LUT in compositor · paddle-velocity english (slice shots) · serve fake-out delay · win fireworks sparkles · TFT attract animation · 2nd controller board (protocol already supports controllerId 1).
