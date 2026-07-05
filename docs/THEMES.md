# Themes

A **theme** is a coherent design-token bundle that restyles the *whole* console at
runtime — the menu, every game, the knob rings, the TFT, and the glow of the LED wall
itself. Games never see hex; they read tokens by **meaning** (`t.c(ROLE_BALL)`,
`t.cat[i]`, `t.ramp[i]`). All themes live in one file, `include/console/themes.cpp`,
defined once and shared by both boards, the sim, and the eval harness. The OS persists the
active index to NVS across reboot.

## The 5 themes

Defined in `THEMES[]` order (index = the value stored in settings):

| # | Name | Character | LightProfile (bright · dim · decay · bloom · gamma) | Ring · TFT |
|---|------|-----------|------------------------------------------------------|------------|
| 0 | **Warm Analog** | amber / ember / gold on coal; long warm decay, soft bloom, rich gamma | 52 · 82 · 210 · 150 · 120 | trail-dot · soft |
| 1 | **Neon Coin-op** | cyan / magenta / violet on void; snappy decay, tight bright bloom | 58 · 72 · 70 · 72 · 90 | pulse · bold |
| 2 | **Mono Glow** | near-white + a lime pop over graphite on ink; minimal bloom, crisp | 50 · 78 · 90 · 40 · 100 | solid-arc · mono |
| 3 | **DMG Green** | four Game-Boy phosphor shades; very low glow & decay, dim (amber = danger) | 38 · 70 · 40 · 24 · 110 | solid-arc · mono |
| 4 | **Aurora** | soft teal / pink / indigo pastels on deep; medium bloom, dreamy flow | 46 · 80 · 170 · 150 · 115 | trail-dot · soft |

The `LightProfile` is why the same game *feels* different per theme: Warm Analog's high
`decay` (210) leaves long phosphor-like trails and its soft `bloom` glows; Neon Coin-op's
low `decay` (70) snaps crisply with a tight bright halo; DMG Green barely glows at all.
That profile is the handful of bytes shipped to the screen on every theme switch (see
[ARCHITECTURE.md](ARCHITECTURE.md) §3).

## Token taxonomy (`theme.h`)

A `Theme` is:

```cpp
struct Theme {
  const char*   name;
  Color         role[ROLE_COUNT];   // 13 semantic roles
  Color         cat[CAT_N];         // 8 categorical hues
  Color         ramp[RAMP_N];       // 5 gradient stops
  LightProfile  light;              // wallBrightness, dimLevel, decay, bloom, gamma
  MotionProfile motion;             // transitionMs, blinkMs, ease
  RingStyleId   ring;               // RING_TRAIL_DOT | RING_SOLID_ARC | RING_PULSE
  TftStyleId    tft;                // TFT_BOLD | TFT_MONO | TFT_SOFT
  Color    c(Role r) const;         // role[r]
  uint16_t c565(Role r) const;      // role[r] as RGB565 for the TFT
};
```

**`role[]` — 13 semantic roles** (`Role` enum). Draw with the one that matches *meaning*:

`ROLE_BG`, `ROLE_DIM`, `ROLE_ACCENT`, `ROLE_ACCENT2`, `ROLE_INK` (TFT text), `ROLE_P1`,
`ROLE_P2`, `ROLE_BALL`, `ROLE_NET`, `ROLE_FOOD`, `ROLE_HAZARD`, `ROLE_GOOD`, `ROLE_NEUTRAL`.

*Examples in the wild:* snake paints food `ROLE_FOOD` and its head `ROLE_ACCENT`; every game
crashes/penalizes in `ROLE_HAZARD`; pong uses `ROLE_P1`/`ROLE_P2`/`ROLE_BALL`/`ROLE_NET`.

**`cat[8]` — categorical hues.** Eight distinct colors for "N of a kind" content (Tetris
pieces, 2048 tiles, matrix-rain columns, jukebox sparkles). Index with
`t.cat[rng.below(CAT_N)]`.

**`ramp[5]` — a 5-stop gradient.** For continuous intensity: fire heat, plasma, VU-meter
height, trails. Sample it continuously by lerping between adjacent stops (ambient's fire and
jukebox's VU bars both do this).

**`LightProfile{wallBrightness, dimLevel, decay, bloom, gamma}`** — shapes the screen's
light engine. `wallBrightness` is the global LED ceiling (kept low — the driver hard-clamps
at 51/255 ≈ 20%; see [HARDWARE.md](HARDWARE.md)); `decay` = trail persistence per tick
(0 = none, 255 = infinite hold); `bloom` = glow spread; `gamma` = brightness curve
(fixed-point /100).

**`MotionProfile{transitionMs, blinkMs, ease}`** — timings/easing for menu transitions and
attention blinks (`ease`: 0 linear, 1 easeOutCubic, 2 spring). The OS menu/overlay breathe
on `blinkMs`.

**`ring` / `tft`** — style enums the knob-ring and TFT adapters switch on to restyle those
surfaces to match.

Access is always by token: `t.c(ROLE_X)`, `t.cat[i]`, `t.ramp[i]`, `t.light`, `t.motion`.
`ThemeManager` wraps `THEMES[]` with `active()/next()/prev()/setActive()`, and the OS scales
`light.wallBrightness` by the user's brightness setting before it rides to the wall.

## How to add a theme

Themes are pure data — no code.

**1. Append an entry to `THEMES[]` in `include/console/themes.cpp`.** Fill every field
positionally (the file header documents the layout). Keep the palette inside the two-tier
reality of the 20%-clamped wall: **accents saturated and bright, dims in the ~70–90 band** —
anything too dark quantizes to mud on the LEDs.

```cpp
// 6. Sunset — coral/gold on plum; medium trail, warm bloom
{ "Sunset",
  { /* role[13]: bg dim accent accent2 ink | p1 p2 ball net food hazard good neutral */
    {26,12,28}, {80,50,70}, {255,138,91}, {255,206,84}, {255,238,224},
    {255,138,91}, {124,160,255}, {255,246,232}, {96,64,90}, {255,206,84},
    {255,73,92}, {130,224,150}, {150,120,140} },
  { /* cat[8] */ {255,138,91},{255,206,84},{124,160,255},{130,224,150},
                 {224,120,200},{120,220,220},{255,170,120},{255,100,120} },
  { /* ramp[5] */ {40,12,40},{150,40,70},{255,120,80},{255,190,110},{255,240,220} },
  { 48, 80, 150, 130, 118 },   // LightProfile
  { 300, 600, 1 },             // MotionProfile
  RING_TRAIL_DOT, TFT_SOFT },
```

`THEME_COUNT` is computed from `sizeof(THEMES)`, so it updates automatically — nothing else
to touch. The new theme immediately appears in the sim's `t`-key cycle, the OS settings
theme-picker, and the eval harness.

**2. If (and only if) you need a new semantic color, APPEND to the `Role` enum in
`theme.h`** — never insert in the middle. `THEMES[]` initializes `role[]` positionally, so
inserting a role would silently shift every existing theme's colors. Append, then add the
new color as the last `role[]` entry in every theme.

**Verify:**

```bash
make -C modules/console_os test      # theme switch re-renders the menu in the new palette
make -C modules/console_e2e test      # SET_LIGHT_PROFILE still crosses the wire correctly
make -C sim gamerun GAME=demo        # press t to cycle to your theme and eyeball it
```
