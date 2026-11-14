// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Low latency with V-sync (Options > GRAPHICS > Low latency). With V-sync
// the driver lets the game draw up to a few frames ahead of the screen, and
// each of them adds a refresh of delay between a key and what it shows.
// Here the game waits until each frame is on the screen (none are queued)
// and starts the next one as late as it safely can, reading the keys about
// every millisecond meanwhile: what a key does shows in the next refresh,
// and every press keeps a precise time. If V-sync does not really wait for
// the screen (some drivers, a window that is not shown), nothing is delayed.
#pragma once

namespace gh {

class FramePacer {
public:
    // refreshHz: the screen's refresh rate (0 when unknown: measured).
    void reset(double refreshHz);
    // A frame was shown at `t` (seconds: its swap returned and the graphics
    // card finished); `work`: the seconds it took, from its start to its swap.
    void shown(double t, double work);
    // When the next frame should start; at or before now: at once.
    double startAt() const;
    bool pacing() const { return synced_; }  // frames are being delayed
    double period() const { return period_; }
    double margin() const { return margin_; }
    double workEstimate() const;
    int misses() const { return misses_; }  // refreshes missed while pacing

private:
    // Frame times: the slowest of the last kWorks (about 2 seconds at
    // 60 Hz), so a heavier frame now and then is still ready in time.
    static constexpr int kWorks = 128, kInts = 32;
    double medianInterval() const;
    double hz_ = 0, period_ = 1.0 / 60.0, last_ = -1.0, margin_ = 0.002;
    double works_[kWorks] = {}, ints_[kInts] = {};
    int workN_ = 0, workPos_ = 0, intN_ = 0, intPos_ = 0;
    int calm_ = 0, misses_ = 0;
    bool synced_ = false;
};

}  // namespace gh
