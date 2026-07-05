#pragma once
#include <Arduino.h>
#include "link_frame.h"
#include "pong_link.h"

// PRIMARY console transport: binary COBS-framed datagrams over USB-CDC serial.
// Each sendRaw() COBS-encodes the packet and writes it followed by a 0x00
// delimiter; recvRaw() reassembles the stream through CobsReader. Binary (vs
// LinkSerial's hex) ~halves the wire bytes and, with the raised MTU, lets a
// 229-B frame packet through. Drop-in for the PongLink seam, with the same
// LinkUdpServer-compat shims so screen glue compiles unchanged.
class LinkCobsSerial : public PongLink {
public:
    bool begin() override { return true; }
    void poll(uint32_t) override {}
    bool isUp() const override { return true; }

    bool sendRaw(const void* buf, size_t len) override {
        if (len > CONSOLE_LINK_MTU) return false;
        uint8_t enc[CONSOLE_LINK_FRAME_MAX];
        size_t n = console::cobsEncode((const uint8_t*)buf, len, enc, sizeof(enc));
        if (n == 0) return false;
        enc[n++] = 0;  // frame delimiter
        // Serial runs non-blocking (setTxTimeoutMs(0)); only send if the framed
        // packet fits the TX buffer whole — a partial write would tear the COBS
        // framing. Dropping the frame instead self-heals via the screen heartbeat.
        if (Serial.availableForWrite() < (int)n) return false;
        Serial.write(enc, n);
        return true;
    }

    int recvRaw(void* buf, size_t maxLen) override {
        while (Serial.available()) {
            size_t n = reader_.feed((uint8_t)Serial.read(), (uint8_t*)buf, maxLen);
            if (n > 0) return (int)n;
        }
        return 0;
    }

    // LinkUdpServer-compat shims (single-peer over serial)
    bool sendTo(const IPAddress&, uint16_t, const void* buf, size_t len) {
        return sendRaw(buf, len);
    }
    IPAddress lastRemoteIp() const { return IPAddress(1, 1, 1, 1); }
    uint16_t lastRemotePort() const { return 1; }

private:
    CobsReader reader_;
};
