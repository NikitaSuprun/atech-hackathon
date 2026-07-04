// Generic desktop host for ANY console::Game — no hardware, no game-specific code.
// Modes:
//   (no args)            interactive ANSI-truecolor terminal (6x18), keyboard input
//   --dump [frames [n]]  headless F-format frame dump (tools/gifgen/dump_frames.cpp shape)
//   --selftest           deterministic no-TTY smoke of the runner + SDK font
// The game is chosen at link time (REGISTER_GAME in exactly one game .cpp).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "console/color.h"
#include "console/config.h"
#include "console/game.h"
#include "console/input.h"
#include "console/theme.h"
#include "registry.h"
#include "runner.h"
#include "sdk.h"
#include "stub_theme.h"

using console::Color;
using console::Game;
using console::Input;
using console::Theme;
using console::SCREEN_H;
using console::SCREEN_PX;
using console::SCREEN_W;
using console::TICK_MS;

static constexpr int MAX_CATCHUP_STEPS = 4;   // fixed-timestep catch-up cap

// ---------------- terminal state ----------------

static struct termios g_origTio;
static bool           g_ttyRaw = false;

static void restoreTty() {
    if (g_ttyRaw) tcsetattr(0, TCSANOW, &g_origTio);
    printf("\x1b[?25h\x1b[0m\n");
}

// ---------------- headless frame dump (F + 18 rows of 6 hex tokens) ----------------

static void dumpFrame(const Color* buf) {
    puts("F");
    for (int y = 0; y < SCREEN_H; ++y)
        for (int x = 0; x < SCREEN_W; ++x) {
            const Color& c = buf[y * SCREEN_W + x];
            printf("%02X%02X%02X%c", c.r, c.g, c.b, x == SCREEN_W - 1 ? '\n' : ' ');
        }
}

static int runDump(Game* g, const Theme* th, int frames, int stride, uint32_t seed) {
    host::GameRunner r(g, th);
    r.init(seed);
    Input zero{};   // neutral input; the game self-animates for the dump
    int total = frames * stride;
    for (int t = 0; t < total; ++t) {
        r.tick(zero, TICK_MS);
        if (t % stride == 0) dumpFrame(r.frame());
    }
    return 0;
}

// ---------------- selftest (CI gate, no TTY) ----------------

static void check(bool ok, const char* what, int& fails) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

// deterministic scripted input so two runs must match bit-for-bit
static void scriptInput(host::InputSampler& s, int t) {
    if (t % 7 == 0)  s.rotate(0, +1);
    if (t % 11 == 0) s.rotate(1, -1);
    if (t % 25 == 0) s.toggleButton(0);
    if (t % 40 == 0) s.toggleButton(1);
}

static void runOnce(Game* g, const Theme* th, uint32_t seed, int ticks, Color* outFinal, bool* drew) {
    host::GameRunner r(g, th);
    host::InputSampler s;
    r.init(seed);
    Color bg = th->c(console::ROLE_BG);
    bool d = false;
    for (int t = 0; t < ticks; ++t) {
        scriptInput(s, t);
        Input in = s.sample();
        r.tick(in, TICK_MS);
        const Color* f = r.frame();
        for (int i = 0; i < SCREEN_PX; ++i)
            if (f[i].r != bg.r || f[i].g != bg.g || f[i].b != bg.b) { d = true; break; }
    }
    memcpy(outFinal, r.frame(), sizeof(Color) * SCREEN_PX);
    *drew = d;
}

static int runSelftest(Game* g, const Theme* th) {
    const int TICKS = 300;
    Color a[SCREEN_PX], b[SCREEN_PX];
    bool  drewA = false, drewB = false;
    runOnce(g, th, 0x1234u, TICKS, a, &drewA);
    runOnce(g, th, 0x1234u, TICKS, b, &drewB);

    int fails = 0;
    check(memcmp(a, b, sizeof(a)) == 0, "runner deterministic for a fixed seed", fails);
    check(drewA, "game draws a non-background pixel", fails);

    // exercise the SDK 3x5 bitmap font on a scratch canvas
    Color fb[SCREEN_PX] = {};
    console::Canvas fc(fb, SCREEN_W, SCREEN_H);
    sdk::drawText(fc, 0, 0, "8", console::WHITE);
    check(fc.get(0, 0).r > 0, "sdk 3x5 font renders", fails);

    printf(fails ? "GAME SELFTEST FAILED (%d)\n" : "GAME SELFTEST OK\n", fails);
    return fails ? 1 : 0;
}

// ---------------- interactive terminal ----------------

