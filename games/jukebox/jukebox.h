#pragma once
#include <math.h>
#include <stdint.h>
#include "console/config.h"
#include "console/game.h"
#include "sdk.h"

// Jukebox: the audio+lights showpiece. Two apps on the 6x18 wall.
//
//  TRACK mode  - knob[0] rotation scrolls a small library of famous-song RTTTL
//                melodies (title scrolls horizontally in the 3x5 font); knob[0]
//                press plays the selection via audio->melody(rtttl), a second
//                press stops. While playing, a spectrum visualizer runs: six VU
//                bars with peak-hold dots, a beat pulse and falling sparkles,
//                all timed from the track's RTTTL tempo and coloured from
//                theme.ramp[] / theme.cat[].
//  PLAY mode   - knob[1] press toggles here. knob[0] rotation selects a note
//                across three octaves (name shown), knob[0] press sounds it via
//                audio->note(midi, ms) and lights the matrix in the note colour.
//
// The sim runs a silent NullAudio, so playback can't be heard and audio->playing()
// is meaningless here: the game tracks its own play state and drives the beat from
// a tempo timer. After a short idle it auto-enters an ATTRACT demo so an unattended
// wall (and the headless frame dump) keeps the visualizer alive; any input leaves it.
//
// Deterministic: sparkle spawns come from the seeded sdk::Rng and all motion from
// accumulated dtMs, so a fixed seed + input replays identically.

