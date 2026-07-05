#pragma once
#include <math.h>
#include <stdint.h>
#include "console/audio.h"
#include "speaker.h"

// console::Audio backed by the I2S Speaker module (MAX98357A). The OS + games call
// this abstract sink; it maps onto the speaker's non-blocking background task —
// raw tones, MIDI notes, and RTTTL melodies (the jukebox game streams RTTTL
// through melody()). Nothing else in the console touches the speaker directly.
class SpeakerAudio : public console::Audio {
public:
    explicit SpeakerAudio(Speaker& sp) : sp_(sp) {}

    void tone(uint16_t freqHz, uint16_t ms) override { sp_.playTone(float(freqHz), int(ms)); }
    void note(uint8_t midi, uint16_t ms) override { sp_.playNote(midiToFreq(midi), int(ms)); }
    void melody(const char* rtttl) override { sp_.playRTTTL(rtttl); }
    void stop() override { sp_.stop(); }
    bool playing() const override { return sp_.isPlaying(); }
    void setVolume(float v01) override { sp_.setVolume(v01); }

private:
    static float midiToFreq(uint8_t m) {
        return 440.0f * powf(2.0f, float(int(m) - 69) / 12.0f);
    }
    Speaker& sp_;
};
