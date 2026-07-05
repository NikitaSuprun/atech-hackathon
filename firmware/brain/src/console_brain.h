#pragma once
#include <stdint.h>

#include "brain_os.h"
#include "builtin_games.h"
#include "console/themes.h"
#include "link_cobs_serial.h"
#include "link_frame_sink.h"
#include "rotary_encoder.h"
#include "settings.h"
#include "speaker_audio.h"
#include "st7735_tft.h"
#include "tft_dashboard.h"
#include "tft_st7735.h"

// The brain firmware's composition root. Wires the two rotary knobs + speaker to
// BrainOS (all 10 games, 5 themes, NVS-persisted settings), renders the animated
// dashboard to the 160x80 TFT, and streams the composed frames over the COBS
// serial link to the screen board. main.cpp owns the pins + hardware objects;
// this owns the OS wiring. Mirrors pong_control in shape: construct with the
// hardware, begin() once, service() every loop.
class ConsoleBrain {
public:
    ConsoleBrain(RotaryEncoder& k0, RotaryEncoder& k1, Speaker& spk, ST7735_TFT& tft)
        : k0_(k0),
          k1_(k1),
          themes_(THEMES, THEME_COUNT),  // THEMES/THEME_COUNT are global (themes.h)
          audio_(spk),
          sink_(link_),
          os_(reg_, themes_, &audio_, store_, &sink_),
          tft_(tft),
          surface_(tft_),
          dash_(surface_) {}

    void begin(uint32_t seed) {
        tft_.begin();  // inits the panel + wipes the stale old-Pong text
        console_os::registerBuiltinGames(reg_);
        link_.begin();
        os_.setSeed(seed);
        os_.begin();
        dash_.begin();
    }

    // Call every loop (after knob.update()): derive a per-tick console::Input
    // (delta + button edges) from the encoders' absolute positions, advance the
    // OS at wall-clock, then repaint the TFT dashboard from the OS's live state.
    void service(uint32_t nowMs) {
        console::Input in{};
        fillKnob(in.knob[0], k0_.getPosition(), k0_.isPressed(), st0_);
        fillKnob(in.knob[1], k1_.getPosition(), k1_.isPressed(), st1_);
        os_.pump(nowMs, in);
        dash_.render(os_, nowMs);
    }

private:
    struct KnobState {
        int32_t pos = 0;
        bool    down = false;
        bool    init = false;
    };
    static void fillKnob(console::Knob& k, int32_t pos, bool down, KnobState& s) {
        if (!s.init) { s.pos = pos; s.down = down; s.init = true; }
        k.delta = pos - s.pos;
        k.pos = pos;
        k.down = down;
        k.justPressed = down && !s.down;
        k.justReleased = !down && s.down;
        s.pos = pos;
        s.down = down;
    }

    RotaryEncoder&               k0_;
    RotaryEncoder&               k1_;
    console::ThemeManager        themes_;
    SpeakerAudio                 audio_;
    console_os::NvsSettingsStore store_;
    console_os::GameRegistry     reg_;
    LinkCobsSerial               link_;
    console_os::LinkFrameSink    sink_;
    console_os::BrainOS          os_;
    KnobState                    st0_, st1_;
    ST7735_TFT&                  tft_;
    console_os::St7735Surface    surface_;
    console_os::TftDashboard     dash_;
};
