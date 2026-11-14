// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Low latency with V-sync (frame_pacer.hpp), on a simulated screen: frames
// start just before their refresh, nothing is delayed when V-sync does not
// wait, and a missed refresh makes the game start earlier.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "frame_pacer.hpp"

using namespace gh;

extern int g_fail, g_pass;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) ++g_pass;                                                      \
        else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

namespace {

struct SimResult {
    double latency = 0;  // average from a frame's start (keys read) to the screen showing it
    int missed = 0;      // frames shown a refresh later than they could have been
    int frames = 0;
};

// The game loop against a screen refreshing every `period` seconds. With
// vsync the swap returns at the next refresh, else at once. `t`: the time,
// kept from one call to the next.
SimResult simulate(FramePacer& p, double period, bool vsync, int frames, const std::function<double(int)>& work,
                   int skip, double& t) {
    SimResult r;
    for (int f = 0; f < frames; ++f) {
        const double start = std::max(t, p.startAt());
        const double ready = start + work(f);
        const double shown = vsync ? std::ceil(ready / period - 1e-9) * period : ready;
        p.shown(shown, ready - start);
        if (f >= skip) {
            r.latency += shown - start;
            ++r.frames;
            // Starting at once after the last refresh, a frame lighter than a
            // refresh is always shown at the next one: later means missed.
            if (vsync && shown - t > period * 1.5 && work(f) < period * 0.9) ++r.missed;
        }
        t = shown;
    }
    if (r.frames) r.latency /= r.frames;
    return r;
}

void testPacesWithVsync() {
    const double period = 1.0 / 60.0;
    FramePacer p;
    p.reset(60.0);
    double t = 0;
    const SimResult r = simulate(p, period, true, 600, [](int) { return 0.003; }, 120, t);
    CHECK(p.pacing());
    CHECK(r.missed == 0);
    // A frame starting right after the last refresh would wait ~16.7 ms to
    // show; paced, it starts ~3 ms (its work) and a small margin before.
    CHECK(r.latency < 0.008);
    CHECK(r.latency > 0.003);
    std::printf("  frame pacing at 60 Hz, 3 ms frames: %.1f ms from the keys to the screen (16.7 unpaced)\n",
                r.latency * 1000.0);
}

void testNoDelayWithoutVsync() {
    FramePacer p;
    p.reset(60.0);
    double t = 0;
    const SimResult r = simulate(p, 1.0 / 60.0, false, 300, [](int) { return 0.002; }, 120, t);
    CHECK(!p.pacing());
    CHECK(p.startAt() == 0.0);
    CHECK(r.latency < 0.0025);
}

void testHeavyFramesAreNotDelayed() {
    // Frames heavier than a refresh: every one waits a refresh more; no delay.
    FramePacer p;
    p.reset(60.0);
    double t = 0;
    simulate(p, 1.0 / 60.0, true, 300, [](int) { return 0.025; }, 120, t);
    CHECK(!p.pacing());
}

void testMissesMakeItStartEarlier() {
    // Every 50th frame takes 9 ms more than the others: the pacer misses a
    // refresh with some of them, starts earlier, and misses no more.
    FramePacer p;
    p.reset(60.0);
    auto work = [](int f) { return f % 50 == 49 ? 0.011 : 0.002; };
    const double m0 = p.margin();
    double t = 0;
    simulate(p, 1.0 / 60.0, true, 400, work, 0, t);
    CHECK(p.pacing());
    const SimResult later = simulate(p, 1.0 / 60.0, true, 400, work, 0, t);
    CHECK(later.missed == 0);
    CHECK(p.margin() >= m0);
    CHECK(p.margin() <= 1.0 / 60.0 * 0.5);
}

void testUnknownRefreshRate() {
    // The refresh rate is not known (0): it is measured from the frames.
    FramePacer p;
    p.reset(0.0);
    double t = 0;
    const SimResult r = simulate(p, 1.0 / 144.0, true, 600, [](int) { return 0.0015; }, 120, t);
    CHECK(std::fabs(p.period() - 1.0 / 144.0) < 0.0002);
    CHECK(p.pacing());
    CHECK(r.missed == 0);
    CHECK(r.latency < 1.0 / 144.0);
}

}  // namespace

void runPacerTests() {
    testPacesWithVsync();
    testNoDelayWithoutVsync();
    testHeavyFramesAreNotDelayed();
    testMissesMakeItStartEarlier();
    testUnknownRefreshRate();
}