namespace jukebox {

using console::Canvas;
using console::Color;
using console::Game;
using console::GameContext;
using console::GameMeta;
using console::Input;
using console::Knob;
using console::Theme;

// One library entry: a font-safe display title and its RTTTL melody.
struct Track {
    const char* title;
    const char* rtttl;
};

// Real public-domain RTTTL ringtones; only b= (bpm) is parsed for the beat clock.
static const Track TRACKS[] = {
    {"SUPER MARIO",
     "smb:d=4,o=5,b=100:16e6,16e6,32p,8e6,16c6,8e6,8g6,8p,8g,8p,8c6,16p,8g,16p,8e,"
     "16p,8a,8b,16a#,8a,16g.,16e6,16g6,8a6,16f6,8g6,8e6,16c6,16d6,8b"},
    {"ZELDA",
     "Zelda:d=4,o=5,b=125:2a#,32f.6,32f6,32f.6,8g#.6,16a#.6,16a#.6,16g#6,16a#.6,8f6,"
     "2f6,8d#6,8d#6,8d6,2c6,4p,2a#,8g6,8g6,8f.6,16d#6,16d#6,16f6,8f.6,8d#6,8d#6,8d6,2c6"},
    {"TETRIS",
     "Tetris:d=4,o=5,b=160:e6,8b,8c6,8d6,16e6,16d6,8c6,8b,a,8a,8c6,e6,8d6,8c6,b,8b,"
     "8c6,d6,e6,c6,a,2a,8p,d6,8f6,a6,8g6,8f6,e6,8e6,8c6,e6,8d6,8c6,b,8b,8c6,d6,e6,c6,a,2a"},
    {"NOKIA",
     "Nokia:d=4,o=5,b=225:8e6,8d6,f#,g#,8c#6,8b,d,e,8b,8a,c#,e,2a"},
    {"STAR WARS",
     "StarWars:d=4,o=5,b=112:8a,8a,8a,8f.,16c6,8a,8f.,16c6,2a,8e6,8e6,8e6,8f.6,16c6,"
     "8g#,8f.,16c6,2a"},
    {"FUR ELISE",
     "FurElise:d=8,o=5,b=125:e6,d#6,e6,d#6,e6,b,d6,c6,4a,4p,c,e,a,4b,4p,e,g#,b,4c6,"
     "4p,e,e6,d#6,e6,d#6,e6,b,d6,c6,4a,4p,c,e,a,4b,4p,e,c6,b,2a"},
    {"PACMAN",
     "PacMan:d=16,o=5,b=112:b,b6,f#6,d#6,b6,f#6,32d#6,8d#6,c6,c7,g6,e6,c7,g6,32e6,"
     "8e6,b,b6,f#6,d#6,b6,f#6,32d#6,8d#6,d#6,e6,f6,f6,f#6,g6,g6,g#6,a6,8b6"},
    {"AXEL F",
     "AxelF:d=4,o=5,b=125:g,8a#.,16g,16p,16g,8c6,8g,8f,g,8d.6,16g,16p,16g,8d#6,8d6,"
     "8a#,8g,8d6,8g6,16g,16f,16p,16f,8d,8a#,2g"},
};
static constexpr int TRACK_N = int(sizeof(TRACKS) / sizeof(TRACKS[0]));

// Read the bpm from an RTTTL control header ("...b=NNN:..."); note names never
// precede '=', so the match is unambiguous. Falls back to 120 bpm.
inline int parseBpm(const char* r) {
    for (const char* p = r; p[0] && p[1]; ++p) {
        if ((p[0] == 'b' || p[0] == 'B') && p[1] == '=') {
            int v = 0;
            for (const char* q = p + 2; *q >= '0' && *q <= '9'; ++q) v = v * 10 + (*q - '0');
            if (v > 0) return v;
        }
    }
    return 120;
}

// ---- float / theme-colour helpers (tokens only, never hex) ----

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline uint8_t f2u8(float f) { return uint8_t(clampf(f, 0.f, 1.f) * 255.f + 0.5f); }
inline Color dimc(Color c, float f) { return console::scale(c, f2u8(f)); }
inline Color mixc(Color a, Color b, float f) { return console::lerp(a, b, f2u8(f)); }

// Sample the 5-stop theme ramp continuously, f in 0..1.
inline Color rampAt(const Theme& t, float f) {
    float s = clampf(f, 0.f, 1.f) * (console::RAMP_N - 1);
    int i = int(s);
    if (i >= console::RAMP_N - 1) return t.ramp[console::RAMP_N - 1];
    return console::lerp(t.ramp[i], t.ramp[i + 1], f2u8(s - i));
}

// Custom 3x5 sharp glyph (the SDK font has no '#'), MSB = leftmost column.
inline void drawSharp(Canvas& c, int x, int y, Color fg) {
    static const uint8_t g[5] = {0b101, 0b111, 0b101, 0b111, 0b101};
    for (int r = 0; r < 5; ++r)
        for (int col = 0; col < 3; ++col)
            if (g[r] & (1 << (2 - col))) c.pixel(x + col, y + r, fg);
}

class JukeboxGame : public console::Game {
public:
    const GameMeta& meta() const override { return meta_; }

    void init(const GameContext& ctx) override {
        audio_ = ctx.audio;
        rng_ = sdk::Rng(ctx.rngSeed ? ctx.rngSeed : 1u);
        mode_ = MODE_TRACK;
        trackIdx_ = 0;
        playing_ = false;
        attract_ = false;
        idleMs_ = 0;
        timeMs_ = 0.f;
        beatPhase_ = 0.f;
        beatEnv_ = 0.f;
        intensity_ = IDLE_INTENSITY;
        scroll_ = 0.f;
        noteIdx_ = 60 - MIDI_MIN;
        noteEnv_ = 0.f;
        for (int i = 0; i < BAR_N; ++i) { level_[i] = 0.f; barH_[i] = 0; peak_[i] = 0.f; }
        for (int i = 0; i < MAXP; ++i) part_[i].on = false;
        setBeat(TRACKS[trackIdx_]);
    }

