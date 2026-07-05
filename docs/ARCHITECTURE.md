# Architecture

Atech Arcade is a **ports-and-adapters** system. A pure-C++ core (contracts + games + OS +
the dumb renderer) has zero hardware knowledge; thin adapters bind it to the ESP32, the
wire, and the desktop host. The whole platform builds and runs on a laptop with no board.

## 1. The contracts (`include/console/`)

Everything the games and OS agree on lives in one header set. It is **pure C++14**, uses
only `<stdint.h>`/`<string.h>`, allocates nothing on the heap, and includes no Arduino — so
the identical files compile into ESP32 firmware, the desktop sim, and a WASM build.

| Header | Contract |
|--------|----------|
| `color.h` | `struct Color{r,g,b}`; `to565`/`from565`, `scale`, `lerp`. The one true color type. |
| `config.h` | Logical geometry + timing: `SCREEN_W=6`, `SCREEN_H=18`, `SCREEN_PX=108`, `TICK_HZ=50`, `TICK_MS=20`. |
| `canvas.h` | `Canvas` — the abstract 6×18 drawing surface (`pixel/hline/vline/rect/fill/blit`) over a caller-owned `Color[108]`. **No LED, serpentine, or panel knowledge.** |
| `input.h` | `Input{Knob knob[2]}`; each `Knob` has `delta`, `pos` (absolute detents), `down`, `justPressed`, `justReleased`. `knob[0]`=P1/left, `knob[1]`=P2/right. |
| `audio.h` | Abstract `Audio` sink: `tone/note/melody(rtttl)/stop/playing/setVolume`. |
| `theme.h` | `Theme` = a design-token bundle; `Role` enum, `LightProfile`, `MotionProfile`, `ThemeManager`. See [THEMES.md](THEMES.md). |
| `game.h` | The `Game` interface every title implements; `GameContext`, `GameMeta`, `GameEvent`. |
| `frame_proto.h` | Brain→screen wire protocol: `frameEncode/Decode`, `LightMsg`, COBS helpers. |
| `host_proto.h` | Host↔board control + telemetry (volume/theme/inject-input/select-game/telemetry). |
| `themes.{h,cpp}` | `THEMES[]` + `THEME_COUNT` — the 5 hand-authored themes, defined once, shared everywhere. |

The **game interface** is deliberately tiny:

```cpp
class Game {
  virtual const GameMeta& meta() const = 0;
  virtual void init(const GameContext& ctx) = 0;      // full reset; ctx = {audio, theme, rngSeed}
  virtual void update(const Input& in, uint32_t dtMs) = 0;
  virtual void draw(Canvas& c, const Theme& t) = 0;
  virtual void teardown() {}
  virtual void onEvent(GameEvent) {}                  // EV_PAUSE/RESUME/MENU/EXIT
};
```

A game reads **only** `Input`, draws **only** to `Canvas` using **theme tokens**
(`t.c(ROLE_*)`, `t.cat[i]`, `t.ramp[i]`), and gets services (`audio`, `theme`, `rngSeed`)
through `GameContext`. It never touches hardware, a transport, or a raw color.

## 2. The OS (`modules/console_os/`)

`BrainOS` is the on-device shell. It owns:

- **The fixed-rate loop.** `tick(in, dtMs)` advances exactly one `TICK_MS` step;
  `pump(nowMs, in)` feeds wall-clock and runs every due fixed step (catch-up capped at 4).
  Input deltas/edges apply on the first sub-step only, so a burst never double-counts a detent.
- **One framebuffer.** A single `Color[108]` that the menu, settings screen, active game,
  and pause overlay all draw into — via one `Canvas`.
- **App lifecycle.** Modes `Boot → Menu → Settings → Game`. The **menu** (`menu.h`) is one
  knob: rotate to scroll the registry + a trailing SETTINGS row, press to launch/open. A
  reserved **both-buttons chord** (`overlay.h`) floats a RESUME/EXIT pause menu *over* a
  running game **without tearing it down** — RESUME closes it and the game continues from
  exactly where it froze.
