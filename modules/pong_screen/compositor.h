#pragma once
#include <stdint.h>
#include "pong_frame.h"
#include "pong_config.h"

// PURE C++ (sim-safe). Maps the 6x18 logical frame onto 12 physical 3x3 tiles
// via TILE_MAP (position + quarter-turn rotation), with a per-tile dirty cache
// so only changed tiles get show()n.
class Compositor {
public:
    // build the tile/chip -> framebuffer byte-offset LUT from TILE_MAP
    void begin();
    // fill out[27] with RGB triplets in chip order; true = differs from cache (push it)
    bool composeTile(int t, const pong::Frame& fb, uint8_t out[27], bool force);

private:
    uint16_t lut_[NUM_TILES][9];
    uint8_t cache_[NUM_TILES][27];
    bool valid_[NUM_TILES];
};
