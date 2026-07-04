#pragma once
#include <stdint.h>

// Deterministic xorshift32 — seed from GameContext.rngSeed for replayable games.

namespace sdk {

struct Rng {
    uint32_t s;

    explicit Rng(uint32_t seed = 0x1234567u) : s(seed ? seed : 0xA341316Cu) {}

    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    // [0, n)
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
    // [lo, hi] inclusive
    int range(int lo, int hi) { return hi <= lo ? lo : lo + (int)below((uint32_t)(hi - lo + 1)); }
    // [0, 1)
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
    bool chance(float p) { return unit() < p; }
};

}  // namespace sdk
