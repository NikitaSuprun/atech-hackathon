#pragma once
#include <stdint.h>
#include "console/color.h"
#include "console/config.h"
#include "console/theme.h"

// The smooth-glow light engine — the on-hardware effect the firmware has none
// of today (it fully redraws every frame). A persistent Color field eases
// toward each incoming frame and, on the way to the LEDs, gains a bloom halo.
// The active LightProfile shapes the feel: `decay` = trail persistence, `bloom`
// = glow spread. Pure functions over Color buffers, so it is fully host-tested
// off-hardware; the renderer glue just owns one of these.
//
// Envelope per pixel per channel each tick:
//   delta = (target - field) * alpha / 255       // alpha = attack up / (255-decay) down
//   field += delta, snapping to target when |delta| rounds to 0
// Attack is fast (LIGHT_ATTACK) so game objects stay crisp; the release is
// eased by `decay`, which is what leaves the trailing glow:
//   decay = 0   -> release alpha 255 -> snaps to frame (no trail)
//   decay = 255 -> release alpha 0   -> holds forever (max trail)

namespace console {

// Attack ease rate toward a brighter target (per 255). Near-instant but eased.
constexpr uint8_t LIGHT_ATTACK = 230;

namespace detail {

inline uint8_t ease8(uint8_t cur, uint8_t target, uint8_t alpha) {
    if (alpha == 0) return cur;  // frozen (decay=255 -> hold the peak, infinite trail)
    int delta = (int(target) - int(cur)) * int(alpha) / 255;
    if (delta == 0) return target;  // within one step -> snap (kills integer stall)
    return uint8_t(int(cur) + delta);
}

inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : uint8_t(v)); }

}  // namespace detail

class LightEngine {
public:
    // The persistent eased/decayed light field (pre-bloom). Tests read this to
    // assert the decay math; the renderer never shows it directly.
    Color field[SCREEN_PX];

    void reset(Color c = BLACK) {
        for (int i = 0; i < SCREEN_PX; ++i) field[i] = c;
    }

    // Advance `field` one tick toward `target` (attack/decay), then write the
    // display buffer `out` = field + bloom halo. Bloom reads the post-envelope
    // field only (never fed back), so it is a stable per-tick spatial filter,
    // not a compounding smear. `out` may not alias `target`.
    void step(const Color target[SCREEN_PX], const LightProfile& lp,
              Color out[SCREEN_PX]) {
        uint8_t up = LIGHT_ATTACK;
        uint8_t down = uint8_t(255 - lp.decay);
        for (int i = 0; i < SCREEN_PX; ++i) {
            Color t = target[i], f = field[i];
            f.r = detail::ease8(f.r, t.r, t.r >= f.r ? up : down);
            f.g = detail::ease8(f.g, t.g, t.g >= f.g ? up : down);
            f.b = detail::ease8(f.b, t.b, t.b >= f.b ? up : down);
            field[i] = f;
        }
        bloom(lp.bloom, out);
    }

private:
    // Additive 4-neighbour glow spread. bloom=0 -> out==field; bloom=255 adds
    // the average of the orthogonal neighbours (clamped).
    void bloom(uint8_t amount, Color out[SCREEN_PX]) const {
        if (amount == 0) {
            for (int i = 0; i < SCREEN_PX; ++i) out[i] = field[i];
            return;
        }
        for (int y = 0; y < SCREEN_H; ++y)
            for (int x = 0; x < SCREEN_W; ++x) {
                int i = y * SCREEN_W + x;
                int sr = 0, sg = 0, sb = 0;
                acc(x, y - 1, sr, sg, sb);
                acc(x, y + 1, sr, sg, sb);
                acc(x - 1, y, sr, sg, sb);
                acc(x + 1, y, sr, sg, sb);
                Color f = field[i];
                out[i].r = detail::clamp8(f.r + sr * amount / (255 * 4));
                out[i].g = detail::clamp8(f.g + sg * amount / (255 * 4));
                out[i].b = detail::clamp8(f.b + sb * amount / (255 * 4));
            }
    }

    void acc(int x, int y, int& sr, int& sg, int& sb) const {
        if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
        Color c = field[y * SCREEN_W + x];
        sr += c.r;
        sg += c.g;
        sb += c.b;
    }
};

}  // namespace console
