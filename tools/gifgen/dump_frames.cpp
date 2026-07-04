// Runs the real pong engine through a scripted, watchable match and dumps the
// 6x18 framebuffer as text for tools/gifgen/render_matrix.py.
//
// Build:  g++ -std=c++14 -O2 -I modules/pong_screen tools/gifgen/dump_frames.cpp \
//             modules/pong_screen/pong_engine.cpp -o /tmp/dump_frames
// Run:    /tmp/dump_frames [stride]   (stride = dump every Nth 20 ms tick, default 2)
//
// stdout: per dumped frame, a line "F" then 18 rows of 6 hex RRGGBB tokens.
// stderr: "MARK <frameIndex> <label>" lines for preview stills, plus a summary.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "pong_engine.h"
#include "pong_config.h"
#include "net_config.h"

using namespace pong;

namespace {

constexpr uint32_t SEED = 0x50464C31u;
constexpr int MAX_TICKS = 6000;
constexpr double MAX_DETENTS_PER_TICK = 1.5;

// scripted outcome: scorers P1,P2,P1,P1 -> 3-1; QUOTA = paddle contacts
// in the rally before the designated loser's tracking is allowed to freeze
constexpr int NUM_POINTS = 4;
constexpr int SCORER[NUM_POINTS] = {0, 1, 0, 0};
constexpr int QUOTA[NUM_POINTS] = {2, 2, 2, 3};

constexpr int INTRO_PULSE_TICKS = 50;
constexpr int BETWEEN_PAUSE_TICKS = 25;
constexpr int GAME_OVER_TICKS = 125;

uint8_t d8(uint8_t n, uint8_t o) { return (uint8_t)(n - o); }
double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool isColor(const Color& c, const Color& ref) {
    return c.r == ref.r && c.g == ref.g && c.b == ref.b;
}

// ball = the only pure-white pixel in the playfield interior
bool findBall(const Frame& f, int& bx, int& by) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (isColor(f.at(x, y), COL_BALL)) { bx = x; by = y; return true; }
    return false;
}

// paddle left edge from its own row (full-brightness player color during play)
int findPaddleLeft(const Frame& f, int p) {
    int row = p == 0 ? H - 1 : 0;
    const Color& ref = p == 0 ? COL_P1 : COL_P2;
    for (int x = 0; x < W; ++x)
        if (isColor(f.at(x, row), ref)) return x;
    return -1;
}

// mirror the engine's side-wall reflection (ball center lives in [0.5, W-0.5])
double foldLanding(double x) {
    const double period = 2.0 * (W - 1.0);
    double u = fmod(x - 0.5, period);
    if (u < 0) u += period;
    if (u > W - 1.0) u = period - u;
    return u + 0.5;
}

// scanned ball positions (cell centers), most recent last
struct BallTrack {
    static constexpr int CAP = 12;
    double x[CAP], y[CAP];
    int n = 0;

    void reset() { n = 0; }

    void push(double px, double py) {
        if (n == CAP) {
            for (int i = 1; i < CAP; ++i) { x[i - 1] = x[i]; y[i - 1] = y[i]; }
            n--;
        }
        x[n] = px;
        y[n] = py;
        n++;
    }

    // per-tick velocity; x window shrinks to the span since the last wall bounce
    bool velocity(double& vx, double& vy) const {
        if (n < 5) return false;
        int ky = n - 1 > 8 ? 8 : n - 1;
        vy = (y[n - 1] - y[n - 1 - ky]) / ky;
        int kx = 0, dir = 0;
        for (int j = n - 1; j > 0 && kx < 8; --j) {
            double d = x[j] - x[j - 1];
            int s = d > 0 ? 1 : (d < 0 ? -1 : 0);
            if (s != 0) {
                if (dir != 0 && s != dir) break;
                dir = s;
            }
            kx++;
        }
        if (kx < 2) return false;
        vx = (x[n - 1] - x[n - 1 - kx]) / kx;
        return vy != 0;
    }

    // extrapolate to the loser's contact plane; falls back to current x
    double landing(double plane) const {
        double vx, vy;
        double bx = x[n - 1], by = y[n - 1];
        if (!velocity(vx, vy)) return bx;
        double ticks = (plane - by) / vy;
        if (ticks <= 0 || ticks > 500) return bx;
        return foldLanding(bx + vx * ticks);
    }
};

