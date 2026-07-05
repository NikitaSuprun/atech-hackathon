#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "input.h"

// Host <-> board control & telemetry. A dev host (laptop bridge, tuner, WASM
// harness) drives the console with a handful of commands and reads back board
// state. Pure structs + (de)serialization; both ends are little-endian so a
// packed struct goes on the wire as-is. No Arduino, no heap.
//
// Loss-tolerant by the same rules as the game link: injected knob positions are
// ABSOLUTE cumulative detents (any packet re-establishes input), button state
// is a HELD level, and seq guards reorder.

namespace console {

constexpr uint32_t HOST_MAGIC   = 0x54534F48u;  // wire bytes 'H','O','S','T'
constexpr uint8_t  HOST_VERSION = 1;

enum HostMsgType : uint8_t {
    // host -> board
    HOST_SET_VOL      = 0x10,  // set master volume
    HOST_SET_THEME    = 0x11,  // select active theme index
    HOST_INJECT_INPUT = 0x12,  // act as a virtual controller
    HOST_SELECT_GAME  = 0x13,  // launch a game by id
    // board -> host
    BOARD_TELEMETRY   = 0x20,  // periodic state snapshot
    BOARD_INPUT       = 0x21,  // live knob positions + buttons (passive twin mirror)
    BOARD_NAV         = 0x22,  // ~10-B OS nav state so a twin renders the real TFT
};

#pragma pack(push, 1)

struct HostHeader {
    uint32_t magic;    // HOST_MAGIC
    uint8_t  version;  // HOST_VERSION
    uint8_t  type;     // HostMsgType
    uint16_t seq;      // per-boot monotonic
};
static_assert(sizeof(HostHeader) == 8, "host header layout");

// Decoded input on the wire: absolute cumulative detents + held levels.
struct WireInput {
    int32_t knobPos[2];  // knob[0] = P1/left, knob[1] = P2/right
    uint8_t heldBits;    // bit0 = knob0 button, bit1 = knob1 button
};

// ---- host -> board commands ----

struct SetVolCmd {
    HostHeader h;
    uint8_t    volume;  // 0..255 -> Audio::setVolume(volume / 255.0f)
    static constexpr uint8_t TYPE = HOST_SET_VOL;
};
struct SetThemeCmd {
    HostHeader h;
    uint8_t    themeIndex;  // ThemeManager::setActive(themeIndex)
    static constexpr uint8_t TYPE = HOST_SET_THEME;
};
struct InjectInputCmd {
    HostHeader h;
    WireInput  in;
    static constexpr uint8_t TYPE = HOST_INJECT_INPUT;
};
struct SelectGameCmd {
    HostHeader h;
    uint8_t    gameId;
    static constexpr uint8_t TYPE = HOST_SELECT_GAME;
};

// ---- board -> host telemetry ----

struct BoardTelemetry {
    HostHeader h;
    uint8_t    gameId;
    uint8_t    gameState;   // app-defined (e.g. PongGameState)
    uint8_t    themeIndex;  // active theme
    uint8_t    volume;      // current master volume, 0..255
    uint8_t    score[2];    // app-defined
    uint8_t    linkUp;      // controller link associated (bool)
    uint16_t   fps;         // rendered frames/s (diagnostics)
    static constexpr uint8_t TYPE = BOARD_TELEMETRY;
};

// Live knob state, board -> host, so a passive mirror (the browser twin) can show
// the real knobs. Same WireInput the host injects, just the other direction.
struct BoardInput {
    HostHeader h;
    WireInput  in;
    static constexpr uint8_t TYPE = BOARD_INPUT;
};

// High-level OS navigation state, board -> host. The TFT dashboard is a pure
// function of these ~10 bytes, so a browser twin renders the REAL TftDashboard
// from this alone — no pixel streaming, no seed, no per-tick input lockstep.
struct BoardNav {
    HostHeader h;
    uint8_t mode;             // console_os::Mode (Boot/Menu/Settings/Game)
    uint8_t menuSel;          // launcher selection
    int8_t  activeGameIndex;  // -1 in menu
    uint8_t settingsRow;      // settings-screen row
    uint8_t overlay;          // (open ? 0x10 : 0) | (sel & 0x0F)
    uint8_t themeIndex;
    uint8_t brightness;
    uint8_t volume;
    uint8_t flags;            // bit0 = menuMusic
    static constexpr uint8_t TYPE = BOARD_NAV;
};

#pragma pack(pop)

// ---------------- (de)serialization ----------------
// Each message carries its own TYPE, so one generic pair covers all of them.

// Stamp the header and copy msg -> buf. Returns bytes written, 0 if buf small.
template <typename T>
inline size_t hostEncode(T msg, uint16_t seq, uint8_t* buf, size_t cap) {
    if (cap < sizeof(T)) return 0;
    msg.h.magic = HOST_MAGIC;
    msg.h.version = HOST_VERSION;
    msg.h.type = T::TYPE;
    msg.h.seq = seq;
    memcpy(buf, &msg, sizeof(T));
    return sizeof(T);
}

// Validate (size, magic, version, type) and copy buf -> out. False on mismatch.
template <typename T>
inline bool hostDecode(const uint8_t* buf, size_t len, T& out) {
    if (len != sizeof(T)) return false;
    memcpy(&out, buf, sizeof(T));
    return out.h.magic == HOST_MAGIC && out.h.version == HOST_VERSION &&
           out.h.type == T::TYPE;
}

// Peek the message type without knowing which struct it is, 0 if header bad.
inline uint8_t hostPeekType(const uint8_t* buf, size_t len) {
    if (len < sizeof(HostHeader)) return 0;
    HostHeader hd;
    memcpy(&hd, buf, sizeof(hd));
    if (hd.magic != HOST_MAGIC || hd.version != HOST_VERSION) return 0;
    return hd.type;
}

// Turn a stream of absolute WireInputs into the per-tick console::Input a game
// reads (signed deltas + button edges). The first decode anchors without a
// jump or a phantom edge; int32 subtraction is wrap-safe.
struct InputDecoder {
    int32_t lastPos[2] = {0, 0};
    bool    lastDown[2] = {false, false};
    bool    init = false;

    Input decode(const WireInput& w) {
        Input in{};
        for (int k = 0; k < 2; ++k) {
            bool down = ((w.heldBits >> k) & 1) != 0;
            int32_t pos = w.knobPos[k];
            in.knob[k].pos = pos;
            in.knob[k].delta = init ? (pos - lastPos[k]) : 0;
            in.knob[k].down = down;
            in.knob[k].justPressed = init && down && !lastDown[k];
            in.knob[k].justReleased = init && !down && lastDown[k];
            lastPos[k] = pos;
            lastDown[k] = down;
        }
        init = true;
        return in;
    }
};

}  // namespace console
