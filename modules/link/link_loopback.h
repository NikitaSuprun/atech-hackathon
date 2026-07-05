#pragma once
#include <stddef.h>
#include <stdint.h>
#include "console/frame_proto.h"  // cobsEncode
#include "link_frame.h"           // CobsReader, CONSOLE_LINK_MTU / _FRAME_MAX
#include "pong_link.h"            // PongLink

// Host/test transport: an in-memory PongLink that mirrors LinkCobsSerial's wire
// behaviour (COBS-framed datagrams + 0x00 delimiter over a byte pipe) WITHOUT
// Arduino, so the full brain -> screen path can run in a host build. sendRaw()
// COBS-encodes the packet into an internal ring; recvRaw() reassembles one
// datagram at a time via CobsReader — byte-identical to the serial link, minus
// the UART. Single-direction (brain -> screen) is all the E2E and sim need; this
// is a test transport, never flashed.
class LinkLoopback : public PongLink {
public:
    bool begin() override { return true; }
    void poll(uint32_t) override {}
    bool isUp() const override { return true; }

    bool sendRaw(const void* buf, size_t len) override {
        if (len > CONSOLE_LINK_MTU) return false;
        uint8_t enc[CONSOLE_LINK_FRAME_MAX];
        size_t n = console::cobsEncode((const uint8_t*)buf, len, enc, sizeof(enc));
        if (n == 0) return false;
        enc[n++] = 0;  // 0x00 frame delimiter
        for (size_t i = 0; i < n; ++i) {
            size_t next = (head_ + 1) % kCap;
            if (next == tail_) return false;  // wire full (test under-sized the ring)
            wire_[head_] = enc[i];
            head_ = next;
        }
        return true;
    }

    int recvRaw(void* buf, size_t maxLen) override {
        while (tail_ != head_) {
            uint8_t b = wire_[tail_];
            tail_ = (tail_ + 1) % kCap;
            size_t n = reader_.feed(b, (uint8_t*)buf, maxLen);
            if (n > 0) return (int)n;
        }
        return 0;
    }

private:
    static constexpr size_t kCap = 8192;  // a few frames of COBS wire
    uint8_t    wire_[kCap];
    size_t     head_ = 0, tail_ = 0;
    CobsReader reader_;
};
