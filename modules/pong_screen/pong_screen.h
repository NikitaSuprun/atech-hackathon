#pragma once
#include <Arduino.h>
#include "neopixel.h"
#include "pong_frame.h"
#include "pong_config.h"
#include "pong_proto.h"
#include "pong_engine.h"
#include "net_config.h"
#include "compositor.h"
#include "link_udp_server.h"

// Arduino glue: hosts the 12 wall tiles, pumps UDP input into per-controller
// slots, drives the pure engine at a fixed step, pushes dirty tiles through
// the compositor, and sends 20 Hz feedback to every learned controller.
class PongScreen {
public:
    // tiles = grids in wiring order (ports 1..7,9..11,13,14); n must be 12
    void begin(NeoPixelGrid** tiles, int n);
    // never blocks; worst case ~10 ms (full-wall repaint)
    void tick();
    bool ok() const { return ok_; }

private:
    struct ControllerSlot {
        IPAddress ip;
        uint16_t port = 0;
        uint32_t lastRxMs = 0;
        uint32_t lastSeq = 0;
        uint32_t lastUptime = 0;
        uint32_t fbSeq = 0;
        int32_t knobPos[2] = {0, 0};
        uint8_t heldBits = 0;
        bool everSeen = false;
        bool rebooted = false;
    };

    void pumpInput(uint32_t now);
    void sendFeedback();
    void pushFrame(bool force, uint32_t now);
    bool heartbeatDue(uint32_t now) const;
    void drawIdentify(uint32_t now);

    NeoPixelGrid* tiles_[NUM_TILES] = {};
    ControllerSlot slots_[PONG_MAX_CONTROLLERS];
    pong::Engine engine_;
    Compositor comp_;
    LinkUdpServer link_;
    pong::Frame frame_;
    int n_ = 0;
    bool ok_ = false;
    bool everLinked_ = false;
    uint8_t lastState_ = 0xFF;
    uint8_t identifyPhase_ = 0;
    uint32_t accumMs_ = 0;
    uint32_t lastTickMs_ = 0;
    uint32_t lastRenderMs_ = 0;
    uint32_t lastFullPaintMs_ = 0;
    uint32_t lastFeedbackMs_ = 0;
    uint32_t lastIdentifyMs_ = 0;
    uint32_t lastDebugMs_ = 0;
};
