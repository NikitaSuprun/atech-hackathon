#pragma once
#include <stdint.h>
#include "console/theme.h"
#include "tft_surface.h"

// The brain's TFT dashboard: the beautiful, animated 160x80 face of the console.
// It OWNS no OS state — every frame it reads BrainOS's getters (mode, menuSel,
// theme, settings, registry, overlay) and renders the matching screen:
//   Menu     -> a knob-tuned filmstrip launcher that color-washes per game
//   Settings -> volume / brightness / theme, with a live preview
//   Game     -> a now-playing card; the pause wheel when the overlay is open
//   (boot)   -> a short "ATECH ARCADE" intro on first power-up
// Drawing goes through the Tft interface so the identical code runs on the panel
// and in a host preview. Animation is time-based (its own eased scroll + wash).

namespace console_os {

class BrainOS;  // read-only, forward-declared

class TftDashboard {
public:
    explicit TftDashboard(Tft& tft) : tft_(tft) {}

    void begin() { init_ = false; }
    void render(const BrainOS& os, uint32_t nowMs);

private:
    void drawBoot(const console::Theme& t, uint8_t bright, uint32_t nowMs);
    void drawMenu(const BrainOS& os, const console::Theme& t, uint8_t bright);
    void drawCard(const BrainOS& os, const console::Theme& t, uint8_t bright, int idx,
                  float alpha, int dx);
    void drawSettings(const BrainOS& os, const console::Theme& t, uint8_t bright);
    void drawGame(const BrainOS& os, const console::Theme& t, uint8_t bright);

    Tft&     tft_;
    bool     init_    = false;
    uint32_t startMs_ = 0;
    uint32_t lastMs_  = 0;
    uint32_t nowMs_   = 0;
    float    dt_      = 16.f;
    float    scroll_  = 0.f;     // eased filmstrip position (toward menuSel)
    float    wash_[3] = {0, 0, 0};  // eased background wash (toward game accent)
};

}  // namespace console_os
