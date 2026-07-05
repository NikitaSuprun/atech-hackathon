#pragma once
#include <stdint.h>
#include "console/audio.h"

// The console shell's voice over the shared (monophonic) speaker. BrainOS calls it
// SEMANTICALLY — navTick / select / openSettings / back / idle — and knows nothing
// about frequencies, melodies, or loop timing. The soundscape itself is DATA (a
// UiSoundScheme), so there are no bare constants at the call site, and a theme can
// later supply its own scheme (see Theme's planned per-theme AudioSet).

namespace console_os {

// A tunable soundscape. Notes are MIDI (Audio::note handles Hz); motifs are RTTTL
// ("name:d=dur,o=octave,b=bpm:notes"). Every value is named — no magic literals.
struct UiSoundScheme {
    uint8_t     navNote;       // scroll-detent blip
    uint16_t    navMs;
    uint8_t     backNote;      // return-to-menu blip
    uint16_t    backMs;
    const char* launch;        // rising confirm when a game launches
    const char* openSettings;  // entering settings
    const char* ambient;       // gentle loop while the menu sits idle
    uint32_t    settleMs;      // idle time before the ambient loop starts
    uint32_t    loopMs;        // ambient relaunch cadence (~ its own length)
};

// Default, theme-agnostic scheme: warm + sparse, reads as feedback not noise.
constexpr UiSoundScheme kDefaultUiSounds{
    /* navNote      */ 88, /* navMs  */ 12,   // E6
    /* backNote     */ 76, /* backMs */ 34,   // E5
    /* launch       */ "go:d=16,o=6,b=200:c,e,g",
    /* openSettings */ "op:d=16,o=6,b=210:d,a",
    /* ambient      */ "amb:d=4,o=5,b=58:c,e,g,e,a,g,e,c",
    /* settleMs     */ 1100,
    /* loopMs       */ 9000,
};

class UiAudio {
public:
    explicit UiAudio(console::Audio* audio, const UiSoundScheme& scheme = kDefaultUiSounds)
        : audio_(audio), s_(scheme) {}

    // Let a theme (or the caller) restyle the whole soundscape.
    void setScheme(const UiSoundScheme& scheme) { s_ = scheme; }

    void navTick()      { blip(s_.navNote, s_.navMs); rearm(); }
    void select()       { motif(s_.launch); rearm(); }
    void openSettings() { motif(s_.openSettings); rearm(); }
    void back()         { blip(s_.backNote, s_.backMs); rearm(); }

    // Call each idle menu tick with ms since the last navigation. Starts the
    // ambient once you've settled and relaunches it on cadence — never overlapping
    // itself, and bounded so it can't spam a non-playing host stub.
    void idle(uint32_t sinceNavMs) {
        if (audio_ && sinceNavMs >= ambientAt_ && !audio_->playing()) {
            audio_->melody(s_.ambient);
            ambientAt_ = sinceNavMs + s_.loopMs;
        }
    }

private:
    // Blips preempt (monophonic): stop whatever's playing, then sound.
    void blip(uint8_t note, uint16_t ms) { if (audio_) { audio_->stop(); audio_->note(note, ms); } }
    void motif(const char* rtttl)        { if (audio_) { audio_->stop(); audio_->melody(rtttl); } }
    void rearm()                         { ambientAt_ = s_.settleMs; }

    console::Audio* audio_;
    UiSoundScheme   s_;
    uint32_t        ambientAt_ = 0;
};

}  // namespace console_os
