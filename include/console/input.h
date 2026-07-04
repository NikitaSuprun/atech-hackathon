#pragma once
#include <stdint.h>

// The console's control shape: exactly two rotary knobs, each with a push-button.
// The input adapter (rotary encoder driver on hardware, keyboard in sim/WASM)
// fills this each tick; games read it and nothing else.

namespace console {

struct Knob {
    int32_t delta;         // signed detents since last tick
    int32_t pos;           // absolute cumulative detents
    bool    down;          // button held now
    bool    justPressed;   // rising edge this tick
    bool    justReleased;  // falling edge this tick
};

struct Input {
    Knob knob[2];          // knob[0] = P1 / left, knob[1] = P2 / right
};

}  // namespace console
