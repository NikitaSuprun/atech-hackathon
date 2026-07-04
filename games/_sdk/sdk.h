#pragma once
// One include for game code: rng + bitmap font + tiny math helpers.
#include "rng.h"
#include "font3x5.h"

namespace sdk {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int signi(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

}  // namespace sdk
