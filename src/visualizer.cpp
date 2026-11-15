// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "visualizer.hpp"

#include <algorithm>
#include <cmath>

#include "audio.hpp"

namespace gh {

namespace {

Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto ch = [&](unsigned char x, unsigned char y) { return (unsigned char)std::lround(x + (y - x) * t); };
    return Color{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
}

Color withAlpha(Color c, float a) {
    c.a = (unsigned char)std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    return c;
}

float ease(float now, float target, double dt, double rate) { return now + (target - now) * float(1.0 - std::exp(-dt * rate)); }

}  // namespace

void AudioVisualizer::update(double dt, const Mixer& mixer, size_t behind, const State& st) {
    t_ += dt;
    const size_t n = size_t(spec_.fftSize());
    if (buf_.size() != n) buf_.assign(n, 0.0f);
    // Every frame, the window that ends at what is heard now: it slides on
    // smoothly between the device's buffers.
    if (st.playing) {
        mixer.scope(buf_.data(), n, behind);
        spec_.analyse(buf_.data(), dt);
    } else {
        spec_.decay(dt);
    }
    // The colour of whoever's turn it is, never too dark or too grey to see.
    side_[0] = st.side[0];
    side_[1] = st.side[1];
    Vector3 hsv = ColorToHSV(st.side[st.focus ? 1 : 0]);
    const Color c = ColorFromHSV(hsv.x, std::max(hsv.y, 0.45f), std::max(hsv.z, 0.85f));
    const float rgb[3] = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
    for (int i = 0; i < 3; ++i) base_[i] = ease(base_[i], rgb[i], dt, 5.0);
    heat_ = ease(heat_, std::clamp(st.nps / 24.0f, 0.0f, 1.0f), dt, 3.0);
    spam_ = ease(spam_, std::clamp((st.nps - 40.0f) / 60.0f, 0.0f, 1.0f), dt, 2.0);
    danger_ = ease(danger_, std::clamp((0.5f - st.health) / 0.5f, 0.0f, 1.0f), dt, 4.0);
    hitPulse_ *= float(std::exp(-dt * 10.0));
    oppPulse_ *= float(std::exp(-dt * 8.0));
    missPulse_ *= float(std::exp(-dt * 5.0));
    beatPulse_ *= float(std::exp(-dt * 6.0));
}

void AudioVisualizer::hit(int rating) {
    static const Color kRating[4] = {{255, 246, 200, 255}, {150, 255, 180, 255}, {255, 196, 110, 255}, {255, 130, 130, 255}};
    hitColor_ = kRating[std::clamp(rating, 0, 3)];
    hitPulse_ = 1.0f;
}

void AudioVisualizer::opponentHit() { oppPulse_ = std::max(oppPulse_, 0.7f); }
void AudioVisualizer::miss() { missPulse_ = 1.0f; }
void AudioVisualizer::beat() { beatPulse_ = 1.0f; }

void AudioVisualizer::draw(Rectangle area, float alpha) const {
    if (alpha <= 0.01f || area.width < 8 || area.height < 8) return;
    const int B = spec_.bars();
    const std::vector<float>& lv = spec_.levels();
    const std::vector<float>& pk = spec_.peaks();
    const Color base{(unsigned char)std::lround(base_[0] * 255), (unsigned char)std::lround(base_[1] * 255),
                     (unsigned char)std::lround(base_[2] * 255), 255};
    const Vector3 hsv = ColorToHSV(base);
    // The tops: brighter and warmer the thicker the notes get.
    const Color top = ColorFromHSV(std::fmod(hsv.x + 25.0f + 70.0f * heat_, 360.0f), std::min(1.0f, hsv.y * 0.7f + 0.25f), 1.0f);
    const Color bottom = ColorFromHSV(hsv.x, std::min(1.0f, hsv.y + 0.1f), 0.55f + 0.25f * heat_);
    const float danger = danger_ * (0.45f + 0.55f * beatPulse_);  // low health: the bars throb red on the beat

    // A dark backing so the bars read over any stage, a little lighter on the beat.
    DrawRectangleRounded({area.x - 8, area.y - 5, area.width + 16, area.height + 11}, 0.3f, 6,
                         withAlpha(mix(BLACK, base, 0.12f * beatPulse_), (0.24f + 0.08f * beatPulse_) * alpha));
    const float gap = 2.0f, bw = std::max(1.0f, (area.width - gap * float(B - 1)) / float(B));
    const float floorY = area.y + area.height;
    for (int b = 0; b < B; ++b) {
        // A miss shakes the bars for a moment.
        const float x = area.x + float(b) * (bw + gap) + missPulse_ * 3.0f * std::sin(float(b) * 2.3f + float(t_) * 70.0f);
        const float h = std::max(2.0f, lv[size_t(b)] * area.height);
        Color ct = top, cb = bottom;
        if (spam_ > 0.01f) {  // spam: a rainbow running across the bars
            const float hue = std::fmod(float(t_) * 140.0f + float(b) * 9.0f, 360.0f);
            ct = mix(ct, ColorFromHSV(hue, 0.7f, 1.0f), spam_);
            cb = mix(cb, ColorFromHSV(std::fmod(hue + 40.0f, 360.0f), 0.8f, 0.8f), spam_ * 0.8f);
        }
        ct = mix(ct, hitColor_, 0.55f * hitPulse_);                      // a hit lights the tops, in its rating's colour
        cb = mix(cb, ColorBrightness(side_[0], 0.1f), 0.4f * oppPulse_);  // the opponent singing tints the feet
        ct = mix(ct, Color{255, 50, 70, 255}, 0.75f * missPulse_);
        cb = mix(cb, Color{110, 0, 20, 255}, 0.6f * missPulse_);
        ct = mix(ct, Color{255, 70, 70, 255}, 0.7f * danger);
        cb = mix(cb, Color{120, 0, 24, 255}, danger);
        ct = withAlpha(ct, 0.95f * alpha);
        cb = withAlpha(cb, 0.85f * alpha);
        // raylib 5.5 puts its third colour bottom right and its fourth top right.
        DrawRectangleGradientEx({x, floorY - h, bw, h}, ct, cb, cb, ct);
        const float py = floorY - pk[size_t(b)] * area.height - 3.0f;
        if (pk[size_t(b)] > 0.02f) DrawRectangleRec({x, py, bw, 2}, withAlpha(ColorBrightness(ct, 0.35f), 0.9f * alpha));
    }
    DrawRectangleRec({area.x - 2, floorY + 1, area.width + 4, 2}, withAlpha(mix(base, WHITE, 0.25f * beatPulse_), 0.8f * alpha));
}

}  // namespace gh
