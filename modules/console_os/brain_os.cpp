#include "brain_os.h"

#include <string.h>

namespace console_os {

using console::Input;
using console::SCREEN_PX;

void BrainOS::begin() {
    // Load persisted state, clamping the theme to the live registry size.
    Settings s;
    if (store_.load(s)) {
        settings_ = s;
        if (settings_.themeIndex >= themes_.count()) settings_.themeIndex = 0;
    }
    themes_.setActive(settings_.themeIndex);
    if (audio_) audio_->setVolume(settings_.volume / 255.0f);

    mode_ = Mode::Menu;
    menu_ = MenuState{};
    // Prime the light profile so the screen adapter has one before the first frame.
    lastLight_ = lightProfile();
    haveLight_ = true;
    if (sink_) sink_->light(lastLight_);
}

console::LightProfile BrainOS::lightProfile() const {
    console::LightProfile lp = themes_.active().light;
    lp.wallBrightness =
        uint8_t((uint16_t(lp.wallBrightness) * settings_.brightness) / 255u);
    return lp;
}

void BrainOS::tick(const Input& in, uint32_t dtMs) {
    updateActive(in, dtMs);
    drawActive();
    emit();
}

void BrainOS::pump(uint32_t nowMs, const Input& in) {
    if (!pumpInit_) {
        pumpInit_ = true;
        lastNow_  = nowMs;
    }
    accMs_ += nowMs - lastNow_;
    lastNow_ = nowMs;

    int steps = 0;
    Input quiet{};  // held levels only, no deltas/edges for catch-up steps
    for (int k = 0; k < 2; ++k) {
        quiet.knob[k].down = in.knob[k].down;
        quiet.knob[k].pos  = in.knob[k].pos;
    }
    while (accMs_ >= console::TICK_MS && steps < MAX_CATCHUP_STEPS) {
        tick(steps == 0 ? in : quiet, console::TICK_MS);
        accMs_ -= console::TICK_MS;
        ++steps;
    }
    if (accMs_ >= console::TICK_MS) accMs_ = 0;  // fell behind: drop the backlog
}

void BrainOS::updateActive(const Input& in, uint32_t dtMs) {
    switch (mode_) {
        case Mode::Menu: {
            int        prevSel = menu_.sel;
            MenuAction a       = menuUpdate(menu_, reg_.count(), in, dtMs);
            if (a == MenuAction::Launch) {
                sfx_.select();
                launch(menu_.sel);
            } else if (a == MenuAction::OpenSettings) {
                sfx_.openSettings();
                mode_ = Mode::Settings;
                set_  = SettingsState{};
            } else if (menu_.sel != prevSel) {
                sfx_.navTick();
            } else {
                sfx_.idle(menu_.sinceMov);
            }
            break;
        }
        case Mode::Settings: {
            SettingsCtx sc{themes_, audio_, settings_, &store_};
            SettingsAction a = settingsUpdate(set_, sc, in, dtMs);
            if (a == SettingsAction::Back) {
                settings_.themeIndex = themes_.index();
                store_.save(settings_);
                sfx_.back();
                mode_ = Mode::Menu;
            }
            break;
        }
        case Mode::Game: {
            if (!game_) { exitToMenu(); break; }
            if (ov_.open) {
                OverlayAction a = overlayUpdate(ov_, in, dtMs);
                if (a == OverlayAction::Resume) {
                    closeOverlay();
                } else if (a == OverlayAction::Restart) {
                    if (game_) game_->teardown();
                    launch(gameIdx_);  // same title, fresh init()
                } else if (a == OverlayAction::Next) {
                    int next = (gameIdx_ + 1) % reg_.count();
                    if (game_) game_->teardown();
                    launch(next);
                } else if (a == OverlayAction::Exit) {
                    exitToMenu();
                }
            } else if (overlayChord(in)) {
                openOverlay();  // swallow this tick's input from the game
            } else {
                game_->update(in, dtMs);
            }
            break;
        }
        case Mode::Boot:
        default:
            mode_ = Mode::Menu;
            break;
    }
}

void BrainOS::drawActive() {
    const console::Theme& t = themes_.active();
    switch (mode_) {
        case Mode::Menu:
            menuDraw(menu_, reg_, canvas_, t);
            break;
        case Mode::Settings: {
            SettingsCtx sc{themes_, audio_, settings_, &store_};
            settingsDraw(set_, sc, canvas_, t);
            break;
        }
        case Mode::Game:
            if (game_) {
                game_->draw(canvas_, t);          // frozen frame when paused
                if (ov_.open) overlayDraw(ov_, canvas_, t);
            } else {
                canvas_.clear(t.c(console::ROLE_BG));
            }
            break;
        default:
            canvas_.clear(t.c(console::ROLE_BG));
            break;
    }
}

void BrainOS::emit() {
    if (sink_) sink_->frame(buf_, seq_);
    ++seq_;
    console::LightProfile lp = lightProfile();
    if (!haveLight_ || memcmp(&lp, &lastLight_, sizeof(lp)) != 0) {
        lastLight_ = lp;
        haveLight_ = true;
        if (sink_) sink_->light(lp);
    }
}

void BrainOS::launch(int gameIndex) {
    if (gameIndex < 0 || gameIndex >= reg_.count()) return;
    game_    = reg_.at(gameIndex).make();
    gameIdx_ = gameIndex;
    ctx_.audio   = audio_;
    ctx_.theme   = &themes_.active();
    ctx_.rngSeed = seed_++;
    game_->init(ctx_);
    ov_.reset();
    mode_ = Mode::Game;
}

void BrainOS::openOverlay() {
    ov_.begin();
    if (game_) game_->onEvent(console::EV_PAUSE);
}

void BrainOS::closeOverlay() {
    ov_.reset();
    if (game_) game_->onEvent(console::EV_RESUME);
}

void BrainOS::exitToMenu() {
    if (game_) {
        game_->onEvent(console::EV_MENU);
        game_->teardown();
    }
    game_    = nullptr;
    gameIdx_ = -1;
    ov_.reset();
    sfx_.back();
    mode_ = Mode::Menu;
}

}  // namespace console_os
