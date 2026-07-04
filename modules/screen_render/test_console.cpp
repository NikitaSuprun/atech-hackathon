// Off-hardware verification for the console screen platform: COBS framing, the
// frame + host wire protocols, the light-engine math, and the full renderer
// pipeline (light engine -> reused pong compositor -> pixel sink). No Arduino,
// no hardware. Build + run: `make -C modules/screen_render test`.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "console/frame_proto.h"
#include "console/host_proto.h"
#include "light_engine.h"
#include "screen_render.h"
#include "link_frame.h"
#include "link_espnow.h"  // ESP-NOW stub must at least compile as a PongLink

using namespace console;

static int g_fail = 0;
static void check(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}
static bool ceq(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// Local stub LightProfiles (themes.h is being written concurrently — not included).
static const LightProfile kNeon{40, 80, 200, 96, 100};   // long trails, soft bloom
static const LightProfile kCrisp{51, 80, 0, 0, 100};     // no trail, no bloom

// ---------------- COBS ----------------

static void testCobsRoundtrip(const std::vector<uint8_t>& src, const char* what) {
    std::vector<uint8_t> enc(cobsMaxEncoded(src.size()) + 1);
    size_t n = cobsEncode(src.data(), src.size(), enc.data(), enc.size());
    bool noZero = true;
    for (size_t i = 0; i < n; ++i)
        if (enc[i] == 0) noZero = false;
    std::vector<uint8_t> dec(src.size() + 8);
    size_t m = cobsDecode(enc.data(), n, dec.data(), dec.size());
    bool ok = (n > 0) && noZero && (m == src.size()) &&
              (memcmp(dec.data(), src.data(), src.size()) == 0);
    check(ok, what);
}

static void testCobs() {
    // The required check: a full 108-px frame packet COBS roundtrips byte-identical.
    Color canvas[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i)
        canvas[i] = (i % 5 == 0) ? Color{uint8_t(i * 2), 0, uint8_t(255 - i)} : BLACK;
    uint8_t pkt[FRAME_MAX_PACKET];
    size_t len = frameEncode(canvas, 0, 0, SCREEN_W, SCREEN_H, 7, pkt, sizeof(pkt));
    check(len == FRAME_MAX_PACKET, "108-px frame packet is 229 bytes");
    testCobsRoundtrip(std::vector<uint8_t>(pkt, pkt + len),
                      "108-px frame COBS-decodes byte-identical");

    testCobsRoundtrip({}, "COBS empty");
    testCobsRoundtrip(std::vector<uint8_t>(SCREEN_PX * 2, 0),
                      "COBS all-zero payload (216 B)");
    std::vector<uint8_t> ff(600, 0xFF);  // > 254 forces a code-byte split
    testCobsRoundtrip(ff, "COBS 600x 0xFF (code-byte split)");
    std::vector<uint8_t> mix;
    for (int i = 0; i < 500; ++i) mix.push_back(uint8_t((i * 37) % 256));
    testCobsRoundtrip(mix, "COBS mixed 500 B");
}

// ---------------- CobsReader stream reassembly ----------------

static void testStream() {
    // Two packets, COBS-framed, concatenated into one byte stream (mirrors
    // LinkCobsSerial::sendRaw feeding LinkCobsSerial::recvRaw).
    Color canvas[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i) canvas[i] = {uint8_t(i), uint8_t(2 * i), 0};
    uint8_t a[FRAME_MAX_PACKET], b[sizeof(LightMsg)];
    size_t la = frameEncode(canvas, 0, 0, SCREEN_W, SCREEN_H, 1, a, sizeof(a));
    size_t lb = lightEncode(kNeon, 2, b, sizeof(b));

    std::vector<uint8_t> stream;
    uint8_t enc[CONSOLE_LINK_FRAME_MAX];
    for (auto pr : {std::pair<uint8_t*, size_t>{a, la}, {b, lb}}) {
        size_t n = cobsEncode(pr.first, pr.second, enc, sizeof(enc));
        stream.insert(stream.end(), enc, enc + n);
        stream.push_back(0);  // delimiter
    }

    CobsReader reader;
    uint8_t out[CONSOLE_LINK_MTU];
    std::vector<std::vector<uint8_t>> got;
    for (uint8_t byte : stream) {
        size_t n = reader.feed(byte, out, sizeof(out));
        if (n) got.emplace_back(out, out + n);
    }
    bool ok = got.size() == 2 && got[0].size() == la && got[1].size() == lb &&
              memcmp(got[0].data(), a, la) == 0 && memcmp(got[1].data(), b, lb) == 0;
    check(ok, "CobsReader splits a 2-packet stream byte-identically");
}

// ---------------- frame protocol ----------------

static void test565() {
    bool inv = true, idem = true;
    for (int v = 0; v < 65536; ++v)
        if (to565(from565(uint16_t(v))) != uint16_t(v)) inv = false;
    for (int i = 0; i < 256; ++i) {
        Color c{uint8_t(i), uint8_t(255 - i), uint8_t((i * 3) & 0xFF)};
        Color q = from565(to565(c));
        if (!ceq(q, from565(to565(q)))) idem = false;
    }
    check(inv, "to565(from565(v)) == v for all 565 values");
    check(idem, "from565(to565(c)) is 565-idempotent");
}

static void testFrameProto() {
    Color canvas[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i)
        canvas[i] = {uint8_t(i * 2), uint8_t(i + 30), uint8_t(255 - i)};

    // full frame
    uint8_t pkt[FRAME_MAX_PACKET];
    size_t len = frameEncode(canvas, 0, 0, SCREEN_W, SCREEN_H, 42, pkt, sizeof(pkt));
    FrameHeader hd;
    memcpy(&hd, pkt, sizeof(hd));
    check(hd.magic == FRAME_MAGIC && hd.version == FRAME_VERSION &&
              hd.type == MSG_FRAME && hd.fmt == FMT_RGB565 && hd.seq == 42 &&
              hd.x == 0 && hd.y == 0 && hd.w == SCREEN_W && hd.h == SCREEN_H,
          "full-frame header fields");
    check(frameType(pkt, len) == MSG_FRAME, "frameType peeks MSG_FRAME");

    Color dec[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i) dec[i] = {1, 2, 3};
    bool ok = frameDecodeInto(pkt, len, dec);
    bool exact = true;
    for (int i = 0; i < SCREEN_PX; ++i)
        if (!ceq(dec[i], from565(to565(canvas[i])))) exact = false;
    check(ok && exact, "full-frame decode == 565-quantized source");

    // dirty rect: change a 2x3 block, encode just it, decode onto a fresh canvas
    Color scene[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i) scene[i] = BLACK;
    for (int y = 5; y < 8; ++y)
        for (int x = 2; x < 4; ++x) scene[y * SCREEN_W + x] = {200, 10, 40};
    uint8_t rp[FRAME_MAX_PACKET];
    size_t rl = frameEncode(scene, 2, 5, 2, 3, 1, rp, sizeof(rp));
    check(rl == sizeof(FrameHeader) + 2 * 3 * 2, "dirty-rect packet size");
    Color out[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i) out[i] = BLACK;
    frameDecodeInto(rp, rl, out);
    bool rectOk = true;
    for (int i = 0; i < SCREEN_PX; ++i) {
        int x = i % SCREEN_W, y = i / SCREEN_W;
        bool inRect = (x >= 2 && x < 4 && y >= 5 && y < 8);
        Color want = inRect ? from565(to565(Color{200, 10, 40})) : BLACK;
        if (!ceq(out[i], want)) rectOk = false;
    }
    check(rectOk, "dirty-rect decode touches only the rect");

    // rejects: out-of-bounds rect, truncated buffer
    check(frameEncode(canvas, 5, 0, 2, 1, 0, pkt, sizeof(pkt)) == 0,
          "frameEncode rejects x+w > W");
    check(!frameDecodeInto(pkt, sizeof(FrameHeader) - 1, dec),
          "frameDecodeInto rejects short buffer");

    // SET_LIGHT_PROFILE control message
    uint8_t lp[sizeof(LightMsg)];
    size_t ll = lightEncode(kNeon, 9, lp, sizeof(lp));
    check(frameType(lp, ll) == MSG_SET_LIGHT_PROFILE, "frameType peeks light msg");
    LightProfile got{};
    bool lok = lightDecode(lp, ll, got);
    check(lok && memcmp(&got, &kNeon, sizeof(LightProfile)) == 0,
          "SET_LIGHT_PROFILE roundtrip");
    check(!frameDecodeInto(lp, ll, dec), "frameDecodeInto rejects a light msg");
}

// ---------------- host protocol ----------------

static void testHostProto() {
    uint8_t buf[64];

    SetVolCmd vol{};
    vol.volume = 128;
    size_t n = hostEncode(vol, 1, buf, sizeof(buf));
    SetVolCmd volOut{};
    check(n == sizeof(SetVolCmd) && hostDecode(buf, n, volOut) && volOut.volume == 128 &&
              volOut.h.seq == 1 && hostPeekType(buf, n) == HOST_SET_VOL,
          "SET_VOL roundtrip");

    SetThemeCmd th{};
    th.themeIndex = 3;
    n = hostEncode(th, 2, buf, sizeof(buf));
    SetThemeCmd thOut{};
    check(hostDecode(buf, n, thOut) && thOut.themeIndex == 3, "SET_THEME roundtrip");

    SelectGameCmd sg{};
    sg.gameId = 5;
    n = hostEncode(sg, 3, buf, sizeof(buf));
    SelectGameCmd sgOut{};
    check(hostDecode(buf, n, sgOut) && sgOut.gameId == 5, "SELECT_GAME roundtrip");

    InjectInputCmd inj{};
    inj.in = {{-100, 250}, 0x02};
    n = hostEncode(inj, 4, buf, sizeof(buf));
    InjectInputCmd injOut{};
    check(hostDecode(buf, n, injOut) && injOut.in.knobPos[0] == -100 &&
              injOut.in.knobPos[1] == 250 && injOut.in.heldBits == 0x02,
          "INJECT_INPUT roundtrip");

    BoardTelemetry tel{};
    tel.gameId = 1;
    tel.gameState = 3;
    tel.themeIndex = 2;
    tel.volume = 64;
    tel.score[0] = 2;
    tel.score[1] = 1;
    tel.linkUp = 1;
    tel.fps = 50;
    n = hostEncode(tel, 5, buf, sizeof(buf));
    BoardTelemetry telOut{};
    check(hostDecode(buf, n, telOut) && telOut.fps == 50 && telOut.score[0] == 2 &&
              telOut.linkUp == 1 && telOut.gameState == 3,
          "BOARD_TELEMETRY roundtrip");

    // validation: wrong type struct, corrupted magic, wrong length all rejected
    SetThemeCmd wrong{};
    check(!hostDecode(buf, n, wrong), "hostDecode rejects type mismatch");
    n = hostEncode(vol, 1, buf, sizeof(buf));
    buf[0] ^= 0xFF;
    check(!hostDecode(buf, n, volOut) && hostPeekType(buf, n) == 0,
          "hostDecode rejects bad magic");

    // InputDecoder: first packet anchors (no delta, no edge), then deltas + edges
    InputDecoder d;
    Input i0 = d.decode({{10, -5}, 0x00});
    check(i0.knob[0].delta == 0 && i0.knob[1].delta == 0 && !i0.knob[0].justPressed,
          "InputDecoder first packet anchors (no jump)");
    Input i1 = d.decode({{13, -5}, 0x01});
    check(i1.knob[0].delta == 3 && i1.knob[0].down && i1.knob[0].justPressed &&
              i1.knob[1].delta == 0,
          "InputDecoder yields delta + press edge");
    Input i2 = d.decode({{13, -5}, 0x00});
    check(i2.knob[0].justReleased && !i2.knob[0].justPressed,
          "InputDecoder yields release edge");
}

// ---------------- light engine ----------------

static void testLightDecay() {
    // Bright white pixel, target black, decay=127 -> release alpha 128: the
    // channel must halve-and-floor deterministically to zero (assert the math).
    LightProfile lp{51, 80, 127, 0, 100};
    LightEngine le;
    le.reset(WHITE);
    Color target[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i) target[i] = BLACK;
    Color out[SCREEN_PX];
    const uint8_t expect[] = {127, 64, 32, 16, 8, 4, 2, 1, 0, 0};
    bool ok = true;
    for (int k = 0; k < 10; ++k) {
        le.step(target, lp, out);
        if (le.field[0].r != expect[k]) ok = false;
    }
    check(ok, "decay: 255 -> 127,64,32,16,8,4,2,1,0 at decay=127");

    // decay=0 -> release snaps straight to the frame (no trail)
    LightProfile crisp{51, 80, 0, 0, 100};
    le.reset(WHITE);
    le.step(target, crisp, out);
    check(le.field[0].r == 0, "decay=0 snaps to target (no trail)");

    // decay=255 -> holds the peak forever (max trail)
    LightProfile hold{51, 80, 255, 0, 100};
    le.reset(WHITE);
    for (int k = 0; k < 20; ++k) le.step(target, hold, out);
    check(le.field[0].r == 255, "decay=255 holds peak (infinite trail)");
}

static void testLightAttack() {
    LightProfile lp{51, 80, 200, 0, 100};
    LightEngine le;
    le.reset(BLACK);
    Color target[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i) target[i] = WHITE;
    Color out[SCREEN_PX];
    const uint8_t expect[] = {230, 252, 254, 255, 255};
    bool ok = true;
    for (int k = 0; k < 5; ++k) {
        le.step(target, lp, out);
        if (le.field[0].r != expect[k]) ok = false;
    }
    check(ok, "attack: 0 -> 230,252,254,255 (eased, reaches target)");

    // a static frame is converged to exactly and held
    for (int k = 0; k < 5; ++k) le.step(target, lp, out);
    bool conv = true;
    for (int i = 0; i < SCREEN_PX; ++i)
        if (!ceq(le.field[i], WHITE)) conv = false;
    check(conv, "field converges to a static frame");
}

static void testBloom() {
    LightEngine le;
    le.reset(BLACK);
    const int cx = 3, cy = 9, ci = cy * SCREEN_W + cx;
    le.field[ci] = {200, 0, 0};                  // seed one lit pixel directly
    Color target[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i) target[i] = le.field[i];
    LightProfile lp{51, 80, 255, 255, 100};      // hold field, full bloom
    Color out[SCREEN_PX];
    le.step(target, lp, out);
    int nb[] = {(cy - 1) * SCREEN_W + cx, (cy + 1) * SCREEN_W + cx,
                cy * SCREEN_W + (cx - 1), cy * SCREEN_W + (cx + 1)};
    bool ok = out[ci].r == 200;                  // source unchanged (its neighbours dark)
    for (int k = 0; k < 4; ++k) ok = ok && out[nb[k]].r == 50;  // 200/4 halo
    int far = cy * SCREEN_W + (cx + 2);
    ok = ok && out[far].r == 0;
    check(ok, "bloom=255 adds neighbour/4 halo, source intact");
}

// ---------------- full renderer pipeline ----------------

struct CaptureSink : console::TileSink {
    uint8_t rgb[9][3] = {};
    int shows = 0;
    uint8_t brightness = 0;
    void setPixel(int chip, uint8_t r, uint8_t g, uint8_t b) override {
        if (chip >= 0 && chip < 9) {
            rgb[chip][0] = r;
            rgb[chip][1] = g;
            rgb[chip][2] = b;
        }
    }
    void show() override { shows++; }
    void setBrightness(uint8_t b) override { brightness = b; }
};

static void testRenderer() {
    CaptureSink sinks[NUM_TILES];
    TileSink* ptrs[NUM_TILES];
    for (int t = 0; t < NUM_TILES; ++t) ptrs[t] = &sinks[t];

    ScreenRenderer r;
    r.begin(ptrs, NUM_TILES, kCrisp);  // decay=0, bloom=0 -> converges exactly
    check(sinks[0].brightness == kCrisp.wallBrightness,
          "renderer sets sink brightness to wallBrightness");

    // unique-per-pixel target so the tile/rotation mapping is observable
    Color target[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i)
        target[i] = {uint8_t(i + 1), uint8_t((i * 3) & 0xFF), uint8_t(255 - i)};

    for (int k = 0; k < 12; ++k) r.renderFrame(target, false);  // ease to convergence

    // Independently run the reused compositor over the same frame; the renderer
    // must have driven each sink with exactly the compositor's mapped output.
    Compositor comp;
    comp.begin();
    pong::Frame pf;
    memcpy(pf.px, target, sizeof(pf.px));
    bool mapOk = true;
    for (int t = 0; t < NUM_TILES; ++t) {
        uint8_t exp[TILE_BYTES];
        comp.composeTile(t, pf, exp, true);
        if (memcmp(sinks[t].rgb, exp, TILE_BYTES) != 0) mapOk = false;
    }
    check(mapOk, "renderer output == pong compositor mapping (serpentine+rotation)");

    // dirty cache: a converged no-op frame pushes zero shows
    for (int t = 0; t < NUM_TILES; ++t) sinks[t].shows = 0;
    r.renderFrame(target, false);
    int shows = 0;
    for (int t = 0; t < NUM_TILES; ++t) shows += sinks[t].shows;
    check(shows == 0, "dirty cache: unchanged converged frame shows nothing");

    // touch one tile's region (top-left 3x3 = tile at row0/col0) -> only tiles
    // covering changed pixels re-show
    Color t2[SCREEN_PX];
    memcpy(t2, target, sizeof(t2));
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) t2[y * SCREEN_W + x] = {5, 5, 5};
    for (int t = 0; t < NUM_TILES; ++t) sinks[t].shows = 0;
    for (int k = 0; k < 12; ++k) r.renderFrame(t2, false);
    int touched = 0;
    for (int t = 0; t < NUM_TILES; ++t)
        if (sinks[t].shows > 0) touched++;
    check(touched >= 1 && touched <= 2, "dirty cache: a 3x3 change repaints only local tiles");

    // force repaint hits every bound tile
    for (int t = 0; t < NUM_TILES; ++t) sinks[t].shows = 0;
    r.renderFrame(t2, true);
    int all = 0;
    for (int t = 0; t < NUM_TILES; ++t) all += (sinks[t].shows > 0);
    check(all == NUM_TILES, "force repaints all tiles (heartbeat)");
}

static void testEspNowStub() {
    LinkEspNow link;
    uint8_t b[4] = {1, 2, 3, 4};
    check(!link.begin() && !link.isUp() && !link.sendRaw(b, 4) && link.recvRaw(b, 4) == 0,
          "ESP-NOW stub compiles as PongLink and is inert");
}

int main() {
    testCobs();
    testStream();
    test565();
    testFrameProto();
    testHostProto();
    testLightDecay();
    testLightAttack();
    testBloom();
    testRenderer();
    testEspNowStub();
    printf(g_fail ? "\nCONSOLE TESTS FAILED (%d)\n" : "\nCONSOLE TESTS OK\n", g_fail);
    return g_fail ? 1 : 0;
}
