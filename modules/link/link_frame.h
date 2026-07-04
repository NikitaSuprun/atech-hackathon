#pragma once
#include <stddef.h>
#include <stdint.h>
#include "console/frame_proto.h"
#include "pong_link.h"

// Console transport layer, built on the PongLink seam (reuse pong_link.h). The
// game link's 64-B MTU only had to carry 24/26-B packets; a full 108-px frame
// packet is 229 B, so the console transports raise the MTU here (the pong
// transports keep their small one untouched). Framing is binary COBS + a 0x00
// delimiter (link_cobs_serial.h); the raised hex fallback lives in
// link_hex_serial.h and the ESP-NOW seam stub in link_espnow.h.

// Fits FRAME_MAX_PACKET (229) with headroom for future message growth.
constexpr size_t CONSOLE_LINK_MTU = 300;

// Encoded COBS block + 0x00 delimiter for the largest datagram.
constexpr size_t CONSOLE_LINK_FRAME_MAX = console::cobsMaxEncoded(CONSOLE_LINK_MTU) + 1;

// Pure streaming COBS de-framer: feed the raw byte stream one byte at a time;
// on the 0x00 that ends a block it decodes the accumulated bytes into `out` and
// returns the decoded length (0 otherwise). No heap; a fixed accumulator that
// resets on overflow so a garbage burst can't wedge it. Host-tested.
struct CobsReader {
    uint8_t acc[CONSOLE_LINK_FRAME_MAX];
    size_t  len = 0;

    void reset() { len = 0; }

    size_t feed(uint8_t byte, uint8_t* out, size_t outCap) {
        if (byte == 0) {
            size_t n = console::cobsDecode(acc, len, out, outCap);
            len = 0;
            return n;
        }
        if (len < sizeof(acc)) {
            acc[len++] = byte;
        } else {
            len = 0;  // overrun: drop this block, resync on the next delimiter
        }
        return 0;
    }
};
