#include "pong_screen.h"

static uint32_t s_rxAny = 0, s_rxOk = 0;
#include <esp_system.h>
#include <string.h>

namespace {

const uint32_t kHeartbeatMs = (uint32_t)(HEARTBEAT_REPAINT_S * 1000.0f);

// identify hues R,G,B,Y,M,C at ~180, reused per tile column (i % 6)
const pong::Color kIdHues[6] = {
    {180, 0, 0},   {0, 180, 0},   {0, 0, 180},
    {180, 180, 0}, {180, 0, 180}, {0, 180, 180},
};

#ifdef PONG_CALIBRATION_MODE
// full-screen TILE_MAP verify glyph, asymmetric on both axes:
// "F" in the top half, down-arrow in the bottom half,
// top row solid COL_P2, bottom row solid COL_P1
void drawVerifyGlyph(pong::Frame& f) {
    f.clear();
    const pong::Color w = COL_BALL;
    for (int y = 1; y <= 8; ++y) f.at(1, y) = w;
    for (int x = 1; x <= 4; ++x) f.at(x, 1) = w;
    for (int x = 1; x <= 3; ++x) f.at(x, 4) = w;
    for (int y = 9; y <= 12; ++y) f.at(2, y) = w;
    for (int x = 0; x <= 4; ++x) f.at(x, 13) = w;
    for (int x = 1; x <= 3; ++x) f.at(x, 14) = w;
    f.at(2, 15) = w;
    for (int x = 0; x < pong::W; ++x) {
        f.at(x, 0) = COL_P2;
        f.at(x, pong::H - 1) = COL_P1;
    }
}
#endif

}  // namespace

void PongScreen::begin(NeoPixelGrid** tiles, int n) {
    n_ = n > NUM_TILES ? NUM_TILES : n;
    for (int t = 0; t < n_; ++t) {
        tiles_[t] = tiles[t];
        if (tiles_[t]) tiles_[t]->setBrightness(WALL_BRIGHTNESS);
    }
    comp_.begin();
    bool up = link_.begin();
    engine_.reset(esp_random());
    ok_ = (n == NUM_TILES) && up;
    lastTickMs_ = millis();
}

void PongScreen::tick() {
    uint32_t now = millis();
    link_.poll(now);
    pumpInput(now);

    ControllerSlot& c0 = slots_[0];
    pong::EngineInputs in = {};
    in.knobPos[0] = c0.knobPos[0];
    in.knobPos[1] = c0.knobPos[1];
    in.held[0] = (c0.heldBits & PONG_HELD_P1) != 0;
    in.held[1] = (c0.heldBits & PONG_HELD_P2) != 0;
    bool linked = c0.everSeen && (uint32_t)(now - c0.lastRxMs) <= INPUT_TIMEOUT_MS;
    in.controllerLinked = linked;
    in.controllerRebooted = c0.rebooted;
    if (linked) everLinked_ = true;

    accumMs_ += (uint32_t)(now - lastTickMs_);
    lastTickMs_ = now;
    int steps = 0;
    while (accumMs_ >= TICK_MS && steps < 5) {
        engine_.tick(in, TICK_MS);
        in.controllerRebooted = false;
        accumMs_ -= TICK_MS;
        ++steps;
    }
    // reboot flag is one-shot: clear only once the engine consumed it
    if (steps > 0) c0.rebooted = false;
    if (accumMs_ > 5 * TICK_MS) accumMs_ = 5 * TICK_MS;

    if ((uint32_t)(now - lastRenderMs_) >= TICK_MS) {
        lastRenderMs_ = now;
#ifdef PONG_CALIBRATION_MODE
        drawVerifyGlyph(frame_);
        pushFrame(heartbeatDue(now), now);
#else
        uint8_t state = engine_.status().state;
        if (!everLinked_ && state == GS_LINK_WAIT) {
            drawIdentify(now);
        } else {
            engine_.render(frame_);
            pushFrame(state != lastState_ || heartbeatDue(now), now);
            lastState_ = state;
        }
#endif
    }

    if ((uint32_t)(now - lastFeedbackMs_) >= FEEDBACK_PERIOD_MS) {
        lastFeedbackMs_ = now;
        sendFeedback();
    }

#ifdef PONG_DEBUG
    if ((uint32_t)(now - lastDebugMs_) >= 1000) {
        lastDebugMs_ = now;
        pong::EngineStatus est = engine_.status();
        Serial.printf(
            "{\"type\":\"log\",\"module\":\"pong_screen\",\"state\":%d,"
            "\"score\":[%d,%d],\"linked\":%d,\"ap\":%d,\"held\":%d,\"rxAny\":%u,\"rxOk\":%u}\n",
            (int)est.state, (int)est.score[0], (int)est.score[1],
            (int)linked, (int)link_.isUp(), (int)est.heldBits,
            (unsigned)s_rxAny, (unsigned)s_rxOk);
    }
#endif
}