void dumpFrame(const Frame& f) {
    puts("F");
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Color& c = f.at(x, y);
            printf("%02X%02X%02X%c", c.r, c.g, c.b, x == W - 1 ? '\n' : ' ');
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    int stride = argc > 1 ? atoi(argv[1]) : 2;
    if (stride < 1) stride = 1;

    Engine eng;
    eng.reset(SEED);
    EngineInputs in = {};
    in.controllerLinked = true;

    double knobF[2] = {0, 0};
    double cdBase[2] = {0, 0};
    bool holdBoth = false;
    bool firstReady = true;
    int pt = 0, rallyHits = 0, ticksSinceHit = 999;
    bool frozen = false;
    double frozenCenter = 0;
    int missSide = 0, fleeSign = 0;
    BallTrack track;
    int dirY = 0, lastBallY = -1;
    int stateTicks = 0;
    int dumped = 0;
    int rallyOfPoint[NUM_POINTS] = {0, 0, 0, 0};
    bool marked[5] = {false, false, false, false, false};

    EngineStatus st = eng.status();
    int t = 0;

    for (; t < MAX_TICKS; ++t) {
        Frame f;
        eng.render(f);

        if (t % stride == 0) {
            dumpFrame(f);
            dumped++;
            int idx = dumped - 1;
            if (!marked[0] && st.state == GS_READY_CHECK && firstReady && stateTicks >= 24) {
                marked[0] = true;
                fprintf(stderr, "MARK %d ready_pulse\n", idx);
            }
            if (!marked[1] && st.state == GS_COUNTDOWN && pt >= 1 &&
                stateTicks >= 38 && stateTicks <= 43) {
                marked[1] = true;
                fprintf(stderr, "MARK %d countdown\n", idx);
            }
            if (!marked[2] && st.state == GS_PLAYING && pt >= 1 && rallyHits >= 1) {
                int bx, by;
                if (findBall(f, bx, by) && by >= 7 && by <= 10) {
                    marked[2] = true;
                    fprintf(stderr, "MARK %d mid_rally\n", idx);
                }
            }
            if (!marked[3] && st.state == GS_POINT_FLASH && st.goalBy == 1 && stateTicks <= 5) {
                marked[3] = true;
                fprintf(stderr, "MARK %d goal_flash\n", idx);
            }
            if (!marked[4] && st.state == GS_GAME_OVER && stateTicks >= 50) {
                marked[4] = true;
                fprintf(stderr, "MARK %d win_sweep\n", idx);
            }
        }

        if (st.state == GS_GAME_OVER && stateTicks >= GAME_OVER_TICKS) break;

        // ---- scripted controller ----
        switch (st.state) {
            case GS_READY_CHECK:
                if (stateTicks >= (firstReady ? INTRO_PULSE_TICKS : BETWEEN_PAUSE_TICKS))
                    holdBoth = true;
                break;
            case GS_COUNTDOWN:
                firstReady = false;
                holdBoth = false;
                if (stateTicks == 0) { cdBase[0] = knobF[0]; cdBase[1] = knobF[1]; }
                // gentle sway; the glued ball rides the serving paddle
                for (int p = 0; p < 2; ++p)
                    knobF[p] = cdBase[p] + 0.9 * sin(0.004 * (t * TICK_MS) + p * 2.6);
                break;
            case GS_PLAYING: {
                int bx = -1, by = -1;
                bool haveBall = findBall(f, bx, by);
                if (haveBall) {
                    track.push(bx + 0.5, by + 0.5);
                    if (lastBallY >= 0 && by != lastBallY) dirY = by > lastBallY ? 1 : -1;
                    lastBallY = by;
                }
                ticksSinceHit++;
                int defender = dirY > 0 ? 0 : 1;
                int loser = pt < NUM_POINTS ? 1 - SCORER[pt] : -1;
                double plane = loser == 0 ? H - PADDLE_PLANE_INSET : PADDLE_PLANE_INSET;

                if (!frozen && haveBall && loser >= 0 && rallyHits >= QUOTA[pt] &&
                    ticksSinceHit > 10) {
                    bool toward = (loser == 0) == (dirY > 0);
                    bool near = loser == 1 ? by <= 4 : by >= H - 5;
                    if (toward && near) {
                        int pl = findPaddleLeft(f, loser);
                        frozen = true;
                        frozenCenter = (pl < 0 ? 2 : pl) + PADDLE_W * 0.5;
                        missSide = 0;
                        fleeSign = 0;
                        fprintf(stderr, "EVENT t=%d freeze P%d\n", t, loser + 1);
                    }
                }
                for (int p = 0; p < 2; ++p) {
                    double target;
                    if (p == defender && haveBall)
                        target = bx + 0.5 + 0.3 * sin(0.004 * (t * TICK_MS) + p * 2.1);
                    else
                        target = W * 0.5 + 0.35 * sin(0.0025 * (t * TICK_MS) + p * 1.7);
                    int pl = findPaddleLeft(f, p);
                    double center = (pl < 0 ? 2 : pl) + PADDLE_W * 0.5;
                    if (frozen && p == loser) {
                        // hold position if the predicted landing already misses,
                        // else drift off to one side of it (picked once, no flip-flop)
                        double land = track.landing(plane);
                        if (missSide == 0 && fabs(land - frozenCenter) < 1.9)
                            missSide = center <= land ? -1 : 1;
                        if (missSide == 0) {
                            target = frozenCenter;
                        } else {
                            target = clampd(land + missSide * 2.8, 1.0, W - 1.0);
                            if (fabs(target - land) < 2.0) {
                                missSide = -missSide;
                                target = clampd(land + missSide * 2.8, 1.0, W - 1.0);
                            }
                        }
                        // last resort: ball nearly at the plane and still reachable
                        bool close = haveBall && (loser == 1 ? by <= 2 : by >= H - 3);
                        if (close && fabs(bx + 0.5 - center) < 1.9) {
                            if (fleeSign == 0) fleeSign = bx + 0.5 <= center ? 1 : -1;
                            target = clampd(center + fleeSign * 2.0, 1.0, W - 1.0);
                        }
                    }
                    double err = target - center;
                    if (fabs(err) > 0.45)
                        knobF[p] += clampd(err * DETENTS_PER_CELL * 0.9,
                                           -MAX_DETENTS_PER_TICK, MAX_DETENTS_PER_TICK);
                }
                break;
            }
            default:
                holdBoth = false;
                break;
        }

        in.held[0] = in.held[1] = holdBoth;
        in.knobPos[0] = (int32_t)llround(KNOB_SIGN[0] * knobF[0]);
        in.knobPos[1] = (int32_t)llround(KNOB_SIGN[1] * knobF[1]);
        eng.tick(in, TICK_MS);

        EngineStatus now = eng.status();
        if (d8(now.serveSeq, st.serveSeq)) {
            rallyHits = 0;
            ticksSinceHit = 999;
            frozen = false;
            track.reset();
            lastBallY = -1;
            dirY = now.servingPlayer == 0 ? -1 : 1;
            fprintf(stderr, "EVENT t=%d serve P%d\n", t, now.servingPlayer + 1);
        }
        if (d8(now.paddleHitSeq, st.paddleHitSeq)) {
            rallyHits += d8(now.paddleHitSeq, st.paddleHitSeq);
            ticksSinceHit = 0;
            track.reset();
            if (frozen) {
                frozen = false;
                fprintf(stderr, "EVENT t=%d missed-miss, retrying\n", t);
            }
        }
        if (d8(now.goalSeq, st.goalSeq)) {
            fprintf(stderr, "EVENT t=%d goal by P%d (rally %d hits) score %d-%d\n",
                    t, now.goalBy + 1, rallyHits, now.score[0], now.score[1]);
            if (pt < NUM_POINTS) {
                rallyOfPoint[pt] = rallyHits;
                if (now.goalBy != SCORER[pt])
                    fprintf(stderr, "WARN unexpected scorer at point %d\n", pt + 1);
            }
            pt++;
            frozen = false;
        }
        if (d8(now.winSeq, st.winSeq))
            fprintf(stderr, "EVENT t=%d win P%d\n", t, now.winner + 1);

        if (now.state != st.state) stateTicks = 0;
        else stateTicks++;
        st = now;
    }

    fprintf(stderr, "SUMMARY ticks=%d frames=%d dur=%.1fs score=%d-%d rallies=%d,%d,%d,%d\n",
            t, dumped, dumped * stride * TICK_MS / 1000.0,
            st.score[0], st.score[1],
            rallyOfPoint[0], rallyOfPoint[1], rallyOfPoint[2], rallyOfPoint[3]);
    return 0;
}
