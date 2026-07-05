// Host test for the brain-side transport adapter. LinkFrameSink encodes a frame
// (and a light-profile change) and pushes it into a LinkLoopback; we drain the
// loopback exactly as ScreenRenderBoard does (frameType -> frameDecodeInto /
// lightDecode) and assert the round-trip. Proves the OS's FrameSink port reaches
// the wire and back byte-faithfully (RGB565 quantization is the only, documented,
// loss). Build/run: make -C modules/link test
#include <cstdio>
#include <cstring>
#include "console/frame_proto.h"
#include "link_frame_sink.h"
#include "link_loopback.h"

static int g_failures = 0;
static void check(bool ok, const char* msg) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) ++g_failures;
}

int main() {
    using namespace console;
    printf("link frame-sink adapter test\n");

    // A deterministic, non-trivial canvas (every pixel distinct-ish).
    Color src[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i)
        src[i] = Color{uint8_t(i * 7), uint8_t(i * 3 + 5), uint8_t(255 - i)};

    LinkLoopback           link;
    console_os::LinkFrameSink sink(link);

    // Emit one full frame through the adapter.
    sink.frame(src, 42);

    // Drain like the screen board does.
    Color dst[SCREEN_PX];
    for (int i = 0; i < SCREEN_PX; ++i) dst[i] = BLACK;
    uint8_t buf[FRAME_MAX_PACKET];
    int len = link.recvRaw(buf, sizeof(buf));
    check(len > 0, "adapter produced a datagram on the wire");
    check(len > 0 && frameType(buf, (size_t)len) == MSG_FRAME, "datagram is an MSG_FRAME");
    check(frameDecodeInto(buf, (size_t)len, dst), "frame decodes into a canvas");

    // Every pixel matches the source after the documented RGB565 quantization.
    bool allMatch = true;
    for (int i = 0; i < SCREEN_PX; ++i) {
        Color expect = from565(to565(src[i]));
        if (memcmp(&dst[i], &expect, sizeof(Color)) != 0) { allMatch = false; break; }
    }
    check(allMatch, "decoded frame equals the source (565-quantized)");
    check(link.recvRaw(buf, sizeof(buf)) == 0, "exactly one datagram was sent");

    // Light-profile change round-trips exactly (no quantization).
    LightProfile lp{123, 45, 67, 89, 210};  // wallBrightness,dimLevel,decay,bloom,gamma
    sink.light(lp);
    len = link.recvRaw(buf, sizeof(buf));
    check(len > 0 && frameType(buf, (size_t)len) == MSG_SET_LIGHT_PROFILE,
          "light() emits a SET_LIGHT_PROFILE datagram");
    LightProfile got{};
    check(len > 0 && lightDecode(buf, (size_t)len, got), "light profile decodes");
    check(memcmp(&lp, &got, sizeof(LightProfile)) == 0, "light profile round-trips exactly");

    printf(g_failures ? "\nLINK ADAPTER TEST FAILED (%d)\n" : "\nLINK ADAPTER TEST OK\n", g_failures);
    return g_failures ? 1 : 0;
}