- **Active theme, master volume, brightness.** Persisted via the `SettingsStore` seam:
  `NvsSettingsStore` (ESP32 NVS/Preferences) on hardware, `FileSettingsStore` (a 6-byte
  file) / `MemorySettingsStore` on the host. `lightProfile()` returns the active theme's
  `LightProfile` with `wallBrightness` scaled by the brightness setting.
- **The `FrameSink` seam.** The OS never talks to a transport. Each composed tick it hands
  the finished `Color[108]` + `LightProfile` to a `FrameSink`. `light()` fires only when
  the profile actually changes (theme switch or brightness change).

The **game registry** (`game_registry.h`) is a data table of `{name, factory}` the menu
enumerates. Adding a game is a one-line `reg.add()` — never a menu edit.

## 3. The frame path: brain → wire → screen

```
BrainOS.tick()                                         ScreenRenderBoard.tick()
  compose Color[108]                                     link.poll(); every TICK_MS:
  emit() ──► FrameSink                                     drain up to N packets:
              │                                              frameType() → dispatch
   LinkFrameSink (modules/link)                                MSG_FRAME → frameDecodeInto()
     frameEncode(px,0,0,6,18,seq) ─► MSG_FRAME                   → persistent logical canvas
       header(13B) + RGB565 payload (full or dirty-rect)         MSG_SET_LIGHT_PROFILE → setLightProfile()
     lightEncode(lp) ─────────────► MSG_SET_LIGHT_PROFILE     ScreenRenderer.renderFrame(canvas)
              │                                                  LightEngine.step() (attack/decay + bloom)
   PongLink (the transport seam)                                 Compositor.composeTile() (serpentine + rot + dirty)
     LinkCobsSerial: COBS-frame + 0x00 delim over USB-CDC        → TileSink → 12 × NeoTile (WS2812)
     (UDP / ESP-NOW / LinkLoopback send the packet raw)
```

**Wire protocol (`frame_proto.h`).** Two message types share a leading
`{magic='CFRM', version=1, type, seq}` so a receiver validates then dispatches on `type`:

- `MSG_FRAME` — a `FrameHeader` (13 B, `static_assert`ed) + an RGB565 payload for a
  rectangle `(x,y,w,h)`. A full frame is `0,0,6,18` = 13 + 108×2 = **229 B**
  (`FRAME_MAX_PACKET`); a dirty-rect frame ships only the changed window.
- `MSG_SET_LIGHT_PROFILE` — a 13-B `LightMsg` carrying the 5-byte `LightProfile`, sent on
  theme/brightness change. This is what makes "warm" *feel* different from "neon" on the wall.

**Framing (`link_frame.h`, `link_cobs_serial.h`).** The primary transport is binary
**COBS** over USB-CDC serial: each packet is COBS-encoded (overhead 1 byte per 254) and
terminated with a single `0x00`. `CobsReader` is a heap-free streaming de-framer that
resyncs on the next delimiter after any overrun. The MTU is raised to 300 B
(`CONSOLE_LINK_MTU`) so a 229-B frame fits. Datagram transports (UDP, ESP-NOW, the host
`LinkLoopback`) are already framed and send the raw packet.

**The screen adapter (`modules/screen_render/`)** is the *dumb renderer* — it knows nothing
about games:

- `ScreenRenderBoard` drains frame packets, keeps a **persistent** logical canvas (so
  dirty-rect frames accumulate), applies light-profile messages, and ticks the renderer
  once per `TICK_MS`. A periodic **heartbeat** force-repaints every tile to heal WS2812
  glitches.
