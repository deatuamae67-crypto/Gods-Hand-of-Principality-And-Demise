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

// A gradient from `top` to `bottom` (raylib 5.5 takes the corners as top-left,
// bottom-left, bottom-right, top-right).
void vgradient(Rectangle r, Color top, Color bottom) { DrawRectangleGradientEx(r, top, bottom, bottom, top); }

// A rounded bar from `top` (its tip) down to `bottom` (its foot).
void pill(float x, float y0, float y1, float w, Color top, Color bottom) {
    const float r = w * 0.5f;
    if (y1 - y0 <= w) {
        DrawCircleV({x + r, y1 - r}, r, mix(top, bottom, 0.5f));
        return;
    }
    vgradient({x, y0 + r, w, y1 - y0 - 2 * r}, top, bottom);
    DrawCircleV({x + r, y0 + r}, r, top);
    DrawCircleV({x + r, y1 - r}, r, bottom);
}

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

// Issue #31: a modern look. A pane of tinted glass with a gloss and a rim
// that lights up with the bass; rounded, glossy bars that glow when they are
// tall; a line of light running over their tips; the peaks as small dots;
// a reflection under a floor of light. The colours still follow the chart.
void AudioVisualizer::draw(Rectangle area, float alpha) const {
    if (alpha <= 0.01f || area.width < 8 || area.height < 8) return;
    const int B = std::min(spec_.bars(), 64);
    const std::vector<float>& lv = spec_.levels();
    const std::vector<float>& pk = spec_.peaks();
    const Color base{(unsigned char)std::lround(base_[0] * 255), (unsigned char)std::lround(base_[1] * 255),
                     (unsigned char)std::lround(base_[2] * 255), 255};
    const Vector3 hsv = ColorToHSV(base);
    // The tips: brighter and warmer the thicker the notes get.
    const Color top = ColorFromHSV(std::fmod(hsv.x + 25.0f + 70.0f * heat_, 360.0f), std::min(1.0f, hsv.y * 0.7f + 0.25f), 1.0f);
    const Color bottom = ColorFromHSV(hsv.x, std::min(1.0f, hsv.y + 0.1f), 0.5f + 0.25f * heat_);
    const float danger = danger_ * (0.45f + 0.55f * beatPulse_);  // low health: the bars throb red on the beat
    const float bass = std::clamp(spec_.bass() * 1.4f, 0.0f, 1.0f);

    // The glass: dark enough for the bars to read over any stage.
    const Rectangle pane{area.x - 9, area.y - 6, area.width + 18, area.height + 12};
    const float round = std::min(0.5f, 14.0f / std::min(pane.width, pane.height));
    DrawRectangleRounded(pane, round, 10, withAlpha(mix(Color{8, 12, 34, 255}, base, 0.10f + 0.12f * beatPulse_), (0.46f + 0.06f * beatPulse_) * alpha));
    const float cr = round * std::min(pane.width, pane.height) * 0.5f;
    vgradient({pane.x + cr, pane.y + 1.5f, pane.width - 2 * cr, pane.height * 0.42f}, withAlpha(WHITE, 0.13f * alpha), withAlpha(WHITE, 0.0f));
    DrawRectangleRoundedLinesEx(pane, round, 10, 1.4f,
                                withAlpha(mix(mix(WHITE, base, 0.55f), Color{255, 60, 80, 255}, danger), (0.30f + 0.45f * bass + 0.15f * beatPulse_) * alpha));

    // The bars stand on a floor 80% of the way down; the rest is their reflection.
    const float floorY = area.y + area.height * 0.8f;
    const float room = floorY - area.y, below = area.y + area.height - floorY;
    const float gap = 2.0f, bw = std::max(2.0f, (area.width - gap * float(B - 1)) / float(B));
    Color tips[64], feet[64];
    Vector2 line[64];
    for (int b = 0; b < B; ++b) {
        Color ct = top, cb = bottom;
        if (spam_ > 0.01f) {  // spam: a rainbow running across the bars
            const float hue = std::fmod(float(t_) * 140.0f + float(b) * 9.0f, 360.0f);
            ct = mix(ct, ColorFromHSV(hue, 0.7f, 1.0f), spam_);
            cb = mix(cb, ColorFromHSV(std::fmod(hue + 40.0f, 360.0f), 0.8f, 0.8f), spam_ * 0.8f);
        }
        ct = mix(ct, hitColor_, 0.55f * hitPulse_);                      // a hit lights the tips, in its rating's colour
        cb = mix(cb, ColorBrightness(side_[0], 0.1f), 0.4f * oppPulse_);  // the opponent singing tints the feet
        ct = mix(ct, Color{255, 50, 70, 255}, 0.75f * missPulse_);
        cb = mix(cb, Color{110, 0, 20, 255}, 0.6f * missPulse_);
        ct = mix(ct, Color{255, 70, 70, 255}, 0.7f * danger);
        cb = mix(cb, Color{120, 0, 24, 255}, danger);
        tips[b] = ct;
        feet[b] = cb;
    }
    // Glow behind the tall bars, then the bars, their gloss and their reflection.
    BeginBlendMode(BLEND_ADDITIVE);
    for (int b = 0; b < B; ++b) {
        const float l = lv[size_t(b)];
        if (l < 0.3f) continue;
        const float x = area.x + float(b) * (bw + gap) + bw * 0.5f;
        DrawCircleGradient(int(x), int(floorY - l * room * 0.55f), bw * 1.6f + l * room * 0.35f, withAlpha(tips[b], 0.16f * l * alpha),
                           withAlpha(tips[b], 0.0f));
    }
    EndBlendMode();
    for (int b = 0; b < B; ++b) {
        // A miss shakes the bars for a moment.
        const float x = area.x + float(b) * (bw + gap) + missPulse_ * 3.0f * std::sin(float(b) * 2.3f + float(t_) * 70.0f);
        const float h = std::max(bw, lv[size_t(b)] * room);
        pill(x, floorY - h, floorY, bw, withAlpha(tips[b], 0.96f * alpha), withAlpha(feet[b], 0.88f * alpha));
        if (h > bw * 2.0f)  // the glass's gloss on the bar's left side
            DrawRectangleRec({x + bw * 0.22f, floorY - h + bw * 0.5f, std::max(1.0f, bw * 0.2f), h - bw}, withAlpha(WHITE, 0.22f * alpha));
        const float rh = std::min(h * 0.45f, below - 2.0f);
        if (rh > 1.0f) vgradient({x, floorY + 2.0f, bw, rh}, withAlpha(feet[b], 0.30f * alpha), withAlpha(feet[b], 0.0f));
        line[b] = {x + bw * 0.5f, floorY - h};
        // The peak: a small dot that hangs a moment over the bar.
        const float p = pk[size_t(b)];
        if (p > 0.03f && p * room > h + bw) DrawCircleV({x + bw * 0.5f, floorY - p * room - bw * 0.5f}, bw * 0.38f,
                                                        withAlpha(ColorBrightness(tips[b], 0.4f), 0.9f * alpha));
    }
    // A line of light over the tips, a soft one under a bright one.
    if (B >= 4) {
        const Color glowC = mix(top, WHITE, 0.3f);
        BeginBlendMode(BLEND_ADDITIVE);
        DrawSplineCatmullRom(line, B, 4.0f, withAlpha(glowC, (0.16f + 0.12f * beatPulse_) * alpha));
        EndBlendMode();
        DrawSplineCatmullRom(line, B, 1.3f, withAlpha(mix(glowC, WHITE, 0.4f), 0.55f * alpha));
    }
    // The floor: a line of light, brightest in the middle, flaring with the bass.
    const Color floorC = mix(mix(base, WHITE, 0.35f + 0.3f * beatPulse_), Color{255, 70, 70, 255}, danger);
    const float fw = area.width * 0.5f + 2.0f, fx = area.x + area.width * 0.5f;
    const float fa = (0.55f + 0.45f * bass) * alpha;
    DrawRectangleGradientEx({fx - fw, floorY, fw, 2}, withAlpha(floorC, 0), withAlpha(floorC, 0), withAlpha(floorC, fa), withAlpha(floorC, fa));
    DrawRectangleGradientEx({fx, floorY, fw, 2}, withAlpha(floorC, fa), withAlpha(floorC, fa), withAlpha(floorC, 0), withAlpha(floorC, 0));
}

}  // namespace gh
