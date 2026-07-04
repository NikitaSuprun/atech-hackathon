#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "color.h"
#include "config.h"
#include "theme.h"

// Brain -> screen wire protocol: pixel frames (full or dirty-rect) plus a
// SET_LIGHT_PROFILE control message. Pure C++ (no Arduino, no heap) so it
// compiles in firmware, the desktop sim and the WASM build unchanged.
//
// A packet is header + payload. Two message types share the leading
// {magic, version, type, seq} so a receiver validates then dispatches on type.
// The byte-stream transport (USB-CDC) frames packets with COBS + a 0x00
// delimiter (see the helpers at the bottom); datagram transports (UDP/ESP-NOW)
// send the raw packet since they are already framed.

namespace console {

constexpr uint32_t FRAME_MAGIC   = 0x4D524643u;  // wire bytes 'C','F','R','M'
constexpr uint8_t  FRAME_VERSION = 1;

enum FrameMsgType : uint8_t {
    MSG_FRAME             = 0x01,  // FrameHeader + RGB565 payload (full or rect)
    MSG_SET_LIGHT_PROFILE = 0x02,  // LightMsg, a few bytes, sent on theme change
};

// Only RGB565 today (2 bytes/px, LE); RGB888 is reserved for a later data add.
enum PixelFmt : uint8_t { FMT_RGB565 = 0, FMT_RGB888 = 1 };

#pragma pack(push, 1)

struct FrameHeader {            // MSG_FRAME                             off sz
    uint32_t magic;            // FRAME_MAGIC                             0  4
    uint8_t  version;          // FRAME_VERSION                          4  1
    uint8_t  type;             // MSG_FRAME                              5  1
    uint16_t seq;              // per-boot monotonic frame counter       6  2
    uint8_t  fmt;              // PixelFmt (FMT_RGB565)                   8  1
    uint8_t  x, y;             // dirty-rect origin px (full = 0,0)       9  2
    uint8_t  w, h;             // dirty-rect extent px (full = W,H)      11  2
};                             // total 13, then w*h*2 RGB565 bytes
static_assert(sizeof(FrameHeader) == 13, "frame header layout");

struct LightMsg {              // MSG_SET_LIGHT_PROFILE                 off sz
    uint32_t     magic;        // FRAME_MAGIC                             0  4
    uint8_t      version;      // FRAME_VERSION                          4  1
    uint8_t      type;         // MSG_SET_LIGHT_PROFILE                  5  1
    uint16_t     seq;          //                                        6  2
    LightProfile profile;      // wallBrightness/dim/decay/bloom/gamma    8  5
};                             // total 13
static_assert(sizeof(LightMsg) == 13, "light message layout");

#pragma pack(pop)

// Largest MSG_FRAME packet = header + a full 108-px RGB565 frame.
constexpr size_t FRAME_MAX_PACKET = sizeof(FrameHeader) + size_t(SCREEN_PX) * 2;  // 229

// RGB565 -> RGB888 (inverse of color.h to565; to565(from565(v)) == v).
constexpr Color from565(uint16_t v) {
    uint8_t r5 = uint8_t((v >> 11) & 0x1F);
    uint8_t g6 = uint8_t((v >> 5) & 0x3F);
    uint8_t b5 = uint8_t(v & 0x1F);
    return {uint8_t((r5 << 3) | (r5 >> 2)), uint8_t((g6 << 2) | (g6 >> 4)),
            uint8_t((b5 << 3) | (b5 >> 2))};
}

// ---------------- frame packets ----------------

// Serialize the (x,y,w,h) rect of a SCREEN_PX canvas as an MSG_FRAME packet.
// Full frame: x=0,y=0,w=SCREEN_W,h=SCREEN_H. Returns bytes written, 0 on bad
// rect / short buffer.
inline size_t frameEncode(const Color* canvas, uint8_t x, uint8_t y, uint8_t w,
                          uint8_t h, uint16_t seq, uint8_t* out, size_t cap) {
    if (x + w > SCREEN_W || y + h > SCREEN_H || w == 0 || h == 0) return 0;
    size_t need = sizeof(FrameHeader) + size_t(w) * h * 2;
    if (cap < need) return 0;
    FrameHeader hd{FRAME_MAGIC, FRAME_VERSION, MSG_FRAME, seq, FMT_RGB565, x, y, w, h};
    memcpy(out, &hd, sizeof(hd));
    uint8_t* p = out + sizeof(hd);
    for (uint8_t j = 0; j < h; ++j)
        for (uint8_t i = 0; i < w; ++i) {
            uint16_t v = to565(canvas[(y + j) * SCREEN_W + (x + i)]);
            *p++ = uint8_t(v & 0xFF);
            *p++ = uint8_t(v >> 8);
        }
    return need;
}

// Peek the message type of a packet, 0 if magic/version/length are bad.
inline uint8_t frameType(const uint8_t* buf, size_t len) {
    if (len < 8) return 0;
    FrameHeader hd;
    memcpy(&hd, buf, 8);
    if (hd.magic != FRAME_MAGIC || hd.version != FRAME_VERSION) return 0;
    return hd.type;
}

// Blit an MSG_FRAME packet's rect into a SCREEN_PX canvas (RGB565 -> Color).
// Returns false on any validation failure (canvas untouched then).
inline bool frameDecodeInto(const uint8_t* buf, size_t len, Color canvas[SCREEN_PX]) {
    if (len < sizeof(FrameHeader)) return false;
    FrameHeader hd;
    memcpy(&hd, buf, sizeof(hd));
    if (hd.magic != FRAME_MAGIC || hd.version != FRAME_VERSION ||
        hd.type != MSG_FRAME || hd.fmt != FMT_RGB565)
        return false;
    if (hd.x + hd.w > SCREEN_W || hd.y + hd.h > SCREEN_H || hd.w == 0 || hd.h == 0)
        return false;
    if (len < sizeof(FrameHeader) + size_t(hd.w) * hd.h * 2) return false;
    const uint8_t* p = buf + sizeof(hd);
    for (uint8_t j = 0; j < hd.h; ++j)
        for (uint8_t i = 0; i < hd.w; ++i) {
            uint16_t v = uint16_t(p[0]) | uint16_t(p[1]) << 8;
            p += 2;
            canvas[(hd.y + j) * SCREEN_W + (hd.x + i)] = from565(v);
        }
    return true;
}

// ---------------- SET_LIGHT_PROFILE control message ----------------

inline size_t lightEncode(const LightProfile& lp, uint16_t seq, uint8_t* out, size_t cap) {
    if (cap < sizeof(LightMsg)) return 0;
    LightMsg m{FRAME_MAGIC, FRAME_VERSION, MSG_SET_LIGHT_PROFILE, seq, lp};
    memcpy(out, &m, sizeof(m));
    return sizeof(m);
}

inline bool lightDecode(const uint8_t* buf, size_t len, LightProfile& out) {
    if (len < sizeof(LightMsg)) return false;
    LightMsg m;
    memcpy(&m, buf, sizeof(m));
    if (m.magic != FRAME_MAGIC || m.version != FRAME_VERSION ||
        m.type != MSG_SET_LIGHT_PROFILE)
        return false;
    out = m.profile;
    return true;
}

// ---------------- COBS (Consistent Overhead Byte Stuffing) ----------------
// Byte-stuff a buffer so it contains no 0x00, then a single 0x00 delimits the
// packet on the stream. Overhead is 1 byte per 254, so a 229-B frame costs 1.
// Pure, no heap: caller owns all buffers.

// Worst-case cobsEncode output length (excluding the 0x00 delimiter).
constexpr size_t cobsMaxEncoded(size_t n) { return n + n / 254 + 1; }

// Encode src -> dst (no trailing delimiter). Returns encoded length, 0 if dst
// is too small (need >= cobsMaxEncoded(srcLen)).
inline size_t cobsEncode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
    if (dstCap < cobsMaxEncoded(srcLen)) return 0;
    size_t readIdx = 0, writeIdx = 1, codeIdx = 0;
    uint8_t code = 1;
    while (readIdx < srcLen) {
        if (src[readIdx] == 0) {
            dst[codeIdx] = code;
            code = 1;
            codeIdx = writeIdx++;
            readIdx++;
        } else {
            dst[writeIdx++] = src[readIdx++];
            if (++code == 0xFF) {
                dst[codeIdx] = code;
                code = 1;
                codeIdx = writeIdx++;
            }
        }
    }
    dst[codeIdx] = code;
    return writeIdx;
}

// Decode a COBS block (the bytes between delimiters, delimiter excluded).
// Returns decoded length, 0 on malformed input or dst overflow.
inline size_t cobsDecode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
    size_t readIdx = 0, writeIdx = 0;
    while (readIdx < srcLen) {
        uint8_t code = src[readIdx++];
        if (code == 0) return 0;
        for (uint8_t i = 1; i < code; ++i) {
            if (readIdx >= srcLen || writeIdx >= dstCap) return 0;
            dst[writeIdx++] = src[readIdx++];
        }
        if (code != 0xFF && readIdx < srcLen) {
            if (writeIdx >= dstCap) return 0;
            dst[writeIdx++] = 0;
        }
    }
    return writeIdx;
}

}  // namespace console