- `ScreenRenderer` eases the light engine toward the canvas and maps logical pixels to
  physical LEDs by **reusing the pong `Compositor`** (serpentine chip order + per-tile
  quarter-turn rotation from `TILE_MAP` + a dirty-tile cache) so only changed tiles are
  `show()`n. It drives LEDs through an abstract `TileSink` (`NeoTile` on hardware, a capture
  sink in tests) — so the whole file is Arduino-free and host-testable.
- `LightEngine` (`light_engine.h`) is the smooth-glow effect the old firmware lacked. A
  persistent `Color` field eases toward each frame — **fast attack** (crisp game objects),
  **eased release** governed by `LightProfile.decay` (the trailing glow) — then adds a
  `bloom` halo (a stable per-tick 4-neighbour spatial filter, never fed back). Pure
  functions over `Color` buffers, fully host-tested.

## 4. Determinism & the CI gate

Games are **replayable**: all randomness comes from a seeded `sdk::Rng` (xorshift32 seeded
from `ctx.rngSeed`), all motion integrates `dtMs`, and `init()` fully resets state. Fixed
seed + fixed input ⇒ **bit-identical** frames. The per-game selftest (`make -C sim
gameselftest GAME=<g>`) runs a game twice for 300 ticks under scripted input and asserts the
two final framebuffers `memcmp`-equal (and that it drew a non-background pixel). The
end-to-end test (`modules/console_e2e/test_e2e.cpp`) composes the **real** `BrainOS` and the
**real** `ScreenRenderBoard` over one in-memory `LinkLoopback` and asserts the screen's
decoded canvas equals `from565(to565(brain_frame))` for every frame — across the menu, a
game launch, and a theme switch.

## 5. Design decisions & rationale

- **Why a dumb screen.** All game logic, physics, scoring, and theming live on the brain;
  the screen only decodes pixels and glows. The two boards agree on nothing but a pixel
  frame + a light profile, so the display is reusable for *any* content (games today,
  ambient scenes, a future clock) and the brain can be re-flashed without touching it.
- **Why theme tokens, not hex.** Games draw with *meanings* (`ROLE_BALL`, `ROLE_HAZARD`,
  `ramp[]`, `cat[]`), not colors. One `Theme` swap restyles the entire console at runtime,
  and it kills the three drifting player-color definitions (LED rgb888 vs TFT rgb565 vs
  Python) that plagued the original Pong — there is now one `Color` type and one palette source.
- **Why deterministic.** Replayability is the test oracle: a headless twice-run diff catches
  any behavior regression byte-for-byte, and it lets an aesthetic-eval harness render every
  game to PNG/GIF off-hardware. It also makes the attract/idle loops reproducible.
- **Why RGB565 on the wire.** Halves frame bytes (2 B/px vs 3) with no visible loss at 20%
  brightness on a 108-px matrix; a 108-px frame is 229 B, comfortably inside one ESP-NOW
  datagram (250 B) for the wireless roadmap. `to565`/`from565` are exact inverses, so the
  E2E test can assert equality through the codec.
- **Why COBS.** A byte-stream link (USB-CDC) needs unambiguous packet boundaries; COBS gives
  a single reserved `0x00` delimiter with 1-byte-per-254 overhead and a de-framer that
  can't wedge on garbage. Datagram links skip it entirely — the framing is isolated to the
  serial adapter.
- **Why the transport is a seam (`PongLink`).** `begin/poll/sendRaw/recvRaw/isUp` is the
  entire link surface. Swapping COBS-serial ↔ ESP-NOW ↔ loopback is a one-object change,
  which is exactly how the original build swapped WiFi→USB in ~20 minutes mid-hackathon and
  how the host E2E test drives the real boards with no wire.
- **Why reuse the pong `Compositor`.** Serpentine wiring + per-tile rotation + the
  dirty-tile cache were already solved and hardware-calibrated for the LED matrix; the new
  renderer inherits that mapping instead of re-deriving it. `console::Color` and
  `pong::Color` share a packed 3-byte layout, so the eased buffer feeds it as-is
  (`static_assert`ed).