    void update(const Input& in, uint32_t dtMs) override {
        timeMs_ += float(dtMs);
        const Knob& k0 = in.knob[0];
        const Knob& k1 = in.knob[1];

        bool anyInput = k0.delta || k1.delta || k0.justPressed || k1.justPressed ||
                        k0.justReleased || k1.justReleased;
        if (anyInput) { idleMs_ = 0; attract_ = false; } else { idleMs_ += dtMs; }

        // knob[1] press flips between the jukebox and the instrument
        if (k1.justPressed) {
            mode_ = (mode_ == MODE_TRACK) ? MODE_PLAY : MODE_TRACK;
            playing_ = false;
            attract_ = false;
            if (audio_) audio_->stop();
        }

        if (mode_ == MODE_TRACK) updateTrack(k0, dtMs);
        else updatePlay(k0, dtMs);
    }

    void draw(Canvas& c, const Theme& t) override {
        if (mode_ == MODE_TRACK) drawTrack(c, t);
        else drawPlay(c, t);
    }

private:
    enum Mode { MODE_TRACK, MODE_PLAY };

    // ---- geometry / tuning ----
    static constexpr int VIZ_Y0 = 6;                 // first visualizer row
    static constexpr int VIZ_H = 18 - VIZ_Y0;        // rows 6..17
    static constexpr int BAR_N = 6;                  // one bar per column
    static constexpr int MAXP = 10;                  // falling sparkles
    static constexpr uint32_t ATTRACT_MS = 800;      // idle before the demo runs
    static constexpr float TITLE_PXPS = 11.f;        // marquee speed, px/s
    static constexpr int TITLE_GAP = 6;              // marquee wrap gap
    static constexpr float IDLE_INTENSITY = 0.22f;   // resting bar height factor
    static constexpr float BEAT_FALL_MS = 210.f;     // beat-pulse decay
    static constexpr int MIDI_MIN = 48;              // C3
    static constexpr int MIDI_MAX = 84;              // C6
    static constexpr int NOTE_N = MIDI_MAX - MIDI_MIN + 1;
    static constexpr uint16_t NOTE_MS = 320;

    struct Particle {
        float x, y, vy;
        uint8_t ci;
        bool on;
    };

    void setBeat(const Track& tr) {
        int bpm = parseBpm(tr.rtttl);
        beatMs_ = 60000.f / float(bpm > 0 ? bpm : 120);
    }

    void updateTrack(const Knob& k0, uint32_t dtMs) {
        // rotate to browse (locked while a song is actually playing)
        if (!playing_ && k0.delta) {
            trackIdx_ = ((trackIdx_ + int(k0.delta)) % TRACK_N + TRACK_N) % TRACK_N;
            scroll_ = 0.f;
            setBeat(TRACKS[trackIdx_]);
        }
        // press toggles playback of the selection
        if (k0.justPressed) {
            if (playing_) {
                playing_ = false;
                if (audio_) audio_->stop();
            } else {
                playing_ = true;
                attract_ = false;
                setBeat(TRACKS[trackIdx_]);
                beatPhase_ = 0.f;
                beatEnv_ = 1.f;
                if (audio_) audio_->melody(TRACKS[trackIdx_].rtttl);
            }
        }
        // unattended: fall into a silent visual demo so the wall stays alive
        if (!playing_ && !attract_ && idleMs_ > ATTRACT_MS) {
            attract_ = true;
            setBeat(TRACKS[trackIdx_]);
            beatPhase_ = 0.f;
            beatEnv_ = 1.f;
        }
        updateViz(dtMs);
        scroll_ += TITLE_PXPS * float(dtMs) / 1000.f;
    }