void PongScreen::pumpInput(uint32_t now) {
    for (int i = 0; i < PONG_LINK_DRAIN; ++i) {
        uint8_t buf[PONG_LINK_MTU];
        int n = link_.recvRaw(buf, sizeof(buf));
        if (n <= 0) break;
        ++s_rxAny;
        if (n != (int)sizeof(PongInputPacket)) continue;
        PongInputPacket pkt;
        memcpy(&pkt, buf, sizeof(pkt));
        if (pkt.magic != PONG_MAGIC || pkt.version != PONG_VERSION ||
            pkt.type != PKT_INPUT || pkt.controllerId >= PONG_MAX_CONTROLLERS)
            continue;
        ControllerSlot& s = slots_[pkt.controllerId];
        if (s.everSeen) {
            // uptime regression = controller rebooted: accept + re-anchor tracking
            if (pkt.uptimeMs < s.lastUptime) {
                s.rebooted = true;
            } else if ((int32_t)(pkt.seq - s.lastSeq) <= 0) {
                continue;
            }
        }
        s.everSeen = true;
        ++s_rxOk;
        s.lastRxMs = now;
        s.lastSeq = pkt.seq;
        s.lastUptime = pkt.uptimeMs;
        s.knobPos[0] = pkt.knobPos[0];
        s.knobPos[1] = pkt.knobPos[1];
        s.heldBits = pkt.heldBits;
        s.ip = link_.lastRemoteIp();
        s.port = link_.lastRemotePort();
    }
}

void PongScreen::sendFeedback() {
    pong::EngineStatus est = engine_.status();
    for (uint8_t id = 0; id < PONG_MAX_CONTROLLERS; ++id) {
        ControllerSlot& s = slots_[id];
        if (!s.everSeen || s.port == 0) continue;
        PongFeedbackPacket fb;
        fb.magic = PONG_MAGIC;
        fb.version = PONG_VERSION;
        fb.type = PKT_FEEDBACK;
        fb.targetControllerId = id;
        fb.gameState = est.state;
        fb.seq = ++s.fbSeq;
        fb.score[0] = est.score[0];
        fb.score[1] = est.score[1];
        fb.heldBits = est.heldBits;
        fb.readyProgress[0] = est.readyProgress[0];
        fb.readyProgress[1] = est.readyProgress[1];
        fb.paddleHitSeq = est.paddleHitSeq;
        fb.wallBounceSeq = est.wallBounceSeq;
        fb.goalSeq = est.goalSeq;
        fb.goalBy = est.goalBy;
        fb.winSeq = est.winSeq;
        fb.winner = est.winner;
        fb.serveSeq = est.serveSeq;
        fb.servingPlayer = est.servingPlayer;
        fb.pad = 0;
        link_.sendTo(s.ip, s.port, &fb, sizeof(fb));
    }
}

void PongScreen::pushFrame(bool force, uint32_t now) {
    if (force) lastFullPaintMs_ = now;
    uint8_t buf[TILE_BYTES];
    for (int t = 0; t < NUM_TILES && t < n_; ++t) {
        NeoPixelGrid* g = tiles_[t];
        if (!g) continue;
        if (!comp_.composeTile(t, frame_, buf, force)) continue;
        for (int i = 0; i < LEDS_PER_TILE; ++i)
            g->setPixel(i, buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]);
        g->show();
    }
}

bool PongScreen::heartbeatDue(uint32_t now) const {
    return (uint32_t)(now - lastFullPaintMs_) >= kHeartbeatMs;
}

// pre-first-link calibration/identify pattern; bypasses the compositor on
// purpose — its whole job is to DISCOVER TILE_MAP. Doubles as the RMT smoke
// test for all 24 strip objects.
void PongScreen::drawIdentify(uint32_t now) {
    if ((uint32_t)(now - lastIdentifyMs_) < 50) return;
    lastIdentifyMs_ = now;
    // alternate halves: ~6 show()s per call, each tile refreshed ~10 Hz
    identifyPhase_ ^= 1;
    for (int t = identifyPhase_; t < n_; t += 2) {
        NeoPixelGrid* g = tiles_[t];
        if (!g) continue;
        const pong::Color& c = kIdHues[t % 6];
        g->setAll(c.r, c.g, c.b);
        // chip 0 = white rotation corner, blinks (t+1)x per cycle (dark pulses)
        uint32_t cycle = (uint32_t)(t + 1) * 400u + 1000u;
        uint32_t ph = now % cycle;
        bool off = ph < (uint32_t)(t + 1) * 400u && (ph % 400u) < 200u;
        if (off) g->setPixel(0, 0, 0, 0);
        else g->setPixel(0, 255, 255, 255);
        // chip 1 = dim gray top-edge marker (guards against diagonal misreads)
        g->setPixel(1, 70, 70, 70);
        g->show();
    }
}
