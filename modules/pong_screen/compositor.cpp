#include "compositor.h"
#include <string.h>

static_assert(sizeof(pong::Color) == 3, "Frame must be packed 3-byte RGB");

void Compositor::begin() {
    for (int t = 0; t < NUM_TILES; ++t) {
        const TileCfg& cfg = TILE_MAP[t];
        for (int i = 0; i < 9; ++i) {
            int lr = i / 3;
            int lc = i % 3;
            // LED local (lr,lc) on a tile mounted rot quarter-turns CW shows this region cell
            int rr, rc;
            switch (cfg.rot & 3) {
                case 0:  rr = lr;     rc = lc;     break;
                case 1:  rr = lc;     rc = 2 - lr; break;
                case 2:  rr = 2 - lr; rc = 2 - lc; break;
                default: rr = 2 - lc; rc = lr;     break;
            }
            int x = cfg.tileCol * 3 + rc;
            int y = cfg.tileRow * 3 + rr;
            lut_[t][i] = (uint16_t)((y * pong::W + x) * 3);
        }
        valid_[t] = false;
    }
    memset(cache_, 0, sizeof(cache_));
}

bool Compositor::composeTile(int t, const pong::Frame& fb, uint8_t out[27], bool force) {
    if (t < 0 || t >= NUM_TILES) return false;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(fb.px);
    for (int i = 0; i < 9; ++i) {
        const uint8_t* p = base + lut_[t][i];
        out[i * 3 + 0] = p[0];
        out[i * 3 + 1] = p[1];
        out[i * 3 + 2] = p[2];
    }
    if (!force && valid_[t] && memcmp(out, cache_[t], 27) == 0) return false;
    memcpy(cache_[t], out, 27);
    valid_[t] = true;
    return true;
}
