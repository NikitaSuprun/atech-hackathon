#pragma once
#include <stdint.h>
#include "console/color.h"
#include "console/config.h"
#include "console/host_proto.h"
#include "console/theme.h"

// Where the OS emits each composed tick. The OS never talks to a transport: it
// hands the finished Color[SCREEN_PX] frame + the (brightness-adjusted)
// LightProfile to a sink. On hardware the sink is frame_proto's encoder over
// USB-CDC/UDP; in host tests it is a capture buffer. light() fires only when the
// profile actually changes (theme switch or brightness change).

namespace console_os {

class FrameSink {
public:
    virtual ~FrameSink() {}
    virtual void frame(const console::Color* px, uint16_t seq) = 0;
    virtual void light(const console::LightProfile& lp) = 0;
    // Live knob state for a passive mirror (the browser twin); no-op by default so
    // host-test capture sinks need not implement it.
    virtual void input(const console::WireInput&) {}
    // High-level OS nav state so a twin renders the real TFT from it; no-op default.
    virtual void nav(const console::BoardNav&) {}
};

}  // namespace console_os
