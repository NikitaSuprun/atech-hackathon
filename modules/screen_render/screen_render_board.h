#pragma once
#include <stdint.h>
#include "console/config.h"
#include "console/frame_proto.h"
#include "pong_link.h"
#include "screen_render.h"

// Console SCREEN firmware entry point — the board glue that composes the DUMB
// renderer with the transport. It owns nothing about games: it drains frame
// packets off the link, keeps a persistent logical canvas (so dirty-rect frames
// accumulate), applies SET_LIGHT_PROFILE control messages, and eases the light
// engine toward the canvas once per tick. The compositor's dirty cache means
// only changed tiles hit the LEDs; a periodic heartbeat force-repaints to heal
// WS2812 glitches. Mirrors modules/pong_screen/pong_screen.{h,cpp} in shape.
//
// The transport (a PongLink) and the clock are INJECTED: the caller owns the
// link — LinkCobsSerial on the wire, the ESP-NOW/failover link, or a host
// LinkLoopback — and passes millis() (or a synthetic clock in host tests). That
// keeps this glue transport-agnostic and host-testable end to end.
class ScreenRenderBoard {
public:
    explicit ScreenRenderBoard(PongLink& link) : link_(link) {}

    // tiles[] in wiring/port order (matches TILE_MAP); n should be NUM_TILES.
    void begin(console::TileSink** tiles, int n, const console::LightProfile& lp, uint32_t nowMs) {
        n_ = n;
        for (int i = 0; i < console::SCREEN_PX; ++i) canvas_[i] = console::BLACK;
        renderer_.begin(tiles, n, lp);
        ok_ = link_.begin() && (n == NUM_TILES);
        lastTickMs_ = lastPaintMs_ = nowMs;
    }

    void tick(uint32_t now) {
        link_.poll(now);
        // Qualify: an unnamespaced pong TICK_MS also leaks in via pong_config.h.
        if ((uint32_t)(now - lastTickMs_) < console::TICK_MS) return;
        lastTickMs_ = now;

        bool force = false;
        uint8_t buf[console::FRAME_MAX_PACKET];
        for (int i = 0; i < PONG_LINK_DRAIN; ++i) {
            int len = link_.recvRaw(buf, sizeof(buf));
            if (len <= 0) break;
            switch (console::frameType(buf, (size_t)len)) {
                case console::MSG_FRAME:
                    console::frameDecodeInto(buf, (size_t)len, canvas_);
                    break;
                case console::MSG_SET_LIGHT_PROFILE: {
                    console::LightProfile lp;
                    if (console::lightDecode(buf, (size_t)len, lp)) {
                        renderer_.setLightProfile(lp);
                        force = true;  // brightness/feel changed: repaint all
                    }
                    break;
                }
                default:
                    break;
            }
        }

        const uint32_t heartbeatMs = (uint32_t)(HEARTBEAT_REPAINT_S * 1000.0f);
        if ((uint32_t)(now - lastPaintMs_) >= heartbeatMs) force = true;
        if (force) lastPaintMs_ = now;

        renderer_.renderFrame(canvas_, force);
    }

    bool ok() const { return ok_; }

    // The persistent logical canvas the link has decoded so far (pre-glow). Read
    // only — for host E2E assertions and passive twins.
    const console::Color* canvas() const { return canvas_; }

private:
    console::ScreenRenderer renderer_;
    PongLink&               link_;
    console::Color          canvas_[console::SCREEN_PX];
    int                     n_ = 0;
    bool                    ok_ = false;
    uint32_t                lastTickMs_ = 0;
    uint32_t                lastPaintMs_ = 0;
};
