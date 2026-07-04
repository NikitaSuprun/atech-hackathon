#include "pong_control.h"

// temporary bring-up telemetry; remove when the link is proven stable
#define PONG_CTRL_DEBUG 1
static uint32_t txOk_ = 0;

void PongControl::begin(RotaryEncoder* k1, RotaryEncoder* k2, Speaker* spk, ST7735_TFT* tft) {
    link_.begin();
    audio_.begin(spk);
    display_.begin(tft);
    rings_.begin(k1, k2);
    seq_ = 0;
}

void PongControl::service(int32_t p1Pos, bool p1Held, int32_t p2Pos, bool p2Held) {
    const uint32_t now = millis();
    uint32_t dtMs = now - lastServiceMs_;
    if (dtMs > 100) dtMs = 100;
    lastServiceMs_ = now;

    link_.poll(now);

#ifdef PONG_CTRL_DEBUG
    static uint32_t dbgMs = 0;
    if (now - dbgMs >= 1000) {
        dbgMs = now;
        Serial.printf("{\"type\":\"log\",\"module\":\"pong_control\",\"up\":%d,\"seq\":%u,\"fb\":%d,\"tx\":%u}\n",
                      (int)link_.isUp(), (unsigned)seq_, (int)tracker_.linked(), (unsigned)txOk_);
    }
#endif

    if (link_.isUp() && now - lastSendMs_ >= INPUT_PERIOD_MS) {
        PongInputPacket pkt = {};
        pkt.magic = PONG_MAGIC;
        pkt.version = PONG_VERSION;
        pkt.type = PKT_INPUT;
        pkt.controllerId = 0;
        pkt.heldBits = (uint8_t)((p1Held ? PONG_HELD_P1 : 0) | (p2Held ? PONG_HELD_P2 : 0));
        pkt.seq = ++seq_;
        pkt.uptimeMs = now;
        pkt.knobPos[0] = p1Pos;
        pkt.knobPos[1] = p2Pos;
        if (link_.sendRaw(&pkt, sizeof(pkt))) ++txOk_;
        lastSendMs_ = now;
    }

    // bounded drain keeps the loop watchdog-safe under bursts
    uint8_t buf[PONG_LINK_MTU];
    for (int i = 0; i < PONG_LINK_DRAIN; ++i) {
        int n = link_.recvRaw(buf, sizeof(buf));
        if (n <= 0) break;
        tracker_.feed(buf, n, now);
    }

    const PongCues cues = tracker_.poll(now);
    audio_.apply(tracker_.snapshot(), cues, dtMs);
    display_.apply(tracker_.snapshot(), cues, tracker_.linked());
    rings_.apply(tracker_.snapshot(), cues, tracker_.linked(), dtMs);
}
