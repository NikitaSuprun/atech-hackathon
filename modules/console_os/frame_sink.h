#pragma once
#include <stdint.h>
#include "console/color.h"
#include "console/config.h"
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
};

}  // namespace console_os
