// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "frame_pacer.hpp"

#include <algorithm>

namespace gh {

void FramePacer::reset(double refreshHz) {
    hz_ = refreshHz >= 24.0 && refreshHz <= 1000.0 ? refreshHz : 0.0;
    period_ = hz_ > 0 ? 1.0 / hz_ : 1.0 / 60.0;
    last_ = -1.0;
    margin_ = 0.002;
    workN_ = workPos_ = intN_ = intPos_ = 0;
    calm_ = misses_ = 0;
    synced_ = false;
}

double FramePacer::medianInterval() const {
    double v[kInts];
    std::copy(ints_, ints_ + intN_, v);
    std::nth_element(v, v + intN_ / 2, v + intN_);
    return v[intN_ / 2];
}

double FramePacer::workEstimate() const {
    double w = 0;
    for (int i = 0; i < workN_; ++i) w = std::max(w, works_[i]);
    return w;
}

void FramePacer::shown(double t, double work) {
    works_[workPos_] = std::max(0.0, work);
    workPos_ = (workPos_ + 1) % kWorks;
    workN_ = std::min(workN_ + 1, kWorks);
    if (last_ >= 0.0 && t > last_) {
        const double gap = t - last_;
        ints_[intPos_] = gap;
        intPos_ = (intPos_ + 1) % kInts;
        intN_ = std::min(intN_ + 1, kInts);
        if (intN_ >= 12) {
            const double med = medianInterval();
            // Unknown refresh rate: the gaps between shown frames tell it
            // (while nothing is delayed, a frame per refresh).
            if (hz_ <= 0.0 && !synced_ && med >= 1.0 / 500.0 && med <= 1.0 / 24.0) period_ = med;
            // V-sync really waits for the screen when frames come one refresh
            // apart; much faster (no waiting) or slower (too heavy to keep up)
            // and nothing is delayed.
            synced_ = med > period_ * 0.8 && med < period_ * 1.35;
        }
        if (synced_ && gap > period_ * 1.5) {
            // A refresh was missed: start earlier from now on.
            margin_ = std::min(margin_ + 0.001, period_ * 0.5);
            calm_ = 0;
            ++misses_;
        } else if (++calm_ >= 300) {
            margin_ = std::max(0.001, margin_ - 0.00025);  // and later again while it keeps up
            calm_ = 0;
        }
    }
    last_ = t;
}

double FramePacer::startAt() const {
    if (!synced_ || last_ < 0.0) return 0.0;
    return last_ + period_ - workEstimate() - margin_;
}

}  // namespace gh
