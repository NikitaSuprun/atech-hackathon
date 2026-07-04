#pragma once
#include <Arduino.h>
#include "link_frame.h"
#include "pong_link.h"

// FALLBACK console transport: the "PKT:<hex>\n" line format of the original
// modules/pong_screen/link_serial.h, but with the MTU and line buffer raised
// from 64 B / 160 chars to CONSOLE_LINK_MTU so a 108-px frame fits. Text lines
// survive a serial monitor and dumb relays; the binary COBS transport
// (link_cobs_serial.h) is preferred for throughput. Same PongLink seam +
// LinkUdpServer-compat shims.
class LinkHexSerial : public PongLink {
public:
    bool begin() override { return true; }
    void poll(uint32_t) override {}
    bool isUp() const override { return true; }

    bool sendRaw(const void* buf, size_t len) override {
        static const char* H = "0123456789ABCDEF";
        const uint8_t* p = (const uint8_t*)buf;
        if (len > CONSOLE_LINK_MTU) return false;
        char out[5 + 2 * CONSOLE_LINK_MTU];
        int o = 0;
        out[o++] = 'P'; out[o++] = 'K'; out[o++] = 'T'; out[o++] = ':';
        for (size_t i = 0; i < len; ++i) {
            out[o++] = H[p[i] >> 4];
            out[o++] = H[p[i] & 15];
        }
        out[o++] = '\n';
        Serial.write((const uint8_t*)out, o);
        return true;
    }

    int recvRaw(void* buf, size_t maxLen) override {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                int n = decodeLine(buf, maxLen);
                lineLen_ = 0;
                if (n > 0) return n;
            } else if (lineLen_ < (int)sizeof(line_) - 1) {
                line_[lineLen_++] = c;
            } else {
                lineLen_ = 0;
            }
        }
        return 0;
    }

    bool sendTo(const IPAddress&, uint16_t, const void* buf, size_t len) {
        return sendRaw(buf, len);
    }
    IPAddress lastRemoteIp() const { return IPAddress(1, 1, 1, 1); }
    uint16_t lastRemotePort() const { return 1; }

private:
    static int hexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }
    int decodeLine(void* buf, size_t maxLen) {
        if (lineLen_ < 6 || memcmp(line_, "PKT:", 4) != 0) return 0;
        int hexChars = lineLen_ - 4;
        if (hexChars % 2) return 0;
        int n = hexChars / 2;
        if (n > (int)maxLen) return 0;
        uint8_t* out = (uint8_t*)buf;
        for (int i = 0; i < n; ++i) {
            int hi = hexVal(line_[4 + 2 * i]), lo = hexVal(line_[5 + 2 * i]);
            if (hi < 0 || lo < 0) return 0;
            out[i] = (uint8_t)((hi << 4) | lo);
        }
        return n;
    }
    char line_[4 + 2 * CONSOLE_LINK_MTU + 2];
    int  lineLen_ = 0;
};