    void updateViz(uint32_t dtMs) {
        float dt = float(dtMs);
        bool viz = playing_ || attract_;

        intensity_ += ((viz ? 1.f : IDLE_INTENSITY) - intensity_) * 0.08f;

        // tempo clock -> beat onsets punch the envelope and shed a sparkle
        beatPhase_ += dt / (beatMs_ > 1.f ? beatMs_ : 500.f);
        bool onBeat = false;
        while (beatPhase_ >= 1.f) { beatPhase_ -= 1.f; onBeat = true; }
        if (onBeat && viz) { beatEnv_ = 1.f; spawnParticles(); }
        beatEnv_ -= dt / BEAT_FALL_MS;
        if (beatEnv_ < 0.f) beatEnv_ = 0.f;

        // per-column band: slow wide bass on the left, fast tight treble on the right
        for (int i = 0; i < BAR_N; ++i) {
            float osc = 0.5f + 0.5f * sinf(timeMs_ * (0.004f + 0.0016f * i) + i * 1.7f);
            float ambient = 0.15f + (0.42f - 0.03f * i) * osc;
            float beatW = (i < 2) ? 1.0f : (i < 4 ? 0.72f : 0.5f);
            level_[i] = clampf(ambient + beatEnv_ * beatW * 0.7f, 0.f, 1.f) * intensity_;
            int h = int(level_[i] * VIZ_H + 0.5f);
            barH_[i] = h > VIZ_H ? VIZ_H : h;
            if (float(barH_[i]) > peak_[i]) peak_[i] = float(barH_[i]);
            else { peak_[i] -= 13.f * dt / 1000.f; if (peak_[i] < 0.f) peak_[i] = 0.f; }
        }
        for (int i = 0; i < MAXP; ++i)
            if (part_[i].on) {
                part_[i].y += part_[i].vy;
                if (part_[i].y > float(console::SCREEN_H)) part_[i].on = false;
            }
    }

    void spawnParticles() {
        int n = 1 + int(rng_.below(2));
        for (int s = 0; s < n; ++s)
            for (int i = 0; i < MAXP; ++i)
                if (!part_[i].on) {
                    part_[i].on = true;
                    part_[i].x = float(rng_.below(BAR_N));
                    part_[i].y = float(VIZ_Y0);
                    part_[i].vy = 0.4f + rng_.unit() * 0.7f;
                    part_[i].ci = uint8_t(rng_.below(console::CAT_N));
                    break;
                }
    }

    void updatePlay(const Knob& k0, uint32_t dtMs) {
        if (k0.delta) noteIdx_ = sdk::clampi(noteIdx_ + int(k0.delta), 0, NOTE_N - 1);
        if (k0.justPressed) {
            noteEnv_ = 1.f;
            if (audio_) audio_->note(uint8_t(MIDI_MIN + noteIdx_), NOTE_MS);
        }
        noteEnv_ -= float(dtMs) / 450.f;
        if (noteEnv_ < 0.f) noteEnv_ = 0.f;
    }

    void drawTrack(Canvas& c, const Theme& t) {
        c.clear(t.c(console::ROLE_BG));

        // VU bars rising from the floor, vertical ramp gradient, beat-brightened
        for (int i = 0; i < BAR_N; ++i) {
            for (int j = 0; j < barH_[i]; ++j) {
                int y = (console::SCREEN_H - 1) - j;
                Color col = rampAt(t, VIZ_H > 1 ? float(j) / (VIZ_H - 1) : 0.f);
                c.pixel(i, y, dimc(col, 0.72f + 0.28f * beatEnv_));
            }
            int pk = int(peak_[i] + 0.5f);
            if (pk >= 1) c.pixel(i, (console::SCREEN_H - 1) - (pk - 1), t.c(console::ROLE_ACCENT2));
        }

        // sparkles over the bars, fading as they fall
        for (int i = 0; i < MAXP; ++i)
            if (part_[i].on) {
                float life = 1.f - (part_[i].y - VIZ_Y0) / float(console::SCREEN_H - VIZ_Y0);
                c.pixel(int(part_[i].x), int(part_[i].y),
                        dimc(t.cat[part_[i].ci], 0.5f + 0.5f * clampf(life, 0.f, 1.f)));
            }

        // row 5: beat-pulsed bar carrying the scroll-position marker
        Color base = mixc(dimc(t.c(console::ROLE_DIM), 0.55f), t.c(console::ROLE_ACCENT),
                          clampf(beatEnv_ * intensity_, 0.f, 1.f) * 0.85f);
        for (int x = 0; x < console::SCREEN_W; ++x) c.pixel(x, 5, base);
        c.pixel(trackIdx_ * console::SCREEN_W / TRACK_N, 5, t.c(console::ROLE_ACCENT2));

        drawTitle(c, t);
    }