static void drawTerminal(host::GameRunner& r, const Theme& th, const Input& in) {
    const Color* f = r.frame();
    printf("\x1b[H");
    for (int y = 0; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            const Color& c = f[y * SCREEN_W + x];
            printf("\x1b[48;2;%d;%d;%dm  ", c.r, c.g, c.b);
        }
        printf("\x1b[0m  ");
        switch (y) {
            case 0: printf("game:  %-16s", r.game()->meta().name); break;
            case 1: printf("theme: %-16s", th.name); break;
            case 3: printf("L knob %5d  d%+d   ", in.knob[0].pos, (int)in.knob[0].delta); break;
            case 4: printf("R knob %5d  d%+d   ", in.knob[1].pos, (int)in.knob[1].delta); break;
            case 5: printf("L btn %-4s   R btn %-4s ",
                           in.knob[0].down ? "DOWN" : "up", in.knob[1].down ? "DOWN" : "up"); break;
            case 7: printf("a/d L knob   j/l R knob  "); break;
            case 8: printf("s L btn   k R btn        "); break;
            case 9: printf("t theme   r reset   q quit"); break;
            default: printf("%-26s", "");
        }
    }
    printf("\x1b[0m");
    fflush(stdout);
}

static int runInteractive(Game* game, const Theme* themes, int themeCount, uint32_t seed) {
    int themeIdx = 0;
    host::GameRunner r(game, &themes[themeIdx]);
    bool tty = isatty(0) != 0;
    if (tty) {
        tcgetattr(0, &g_origTio);
        struct termios tio = g_origTio;
        tio.c_lflag &= ~(ICANON | ECHO);
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &tio);
        g_ttyRaw = true;
    }
    atexit(restoreTty);
    printf("\x1b[?25l\x1b[2J");

    r.init(seed);
    host::InputSampler sampler;
    bool quit = false;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!quit) {
        char buf[64];
        ssize_t n = read(0, buf, sizeof(buf));
        if (n == 0 && !tty) break;   // piped stdin closed -> exit (never hangs in CI)
        for (ssize_t i = 0; i < n; ++i) {
            switch (buf[i]) {
                case 'a': sampler.rotate(0, -1); break;
                case 'd': sampler.rotate(0, +1); break;
                case 's': sampler.toggleButton(0); break;
                case 'j': sampler.rotate(1, -1); break;
                case 'l': sampler.rotate(1, +1); break;
                case 'k': sampler.toggleButton(1); break;
                case 't': themeIdx = (themeIdx + 1) % themeCount; r.setTheme(&themes[themeIdx]); break;
                case 'r': r.init(seed); break;
                case 'q': quit = true; break;
            }
        }

        // fixed 50 Hz with catch-up cap (same scheme as the pong sim)
        next.tv_nsec += TICK_MS * 1000000L;
        if (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long behindMs = (now.tv_sec - next.tv_sec) * 1000 + (now.tv_nsec - next.tv_nsec) / 1000000;
        int steps = 1 + (int)(behindMs / (long)TICK_MS);
        if (steps > MAX_CATCHUP_STEPS) { steps = MAX_CATCHUP_STEPS; next = now; }
        Input shown{};
        for (int sIdx = 0; sIdx < steps; ++sIdx) {
            Input stepIn = sampler.sample();   // first step carries the key deltas
            r.tick(stepIn, TICK_MS);
            if (sIdx == 0) shown = stepIn;
        }
        drawTerminal(r, themes[themeIdx], shown);

        clock_gettime(CLOCK_MONOTONIC, &now);
        long remNs = (next.tv_sec - now.tv_sec) * 1000000000L + (next.tv_nsec - now.tv_nsec);
        if (remNs > 0) {
            struct timespec ts = {remNs / 1000000000L, remNs % 1000000000L};
            nanosleep(&ts, nullptr);
        }
    }
    return 0;
}

// ---------------- entry ----------------

int main(int argc, char** argv) {
    int themeCount = 0;
    const Theme* themes = host::stubThemes(themeCount);
    Game* game = host::createGame();

    const char* mode = argc > 1 ? argv[1] : "";
    if (strcmp(mode, "--dump") == 0) {
        int frames = argc > 2 ? atoi(argv[2]) : 150;
        int stride = argc > 3 ? atoi(argv[3]) : 2;
        if (frames < 1) frames = 1;
        if (stride < 1) stride = 1;
        return runDump(game, &themes[0], frames, stride, 0x1234u);
    }
    if (strcmp(mode, "--selftest") == 0) return runSelftest(game, &themes[0]);
    if (strcmp(mode, "--help") == 0 || strcmp(mode, "-h") == 0) {
        printf("usage: %s [--dump [frames [stride]] | --selftest | --help]\n", argv[0]);
        printf("no args: interactive terminal (a/d,j/l knobs; s,k buttons; t theme; r reset; q quit)\n");
        return 0;
    }
    uint32_t seed = isatty(0) ? (uint32_t)time(nullptr) : 0x1234u;
    return runInteractive(game, themes, themeCount, seed);
}
