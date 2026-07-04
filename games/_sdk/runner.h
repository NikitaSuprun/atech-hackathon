#pragma once
#include <stdint.h>
#include "console/audio.h"
#include "console/canvas.h"
#include "console/color.h"
#include "console/config.h"
#include "console/game.h"
#include "console/input.h"
#include "console/theme.h"

// Host-side glue to run ANY console::Game with zero hardware: a fixed Color
// framebuffer, a per-tick Input sampler, and a one-tick update+draw step. No I/O
// and no timing here — the terminal/dump/WASM host owns the loop.

namespace host {

// Silent Audio so games may call ctx.audio-> unconditionally on the desktop.
class NullAudio : public console::Audio {
public:
    void tone(uint16_t, uint16_t) override {}
    void note(uint8_t, uint16_t) override {}
    void melody(const char*) override {}
    void stop() override {}
    bool playing() const override { return false; }
    void setVolume(float) override {}
};

// Turns discrete key/encoder events into a per-tick console::Input: accumulated
// detents become knob.delta, cumulative pos tracks, button toggles yield edges.
class InputSampler {
public:
    void rotate(int knob, int detents) { if (knob == 0 || knob == 1) pending_[knob] += detents; }
    void setButton(int knob, bool down) { if (knob == 0 || knob == 1) down_[knob] = down; }
    void toggleButton(int knob) { if (knob == 0 || knob == 1) down_[knob] = !down_[knob]; }

    console::Input sample() {
        console::Input in{};
        for (int k = 0; k < 2; ++k) {
            int32_t d = pending_[k];
            pending_[k] = 0;
            pos_[k] += d;
            console::Knob& kn = in.knob[k];
            kn.delta = d;
            kn.pos = pos_[k];
            kn.down = down_[k];
            kn.justPressed = down_[k] && !prev_[k];
            kn.justReleased = !down_[k] && prev_[k];
            prev_[k] = down_[k];
        }
        return in;
    }

private:
    int32_t pending_[2] = {0, 0};
    int32_t pos_[2] = {0, 0};
    bool    down_[2] = {false, false};
    bool    prev_[2] = {false, false};
};

// Drives one console::Game against one Theme into a fixed Color[SCREEN_PX] buffer.
class GameRunner {
public:
    GameRunner(console::Game* game, const console::Theme* theme)
        : game_(game), theme_(theme), canvas_(buf_, console::SCREEN_W, console::SCREEN_H) {}

    void init(uint32_t seed) {
        ctx_.audio = &audio_;
        ctx_.theme = theme_;
        ctx_.rngSeed = seed;
        game_->init(ctx_);
    }
    void setTheme(const console::Theme* t) { theme_ = t; ctx_.theme = t; }

    // one fixed tick: advance the game, then repaint the framebuffer through the theme
    void tick(const console::Input& in, uint32_t dtMs) {
        game_->update(in, dtMs);
        game_->draw(canvas_, *theme_);
    }

    const console::Color* frame() const { return buf_; }
    console::Canvas&      canvas() { return canvas_; }
    console::Game*        game() const { return game_; }
    const console::Theme* theme() const { return theme_; }

private:
    console::Game*        game_;
    const console::Theme* theme_;
    NullAudio             audio_;
    console::Color        buf_[console::SCREEN_PX] = {};
    console::Canvas       canvas_;
    console::GameContext  ctx_ = {};
};

}  // namespace host
