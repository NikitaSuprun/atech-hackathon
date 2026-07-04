#pragma once
#include "console/theme.h"

// LOCAL minimal themes for the desktop host ONLY. The real generated THEMES[]
// (include/console/themes.h) is authored elsewhere; do not depend on it here.
// Field order mirrors console::Theme exactly (name, role[], cat[], ramp[], ...).

namespace host {

inline const console::Theme* stubThemes(int& count) {
    using namespace console;
    static const Theme t[] = {
        {"neon",
         {{6, 8, 12}, {40, 44, 52}, {0, 200, 255}, {255, 120, 0}, {230, 235, 245},
          {0, 200, 255}, {255, 120, 0}, {255, 255, 255}, {70, 70, 70}, {80, 220, 120},
          {255, 60, 60}, {80, 220, 120}, {150, 150, 160}},
         {{0, 200, 255}, {255, 120, 0}, {80, 220, 120}, {255, 60, 60}, {200, 120, 255},
          {255, 220, 60}, {60, 255, 200}, {255, 120, 200}},
         {{0, 0, 64}, {128, 0, 128}, {255, 64, 0}, {255, 200, 0}, {255, 255, 255}},
         {40, 80, 40, 60, 100},
         {180, 500, 1},
         RING_TRAIL_DOT, TFT_BOLD},
        {"warm",
         {{14, 8, 4}, {60, 40, 28}, {255, 140, 40}, {120, 200, 255}, {250, 240, 225},
          {255, 140, 40}, {120, 200, 255}, {255, 240, 210}, {90, 70, 50}, {120, 230, 120},
          {255, 70, 50}, {120, 230, 120}, {170, 150, 130}},
         {{255, 140, 40}, {120, 200, 255}, {120, 230, 120}, {255, 70, 50}, {220, 140, 255},
          {255, 210, 90}, {90, 235, 200}, {255, 150, 190}},
         {{32, 0, 0}, {128, 32, 0}, {255, 120, 0}, {255, 210, 80}, {255, 255, 240}},
         {40, 70, 60, 80, 110},
         {220, 600, 2},
         RING_PULSE, TFT_SOFT},
    };
    count = (int)(sizeof(t) / sizeof(t[0]));
    return t;
}

}  // namespace host
