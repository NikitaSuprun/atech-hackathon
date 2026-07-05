#include "compositor.h"
#include <string.h>

static_assert(sizeof(pong::Color) == 3, "Frame must be packed 3-byte RGB");

void Compositor::begin() {
    for (int t = 0; t < NUM_TILES; ++t) {
        const TileCfg& cfg = TILE_MAP[t];
        for (int i = 0; i < LEDS_PER_TILE; ++i) {
            int lr = i / TILE_DIM;
            // hardware-verified: chip chain is serpentine (middle row reversed)
            int lc = (lr == 1) ? 2 - (i % TILE_DIM) : i % TILE_DIM;
            // LED local (lr,lc) on a tile mounted rot quarter-turns CW shows this region cell
            int rr, rc;
            switch (cfg.rot & 3) {
                case 0:  rr = lr;     rc = lc;     break;
                case 1:  rr = lc;     rc = 2 - lr; break;
                case 2:  rr = 2 - lr; rc = 2 - lc; break;
                default: rr = 2 - lc; rc = lr;     break;
            }
            int x = cfg.tileCol * TILE_DIM + rc;
            int y = cfg.tileRow * TILE_DIM + rr;
            if (FLIP_Y) y = (pong::H - 1) - y;  // line-B panels chain bottom-up
            lut_[t][i] = (uint16_t)((y * pong::W + x) * 3);
        }
        valid_[t] = false;
    }
    memset(cache_, 0, sizeof(cache_));
}

bool Compositor::composeTile(int t, const pong::Frame& fb, uint8_t out[TILE_BYTES], bool force) {
    if (t < 0 || t >= NUM_TILES) return false;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(fb.px);
    for (int i = 0; i < LEDS_PER_TILE; ++i) {
        const uint8_t* p = base + lut_[t][i];
        out[i * 3 + 0] = p[0];
        out[i * 3 + 1] = p[1];
        out[i * 3 + 2] = p[2];
    }
    if (!force && valid_[t] && memcmp(out, cache_[t], TILE_BYTES) == 0) return false;
    memcpy(cache_[t], out, TILE_BYTES);
    valid_[t] = true;
    return true;
}
