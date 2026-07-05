#pragma once
#include <stdint.h>
#include "console/frame_proto.h"
#include "console/host_proto.h"  // console::BoardInput / WireInput / hostEncode
#include "frame_sink.h"  // console_os::FrameSink   (-I ../console_os)
#include "pong_link.h"   // PongLink                (-I ../pong_screen)

// The brain's concrete FrameSink: encodes each composed frame (and every
// light-profile change) with the console wire protocol and pushes the raw packet
// into a PongLink. Datagram transports (UDP/ESP-NOW/loopback) send the packet
// as-is; a byte-stream link (LinkCobsSerial) COBS-frames it internally. This is
// the adapter that lets BrainOS stay transport-agnostic — the OS speaks only
// console_os::FrameSink; this bridges that port to the wire.

namespace console_os {

class LinkFrameSink : public FrameSink {
public:
    explicit LinkFrameSink(PongLink& link) : link_(link) {}

    void frame(const console::Color* px, uint16_t seq) override {
        size_t n = console::frameEncode(px, 0, 0, console::SCREEN_W, console::SCREEN_H,
                                        seq, buf_, sizeof(buf_));
        if (n) link_.sendRaw(buf_, n);
    }

    void light(const console::LightProfile& lp) override {
        size_t n = console::lightEncode(lp, lightSeq_++, buf_, sizeof(buf_));
        if (n) link_.sendRaw(buf_, n);
    }

    void input(const console::WireInput& wi) override {
        console::BoardInput msg{};
        msg.in = wi;
        size_t n = console::hostEncode(msg, inputSeq_++, buf_, sizeof(buf_));
        if (n) link_.sendRaw(buf_, n);
    }

    void nav(const console::BoardNav& n) override {
        size_t sz = console::hostEncode(n, navSeq_++, buf_, sizeof(buf_));
        if (sz) link_.sendRaw(buf_, sz);
    }

private:
    PongLink& link_;
    uint16_t  lightSeq_ = 0;
    uint16_t  inputSeq_ = 0;
    uint16_t  navSeq_   = 0;
    uint8_t   buf_[console::FRAME_MAX_PACKET];
};

}  // namespace console_os
