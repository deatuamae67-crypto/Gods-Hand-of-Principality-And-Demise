// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace gh {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 44100, kLowHz = 40, kHighHz = 16000;
// A bar is empty at -54 dB (of a full-scale sine) and full at -4 dB.
constexpr float kFloorDb = -54, kTopDb = -4;
}  // namespace

Spectrum::Spectrum(int bars, int fftSize) {
    int n = 2, bits = 1;
    while (n < fftSize) {
        n <<= 1;
        ++bits;
    }
    n_ = n;
    window_.resize(size_t(n));
    re_.resize(size_t(n));
    im_.resize(size_t(n));
    rev_.resize(size_t(n));
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        window_[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1)));  // Hann
        sum += window_[size_t(i)];
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev_[size_t(i)] = r;
    }
    norm_ = float(2.0 / sum);  // a full-scale sine reads 1
    cos_.resize(size_t(n / 2));
    sin_.resize(size_t(n / 2));
    for (int i = 0; i < n / 2; ++i) {
        cos_[size_t(i)] = float(std::cos(2 * kPi * i / n));
        sin_[size_t(i)] = float(-std::sin(2 * kPi * i / n));
    }
    bars = std::max(1, bars);
    lo_.resize(size_t(bars));
    hi_.resize(size_t(bars));
    tilt_.resize(size_t(bars));
    for (int b = 0; b < bars; ++b) {
        const double f0 = kLowHz * std::pow(kHighHz / kLowHz, double(b) / bars);
        const double f1 = kLowHz * std::pow(kHighHz / kLowHz, double(b + 1) / bars);
        const int lo = std::clamp(int(std::floor(f0 * n / kRate)), 1, n / 2 - 1);
        lo_[size_t(b)] = lo;
        hi_[size_t(b)] = std::clamp(int(std::ceil(f1 * n / kRate)), lo + 1, n / 2);
        // Music has less in its high notes than in its low ones: 4 dB more an
        // octave over 600 Hz (and less under it), so that every bar moves.
        tilt_[size_t(b)] = float(std::clamp(4.0 * std::log2(std::sqrt(f0 * f1) / 600.0), -6.0, 12.0));
    }
    target_.assign(size_t(bars), 0.0f);
    level_.assign(size_t(bars), 0.0f);
    peak_.assign(size_t(bars), 0.0f);
    hold_.assign(size_t(bars), 0.0f);
}

float Spectrum::centreHz(int bar) const {
    const double B = double(bars());
    return float(kLowHz * std::pow(kHighHz / kLowHz, (bar + 0.5) / B));
}

void Spectrum::analyse(const float* s, double dt) {
    const int n = n_;
    for (int i = 0; i < n; ++i) {
        const int j = rev_[size_t(i)];
        re_[size_t(j)] = s[i] * window_[size_t(i)];
        im_[size_t(j)] = 0;
    }
    // Radix-2 FFT, in place (the input went in bit-reversed order).
    float* re = re_.data();
    float* im = im_.data();
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1, step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < half; ++k) {
                const float wr = cos_[size_t(k * step)], wi = sin_[size_t(k * step)];
                const int a = i + k, b = a + half;
                const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
    const int B = bars();
    double all = 0, low = 0;
    const int lowBars = std::max(1, B / 6);
    for (int b = 0; b < B; ++b) {
        float best = 0;  // the loudest frequency of the bar: a note shows as a whole bar
        for (int k = lo_[size_t(b)]; k < hi_[size_t(b)]; ++k) best = std::max(best, re[k] * re[k] + im[k] * im[k]);
        const float db = 20.0f * std::log10(std::sqrt(best) * norm_ + 1e-9f) + tilt_[size_t(b)];
        const float v = std::clamp((db - kFloorDb) / (kTopDb - kFloorDb), 0.0f, 1.0f);
        target_[size_t(b)] = v;
        all += v;
        if (b < lowBars) low += v;
    }
    energy_ = float(all / B);
    bass_ = float(low / lowBars);
    settle(dt);
}

void Spectrum::decay(double dt) {
    std::fill(target_.begin(), target_.end(), 0.0f);
    energy_ = bass_ = 0;
    settle(dt);
}

void Spectrum::settle(double dt) {
    const float rise = float(std::min(1.0, dt * 40.0)), fall = float(dt * 1.8), capFall = float(dt * 0.8);
    for (size_t b = 0; b < level_.size(); ++b) {
        float& l = level_[b];
        const float t = target_[b];
        if (t > l) l += (t - l) * rise;          // up in about 25 ms
        else l = std::max(t, l - fall);          // down in about half a second
        if (l >= peak_[b]) {
            peak_[b] = l;
            hold_[b] = 0.3f;                     // the cap hangs a moment, then drops
        } else if (hold_[b] > 0) {
            hold_[b] -= float(dt);
        } else {
            peak_[b] = std::max(l, peak_[b] - capFall);
        }
    }
}

}  // namespace gh
