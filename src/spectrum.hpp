// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The bars of the audio visualizer (issue #24): a window of the song's sound
// goes through an FFT, its frequencies are gathered into bars spaced evenly in
// pitch, and each bar jumps up at once and falls back slowly.
#pragma once

#include <vector>

namespace gh {

class Spectrum {
public:
    // `fftSize` samples at 44.1 kHz (a power of two) make `bars` bars from
    // 40 Hz to 16 kHz.
    Spectrum(int bars, int fftSize);
    int fftSize() const { return n_; }
    int bars() const { return int(level_.size()); }

    // The newest fftSize() samples of the sound, mono, oldest first; dt is
    // the time since the last call.
    void analyse(const float* samples, double dt);
    // No new sound (paused, silent): the bars fall.
    void decay(double dt);

    const std::vector<float>& levels() const { return level_; }  // 0..1 each, smoothed
    const std::vector<float>& peaks() const { return peak_; }    // 0..1, caps that hang a moment
    const std::vector<float>& target() const { return target_; } // the last analysis, unsmoothed
    float energy() const { return energy_; }                     // all the bars, 0..1
    float bass() const { return bass_; }                         // the lowest sixth, 0..1
    float centreHz(int bar) const;                               // the middle of a bar, in pitch

private:
    void settle(double dt);
    int n_ = 0;
    std::vector<float> window_, re_, im_, cos_, sin_;
    std::vector<int> rev_, lo_, hi_;
    std::vector<float> tilt_;
    float norm_ = 1;
    std::vector<float> target_, level_, peak_, hold_;
    float energy_ = 0, bass_ = 0;
};

}  // namespace gh
