#pragma once
#include <stdint.h>

// Abstract audio sink. The brain's I2S adapter implements it on hardware; the
// sim is a no-op; WASM routes to WebAudio. Games/OS call this, never the speaker.

namespace console {

class Audio {
public:
    virtual ~Audio() {}
    virtual void tone(uint16_t freqHz, uint16_t ms) = 0;
    virtual void note(uint8_t midi, uint16_t ms)    = 0;
    virtual void melody(const char* rtttl)          = 0;  // async, non-blocking
    virtual void stop()                             = 0;
    virtual bool playing() const                    = 0;
    virtual void setVolume(float v01)               = 0;  // 0..1
};

}  // namespace console
