// Host render harness for the console's animated TFT menu.
//
// Constructs the REAL Brain OS (same doubles as modules/console_os/test_os.cpp)
// and the REAL TftDashboard, but points the dashboard at a HostTft — an Adafruit
// GFXcanvas16 wrapper that renders text with the SAME Adafruit fonts as the
// device. So every frame dumped here is pixel-parity with the panel.
//
// It drives knob0 detents to scroll the filmstrip launcher (forward across every
// card, then back) for a seamless loop, and streams 160x80 RGB frames to a file:
//
//   menu_dump <out.bin> [themeIndex] [stepTicks]
//
// File format (little-endian): "MD1\n", u32 w, u32 h, u32 frameCount, then
// frameCount * w*h*3 bytes of RGB888 (RGB565 expanded by bit-replication, i.e.
// what the 16-bit panel actually shows).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "brain_os.h"
#include "builtin_games.h"
#include "console/config.h"
#include "console/themes.h"
#include "host_tft.h"
#include "runner.h"  // host::InputSampler
#include "tft_dashboard.h"

using namespace console_os;
using console::TICK_MS;

namespace {

// RGB565 (native-endian, as stored by GFXcanvas16) -> RGB888, matching how the
// 16-bit TFT presents each pixel (5/6/5 expanded with bit replication).
inline void expand565(uint16_t v, uint8_t* out) {
    uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    out[0] = uint8_t((r << 3) | (r >> 2));
    out[1] = uint8_t((g << 2) | (g >> 4));
    out[2] = uint8_t((b << 3) | (b >> 2));
}

}  // namespace

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "menu_frames.bin";
    int         themeSel = argc > 2 ? atoi(argv[2]) : 0;
    int         stepTicks = argc > 3 ? atoi(argv[3]) : 6;  // ticks between detents
    if (stepTicks < 1) stepTicks = 1;

    // ---- real OS, same construction as test_os.cpp (no audio, in-memory store) ----
    GameRegistry reg;
    registerBuiltinGames(reg);
    console::ThemeManager themes(THEMES, THEME_COUNT);
    MemorySettingsStore   store;

    // Pre-seed persisted settings so begin() lands on the requested palette and a
    // legible full brightness for the hero shot.
    if (themeSel < 0) themeSel = 0;
    if (themeSel >= THEME_COUNT) themeSel = THEME_COUNT - 1;
    {
        Settings s;
        s.themeIndex = uint8_t(themeSel);
        s.brightness = 255;
        store.save(s);
    }

    BrainOS os(reg, themes, /*audio*/ nullptr, store);
    os.setSeed(0x1234u);
    os.begin();

    HostTft       ht;
    TftDashboard  dash(ht);
    dash.begin();
    host::InputSampler in;

    const int W = ht.width(), H = ht.height();
    const int FRAME_BYTES = W * H * 3;
    const int items = reg.count() + 1;  // games + trailing SETTINGS card

    FILE* f = fopen(outPath, "wb");
    if (!f) {
        fprintf(stderr, "menu_dump: cannot open %s\n", outPath);
        return 1;
    }
    // header (frameCount patched at the end)
    uint32_t hdr[3] = {(uint32_t)W, (uint32_t)H, 0};
    fwrite("MD1\n", 1, 4, f);
    long countPos = ftell(f);
    fwrite(hdr, sizeof(uint32_t), 3, f);

    uint32_t nowMs = 0;
    uint32_t frames = 0;
    uint8_t  rgb[160 * 80 * 3];

    auto stepOnce = [&](bool capture) {
        console::Input sampled = in.sample();
        os.pump(nowMs, sampled);
        dash.render(os, nowMs);
        if (capture) {
            const uint16_t* buf = ht.buffer();
            for (int i = 0; i < W * H; ++i) expand565(buf[i], &rgb[i * 3]);
            fwrite(rgb, 1, FRAME_BYTES, f);
            ++frames;
        }
        nowMs += TICK_MS;
    };

    // ---- warm-up: clear the 1600ms branded boot intro, settle on card 0 ----
    const int WARM = 95;  // 95 * 20ms = 1900ms > boot, scroll/wash settled
    for (int t = 0; t < WARM; ++t) stepOnce(/*capture*/ false);

    // ---- capture: present card 0, glide forward across every card, then let the
    // wrap detent (last -> 0) fast-flyback back to the start. All +1 detents, so
    // the sequence returns to card 0 -> the GIF loops seamlessly. ----
    const int DWELL = 6;   // brief "present" pause on a card (glow keeps breathing)
    const int FLY   = 16;  // frames for the flyback ease to settle at card 0
    for (int t = 0; t < DWELL; ++t) stepOnce(true);
    for (int s = 0; s < items - 1; ++s) {   // 0 -> last (SETTINGS)
        in.rotate(0, +1);
        for (int t = 0; t < stepTicks; ++t) stepOnce(true);
    }
    for (int t = 0; t < DWELL; ++t) stepOnce(true);
    in.rotate(0, +1);                       // wrap: SETTINGS -> card 0 (fast flyback)
    for (int t = 0; t < FLY; ++t) stepOnce(true);
    for (int t = 0; t < DWELL; ++t) stepOnce(true);

    // patch frame count
    fseek(f, countPos + 2 * (long)sizeof(uint32_t), SEEK_SET);
    fwrite(&frames, sizeof(uint32_t), 1, f);
    fclose(f);

    fprintf(stderr, "menu_dump: wrote %u frames (%dx%d) to %s  [theme %d '%s', %d cards]\n",
            frames, W, H, outPath, themeSel, THEMES[themeSel].name, items);
    return 0;
}
