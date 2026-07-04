#pragma once
#include <Arduino.h>
#include "rotary_encoder.h"
#include "speaker.h"
#include "st7735_tft.h"
#include "pong_proto.h"
#include "pong_link.h"
#include "net_config.h"
#include "feedback_tracker.h"
#include "audio_director.h"
#include "score_display.h"
#include "ring_fx.h"
#include "link_udp_client.h"

// Controller glue: samples arrive via service() args (module.yaml loop
// template), inputs go out at 50 Hz, feedback drains through the tracker
// into the three presenters. Nothing here ever blocks.
class PongControl {
public:
    void begin(RotaryEncoder* k1, RotaryEncoder* k2, Speaker* spk, ST7735_TFT* tft);
    void service(int32_t p1Pos, bool p1Held, int32_t p2Pos, bool p2Held);

private:
    LinkUdpClient link_;
    FeedbackTracker tracker_;
    AudioDirector audio_;
    ScoreDisplay display_;
    RingFX rings_;
    uint32_t seq_ = 0;
    uint32_t lastSendMs_ = 0;
    uint32_t lastServiceMs_ = 0;
};