    // Marquee the title across rows 0..4; centre it if it already fits.
    void drawTitle(Canvas& c, const Theme& t) {
        const char* s = TRACKS[trackIdx_].title;
        int w = sdk::textWidth(s);
        Color col = mixc(t.c(console::ROLE_ACCENT), t.c(console::ROLE_INK), clampf(intensity_, 0.f, 1.f));
        if (w <= console::SCREEN_W) {
            sdk::drawText(c, (console::SCREEN_W - w) / 2, 0, s, col);
        } else {
            int period = w + TITLE_GAP;
            int x = console::SCREEN_W - int(fmodf(scroll_, float(period)));
            sdk::drawText(c, x, 0, s, col);
            sdk::drawText(c, x + period, 0, s, col);
        }
    }

    void drawPlay(Canvas& c, const Theme& t) {
        c.clear(t.c(console::ROLE_BG));

        int midi = MIDI_MIN + noteIdx_;
        int pc = ((midi % 12) + 12) % 12;
        int oct = midi / 12 - 1;
        static const char LET[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
        static const bool SH[12] = {false, true, false, true, false, false,
                                    true, false, true, false, true, false};
        // keep pitch hues in the brighter half of the ramp so low notes stay vivid
        Color noteCol = rampAt(t, 0.35f + 0.65f * (pc / 11.f));

        // bottom meter: jumps to full on a press, then a breathing idle floor
        float shimmer = 0.10f + 0.06f * (0.5f + 0.5f * sinf(timeMs_ * 0.006f));
        float lvl = noteEnv_ > shimmer ? noteEnv_ : shimmer;
        int lit = int(lvl * 6.f + 0.5f);
        if (lit < 1) lit = 1;
        if (lit > 6) lit = 6;
        for (int j = 0; j < lit; ++j) {
            int y = (console::SCREEN_H - 1) - j;
            Color col = mixc(dimc(noteCol, 0.5f), noteCol, float(j) / 5.f);
            col = dimc(col, 0.55f + 0.45f * noteEnv_);
            for (int x = 0; x < console::SCREEN_W; ++x) c.pixel(x, y, col);
        }
        for (int x = 0; x < console::SCREEN_W; ++x) c.pixel(x, 11, dimc(t.c(console::ROLE_DIM), 0.5f));

        // note name tints toward the note colour when struck but stays legible
        Color txt = mixc(t.c(console::ROLE_INK), noteCol, 0.6f * clampf(noteEnv_, 0.f, 1.f));
        if (SH[pc]) {
            sdk::drawChar(c, 0, 0, LET[pc], txt);
            drawSharp(c, 3, 0, txt);
        } else {
            sdk::drawChar(c, 1, 0, LET[pc], txt);
        }
        sdk::drawChar(c, 1, 6, char('0' + (oct < 0 ? 0 : oct)), t.c(console::ROLE_ACCENT2));
    }

    console::Audio* audio_ = nullptr;
    sdk::Rng rng_{1};

    Mode mode_ = MODE_TRACK;
    int trackIdx_ = 0;
    bool playing_ = false;
    bool attract_ = false;
    uint32_t idleMs_ = 0;

    float timeMs_ = 0.f;
    float beatMs_ = 500.f;
    float beatPhase_ = 0.f;
    float beatEnv_ = 0.f;
    float intensity_ = IDLE_INTENSITY;
    float scroll_ = 0.f;
    float level_[BAR_N] = {};
    int barH_[BAR_N] = {};
    float peak_[BAR_N] = {};
    Particle part_[MAXP] = {};

    int noteIdx_ = 12;
    float noteEnv_ = 0.f;

    GameMeta meta_{"jukebox", nullptr, 1};
};

}  // namespace jukebox
