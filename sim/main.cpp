// Desktop terminal simulator for the pong engine. POSIX only, no deps.
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include "pong_engine.h"
#include "pong_config.h"
#include "net_config.h"

using pong::Engine;
using pong::EngineInputs;
using pong::EngineStatus;
using pong::Frame;

static const char* STATE_NAMES[] = {
    "LINK_WAIT", "READY_CHECK", "COUNTDOWN", "PLAYING", "POINT_FLASH", "GAME_OVER"};

static uint8_t d8(uint8_t n, uint8_t o) { return (uint8_t)(n - o); }

// ---------------- cue log (shared by selftest + interactive) ----------------

static char logLines[6][64];
static int logCount = 0;
static int rally = 0;

static void logCue(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void logCue(const char* fmt, ...) {
    memmove(logLines[0], logLines[1], sizeof(logLines) - sizeof(logLines[0]));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(logLines[5], sizeof(logLines[5]), fmt, ap);
    va_end(ap);
    if (logCount < 6) logCount++;
}

static void pollCues(const EngineStatus& st, EngineStatus& prev, bool print) {
    if (d8(st.serveSeq, prev.serveSeq)) {
        rally = 0;
        logCue("CUE: serve by P%d", st.servingPlayer + 1);
    }
    if (d8(st.paddleHitSeq, prev.paddleHitSeq)) {
        rally += d8(st.paddleHitSeq, prev.paddleHitSeq);
        logCue("CUE: paddleHit rally=%d", rally);
    }
    if (d8(st.wallBounceSeq, prev.wallBounceSeq)) logCue("CUE: wallBounce");
    if (d8(st.goalSeq, prev.goalSeq)) logCue("CUE: goal by P%d", st.goalBy + 1);
    if (d8(st.winSeq, prev.winSeq)) logCue("CUE: win P%d", st.winner + 1);
    if (print && logCount) {
        puts(logLines[5]);
        logCount = 0;
    }
    prev = st;
}

// ---------------- selftest (CI gate, no TTY) ----------------

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static int findBallX(Engine& e) {
    Frame f;
    e.render(f);
    for (int y = 1; y < pong::H - 1; ++y)
        for (int x = 0; x < pong::W; ++x) {
            pong::Color c = f.at(x, y);
            if (c.r == 255 && c.g == 255 && c.b == 255) return x;
        }
    return -1;
}

static int selftest() {
    Engine e;
    e.reset(1);
    EngineInputs in = {};
    in.controllerLinked = true;
    EngineStatus prev = e.status();

    e.tick(in, 20);
    check(e.status().state == GS_READY_CHECK, "first linked input -> READY_CHECK");

    in.held[0] = in.held[1] = true;
    for (int i = 0; i < 30; ++i) e.tick(in, 20);
    check(e.status().state == GS_COUNTDOWN, "both-hold 600ms -> COUNTDOWN");

    in.held[0] = in.held[1] = false;
    for (int i = 0; i < 80 && e.status().state != GS_PLAYING; ++i) e.tick(in, 20);
    check(e.status().state == GS_PLAYING, "countdown elapses -> PLAYING");
    check(d8(e.status().serveSeq, prev.serveSeq) == 1, "serveSeq advanced once");

    // dodge the ball with both paddles until someone reaches PONG_WIN_SCORE
    int32_t knob[2] = {0, 0};
    int goals = 0, serves = 1, ticks = 0;
    prev = e.status();
    while (e.status().state != GS_GAME_OVER && ticks < 60000) {
        EngineStatus st = e.status();
        in.held[0] = in.held[1] = (st.state == GS_READY_CHECK);
        if (st.state == GS_PLAYING) {
            int bx = findBallX(e);
            for (int p = 0; p < 2; ++p) {
                int32_t target = (bx >= 0 && bx <= 2) ? 8 : 0;  // pad cells 4 or 0
                if (knob[p] < target) knob[p] += 2;
                else if (knob[p] > target) knob[p] -= 2;
            }
        }
        in.knobPos[0] = knob[0];
        in.knobPos[1] = knob[1];
        e.tick(in, 20);
        EngineStatus now = e.status();
        goals += d8(now.goalSeq, prev.goalSeq);
        serves += d8(now.serveSeq, prev.serveSeq);
        prev = now;
        ticks++;
    }
    EngineStatus st = e.status();
    check(st.state == GS_GAME_OVER, "match reaches GAME_OVER");
    check(st.winSeq == 1, "winSeq == 1");
    check(st.winner == 0 || st.winner == 1, "winner set");
    check(st.score[st.winner] == PONG_WIN_SCORE, "winner score == PONG_WIN_SCORE");
    check(goals == st.score[0] + st.score[1], "goalSeq deltas match score sum");
    check(serves == goals, "one serve per goal");

    // rematch: both-hold after the celebration -> straight to COUNTDOWN, 0-0
    in.held[0] = in.held[1] = true;
    for (int i = 0; i < 300 && e.status().state != GS_COUNTDOWN; ++i) e.tick(in, 20);
    st = e.status();
    check(st.state == GS_COUNTDOWN, "rematch both-hold -> COUNTDOWN");
    check(st.score[0] == 0 && st.score[1] == 0, "rematch resets scores to 0-0");

    // link drop mid-countdown -> LINK_WAIT, relink -> READY_CHECK
    in.controllerLinked = false;
    e.tick(in, 20);
    check(e.status().state == GS_LINK_WAIT, "link drop -> LINK_WAIT");
    in.controllerLinked = true;
    in.held[0] = in.held[1] = false;
    e.tick(in, 20);
    check(e.status().state == GS_READY_CHECK, "relink -> READY_CHECK");

    printf(failures ? "SELFTEST FAILED (%d)\n" : "SELFTEST OK\n", failures);
    return failures ? 1 : 0;
}

// ---------------- interactive ----------------

static struct termios origTio;
static void restoreTty() {
    tcsetattr(0, TCSANOW, &origTio);
    printf("\x1b[?25h\x1b[0m\n");
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) return selftest();

    tcgetattr(0, &origTio);
    struct termios tio = origTio;
    tio.c_lflag &= ~(ICANON | ECHO);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &tio);
    atexit(restoreTty);
    printf("\x1b[?25l\x1b[2J");

    Engine e;
    e.reset((uint32_t)time(nullptr));
    EngineInputs in = {};
    in.controllerLinked = true;
    EngineStatus prev = e.status();
    int32_t knob[2] = {0, 0};
    bool held[2] = {false, false};
    bool quit = false;

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!quit) {
        char buf[32];
        ssize_t n = read(0, buf, sizeof(buf));
        for (ssize_t i = 0; i < n; ++i) {
            switch (buf[i]) {
                case 'a': knob[0] -= 2; break;
                case 'z': knob[0] += 2; break;
                case 'k': knob[1] -= 2; break;
                case 'm': knob[1] += 2; break;
                case 's': held[0] = !held[0]; break;
                case 'l': held[1] = !held[1]; break;
                case 't': in.controllerLinked = !in.controllerLinked; break;
                case 'r': e.reset((uint32_t)time(nullptr)); prev = e.status(); break;
                case 'q': quit = true; break;
            }
        }
        in.knobPos[0] = knob[0];
        in.knobPos[1] = knob[1];
        in.held[0] = held[0];
        in.held[1] = held[1];

        // fixed 50 Hz with catch-up cap
        next.tv_nsec += TICK_MS * 1000000L;
        if (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long behindMs = (now.tv_sec - next.tv_sec) * 1000 + (now.tv_nsec - next.tv_nsec) / 1000000;
        int steps = 1 + (int)(behindMs / (long)TICK_MS);
        if (steps > 4) { steps = 4; next = now; }
        for (int sIdx = 0; sIdx < steps; ++sIdx) {
            e.tick(in, TICK_MS);
            pollCues(e.status(), prev, false);
        }

        EngineStatus st = e.status();
        Frame f;
        e.render(f);
        printf("\x1b[H");
        for (int y = 0; y < pong::H; ++y) {
            for (int x = 0; x < pong::W; ++x) {
                pong::Color c = f.at(x, y);
                printf("\x1b[48;2;%d;%d;%dm  ", c.r, c.g, c.b);
            }
            printf("\x1b[0m  ");
            switch (y) {
                case 0: printf("state: %-12s", STATE_NAMES[st.state < PONG_STATE_COUNT ? st.state : 0]); break;
                case 1: printf("score: P1 %d - %d P2   ", st.score[0], st.score[1]); break;
                case 2: printf("hold:  P1 %3d%% %s P2 %3d%% %s   ",
                               st.readyProgress[0] * 100 / 255, held[0] ? "[HOLD]" : "      ",
                               st.readyProgress[1] * 100 / 255, held[1] ? "[HOLD]" : "      "); break;
                case 3: printf("rally: %-3d serve:P%d link:%s ", rally,
                               st.servingPlayer == PONG_NOBODY ? 0 : st.servingPlayer + 1,
                               in.controllerLinked ? "up  " : "DOWN"); break;
                case 4: printf("speed~ %4.1f cells/s      ",
                               (double)fminf(BALL_SPEED_START * powf(SPEEDUP_PER_HIT, (float)rally),
                                             BALL_SPEED_MAX)); break;
                case 6: printf("a/z k/m knobs  s/l hold  "); break;
                case 7: printf("t link  r reset  q quit  "); break;
                default:
                    if (y >= 10 && y < 16) printf("%-40s", logLines[y - 10]);
                    else printf("%40s", "");
            }
        }
        printf("\x1b[0m");
        fflush(stdout);

        clock_gettime(CLOCK_MONOTONIC, &now);
        long remNs = (next.tv_sec - now.tv_sec) * 1000000000L + (next.tv_nsec - now.tv_nsec);
        if (remNs > 0) {
            struct timespec ts = {remNs / 1000000000L, remNs % 1000000000L};
            nanosleep(&ts, nullptr);
        }
    }
    return 0;
}
