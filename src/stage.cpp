// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "common.hpp"

namespace gh {

using gfx::fade;
using gfx::mix;

namespace {

constexpr Color kInk{24, 16, 36, 255};
constexpr Rectangle kFull{-760, -560, 2800, 1820};  // what any camera can see, zoomed out too

uint32_t hash3(int a, int b, int c) {
    uint32_t h = uint32_t(a) * 73856093u ^ uint32_t(b) * 19349663u ^ uint32_t(c) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

// Boxes and discs with a dark outline: the stages' cartoon look.
void inkRect(Rectangle r, Color c, float line = 4.0f) {
    DrawRectangleRec({r.x - line, r.y - line, r.width + line * 2, r.height + line * 2}, kInk);
    DrawRectangleRec(r, c);
}
void inkCircle(Vector2 c, float r, Color col, float line = 4.0f) {
    DrawCircleV(c, r + line, kInk);
    DrawCircleV(c, r, col);
}

// A loudspeaker cabinet: two cones that pump on the beat.
void speaker(Rectangle r, float pump) {
    inkRect(r, Color{40, 36, 58, 255});
    const float cx = r.x + r.width * 0.5f;
    const float big = std::min(r.width, r.height * 0.5f) * 0.38f;
    for (int i = 0; i < 2; ++i) {
        const Vector2 c{cx, r.y + r.height * (0.27f + 0.48f * float(i))};
        const float rr = big * (i == 0 ? 0.72f : 1.0f) * (1.0f + 0.08f * pump);
        inkCircle(c, rr, Color{72, 68, 94, 255}, 3.0f);
        DrawCircleV(c, rr * 0.45f, Color{24, 20, 36, 255});
        DrawCircleV({c.x - rr * 0.22f, c.y - rr * 0.22f}, rr * 0.12f, fade(WHITE, 0.35f));
    }
}

// A guitar amplifier: control panel and grille cloth.
void amp(Rectangle r) {
    inkRect(r, Color{30, 26, 36, 255});
    DrawRectangleRec({r.x + 10, r.y + 34, r.width - 20, r.height - 44}, Color{56, 50, 62, 255});
    for (float y = r.y + 40; y < r.y + r.height - 12; y += 8) DrawLineEx({r.x + 12, y}, {r.x + r.width - 12, y}, 1.5f, fade(kInk, 0.35f));
    DrawRectangleRec({r.x + 10, r.y + 8, r.width - 20, 18}, Color{206, 186, 124, 255});
    for (int k = 0; k < 5; ++k) DrawCircleV({r.x + 24 + float(k) * (r.width - 48) / 4, r.y + 17}, 5, kInk);
}

// A brick wall: rows of bricks, every other row shifted half a brick.
void bricks(Rectangle r, Color brick, Color mortar, float bw, float bh, bool detail) {
    DrawRectangleRec(r, mortar);
    if (!detail) {
        DrawRectangleRec({r.x, r.y, r.width, r.height}, mix(brick, mortar, 0.25f));
        return;
    }
    int row = 0;
    for (float y = r.y; y < r.y + r.height; y += bh, ++row) {
        const float off = float(row % 2) * bw * 0.5f;
        for (float x = r.x - off; x < r.x + r.width; x += bw) {
            const float x0 = std::max(r.x, x + 2), x1 = std::min(r.x + r.width, x + bw - 2);
            const float y1 = std::min(r.y + r.height, y + bh - 3);
            if (x1 <= x0 || y1 <= y + 1) continue;
            const uint32_t h = hash3(int(x), int(y), 9);
            DrawRectangleRec({x0, y + 1, x1 - x0, y1 - y - 1}, mix(brick, mortar, float(h % 5) * 0.07f));
        }
    }
}

// A cartoon cloud: three puffs on a flat base.
void cloud(float x, float y, float s, Color c) {
    DrawCircleV({x, y}, 34 * s, c);
    DrawCircleV({x + 42 * s, y - 16 * s}, 46 * s, c);
    DrawCircleV({x + 92 * s, y}, 32 * s, c);
    DrawRectangleRounded({x - 30 * s, y - 2 * s, 152 * s, 34 * s}, 1.0f, 8, c);
}

}  // namespace

void Stage::init(int kind, Color accent, bool lowDetail, bool flashing) {
    kind_ = kind;
    accent_ = accent;
    low_ = lowDetail;
    flashing_ = flashing;
    flash_ = pulse_ = 0;
    beat_ = 0;
    motes_.clear();
    stars_.clear();
    far_.clear();
    near_.clear();
    Rng rng(0x51A7u + uint64_t(kind));
    for (int i = 0; i < 260; ++i) {
        const double u = rng.uniform();
        stars_.push_back({float(rng.uniform() * 2800 - 760), float(rng.uniform() * 1040 - 560), float(1.4 + u * u * 6.5),
                          float(rng.uniform() * 6.28)});
    }
    for (float x = -800; x < 2080;) {
        const float w = float(70 + rng.uniform() * 90);
        far_.push_back({x, w, float(150 + rng.uniform() * 190)});
        x += w + float(rng.uniform() * 14);
    }
    for (float x = -820; x < 2100;) {
        const float w = float(90 + rng.uniform() * 120);
        near_.push_back({x, w, float(60 + rng.uniform() * 120)});
        x += w + float(10 + rng.uniform() * 60);
    }
}

void Stage::update(float dt, double now) {
    flash_ = std::max(0.0f, flash_ - dt * 2.5f);
    pulse_ = std::max(0.0f, pulse_ - dt * 3.0f);
    motes_.update(dt, kind_ == 2 ? -20.0f : 0.0f);
    if (low_) return;
    spawn_ += dt * (kind_ == 2 ? 26.0f : 12.0f);
    while (spawn_ >= 1.0f) {
        spawn_ -= 1.0f;
        gfx::Particle p{};
        p.p = {gfx::randf(-200, 1480), gfx::randf(560, 820)};
        p.kind = 0;
        if (kind_ == 0) {  // warm dust in the sunset
            p.p.y = gfx::randf(200, 700);
            p.v = {gfx::randf(-14, 14), gfx::randf(-20, -6)};
            p.c = Color{255, 222, 170, 150};
            p.size = gfx::randf(3, 7);
            p.life = p.maxLife = gfx::randf(4, 7);
        } else if (kind_ == 1) {  // sparkles off the mirror ball
            p.p = {gfx::randf(-200, 1480), gfx::randf(-100, 620)};
            p.v = {gfx::randf(-10, 10), gfx::randf(-10, 10)};
            p.c = (std::rand() % 2) ? Color{120, 240, 255, 220} : Color{255, 130, 230, 220};
            p.size = gfx::randf(2, 5);
            p.life = p.maxLife = gfx::randf(0.6f, 1.6f);
        } else if (kind_ == 2) {  // embers from the fire barrel
            p.p = {1190 + gfx::randf(-30, 30), 482};
            p.v = {gfx::randf(-24, 24), gfx::randf(-150, -60)};
            p.c = (std::rand() % 3) ? Color{255, 110, 40, 230} : Color{255, 200, 90, 230};
            p.size = gfx::randf(2, 5);
            p.life = p.maxLife = gfx::randf(1.2f, 2.6f);
        } else {
            p.v = {gfx::randf(-10, 10), gfx::randf(-35, -10)};
            p.c = fade(accent_, 0.8f);
            p.size = gfx::randf(3, 8);
            p.life = p.maxLife = gfx::randf(3, 6);
        }
        motes_.add(p);
    }
    (void)now;
}

void Stage::onBeat(int beat) {
    pulse_ = 1.0f;
    beat_ = beat;
    if (kind_ == 2 && flashing_ && beat % 16 == 8) flash_ = 0.45f;
}

// ---------------------------------------------------------------- rooftop --

// The school's roof at sunset: a striped sun over the city, string lights
// that swap on every beat, a chain-link fence, the water tower.
void Stage::drawRooftop(float t) const {
    const Color skyTop{64, 34, 120, 255}, skyMid{238, 96, 140, 255}, skyLow{255, 176, 96, 255};
    gfx::gradientRect({kFull.x, kFull.y, kFull.width, 210 - kFull.y}, skyTop, skyMid);
    gfx::gradientRect({kFull.x, 210, kFull.width, 362}, skyMid, skyLow);
    const Vector2 sun{640, 410};
    const float sr = 200 + 6 * pulse_;
    gfx::glow(sun, sr * 1.8f, Color{255, 150, 120, 110});
    DrawCircleGradient(int(sun.x), int(sun.y), sr, Color{255, 240, 140, 255}, Color{255, 110, 120, 255});
    for (int k = 0; k < 7; ++k) {
        const float y = sun.y - 50 + float(k) * 26.0f, h = 3 + float(k) * 2.4f;
        DrawRectangleRec({sun.x - sr - 4, y, sr * 2 + 8, h}, mix(skyMid, skyLow, saturate((y - 210) / 362)));
    }
    for (int i = 0; i < 5; ++i) {
        const float x = std::fmod(float(i) * 560.0f + t * (8.0f + float(i) * 3.0f), 3000.0f) - 860;
        cloud(x, 30.0f + float(i % 3) * 64, 0.8f + 0.2f * float(i % 2), Color{255, 196, 214, 255});
    }
    // the city: far blocks with windows (a few switch every two beats), near blocks
    for (size_t i = 0; i < far_.size(); ++i) {
        const Block& b = far_[i];
        const float top = 540 - b.h;
        DrawRectangleRec({b.x, top, b.w, b.h}, Color{112, 56, 134, 255});
        if (low_) continue;
        for (float wy = top + 14; wy < 520; wy += 22)
            for (float wx = b.x + 10; wx < b.x + b.w - 14; wx += 18) {
                const uint32_t h = hash3(int(i), int(wx), int(wy));
                const bool lit = (h % 3 == 0) != (h % 13 == 0 && (beat_ / 2) % 2 == 0);
                if (lit) DrawRectangleRec({wx, wy, 8, 11}, Color{255, 214, 140, 210});
            }
    }
    for (const Block& b : near_) {
        const float top = 548 - b.h;
        DrawRectangleRec({b.x, top, b.w, b.h}, Color{70, 32, 96, 255});
        DrawLineEx({b.x + b.w * 0.7f, top}, {b.x + b.w * 0.7f, top - 34}, 3, Color{70, 32, 96, 255});
    }
    // the water tower
    {
        const float x = 70, base = 520;
        for (int l = 0; l < 4; ++l) DrawLineEx({x - 52 + float(l) * 34.0f, base}, {x - 40 + float(l) * 26.0f, base - 124}, 7, kInk);
        DrawLineEx({x - 46, base - 56}, {x + 46, base - 56}, 4, kInk);
        inkRect({x - 64, base - 232, 128, 110}, Color{110, 66, 132, 255});
        for (int s = 0; s < 3; ++s) DrawLineEx({x - 64, base - 204 + float(s) * 32.0f}, {x + 64, base - 204 + float(s) * 32.0f}, 3, fade(kInk, 0.5f));
        gfx::tri({x - 78, base - 230}, {x + 78, base - 230}, {x, base - 292}, kInk);
        gfx::tri({x - 68, base - 234}, {x + 68, base - 234}, {x, base - 284}, Color{140, 80, 150, 255});
    }
    // the fence on the roof's low wall
    const Color wire = fade(Color{60, 40, 80, 255}, 0.55f);
    for (float x = kFull.x; x < kFull.x + kFull.width + 130; x += 22) {
        DrawLineEx({x, 400}, {x - 120, 520}, 1.4f, wire);
        DrawLineEx({x - 120, 400}, {x, 520}, 1.4f, wire);
    }
    for (float x = kFull.x + 60; x < kFull.x + kFull.width; x += 220) DrawRectangleRec({x - 5, 384, 10, 136}, Color{70, 46, 92, 255});
    DrawLineEx({kFull.x, 400}, {kFull.x + kFull.width, 400}, 5, Color{70, 46, 92, 255});
    DrawRectangleRec({kFull.x, 512, kFull.width, 52}, Color{176, 138, 168, 255});
    DrawRectangleRec({kFull.x, 512, kFull.width, 10}, Color{206, 170, 196, 255});
    DrawLineEx({kFull.x, 512}, {kFull.x + kFull.width, 512}, 3, kInk);
    DrawLineEx({kFull.x, 564}, {kFull.x + kFull.width, 564}, 3, kInk);
    // an air conditioner, its fan turning
    inkRect({1090, 440, 150, 72}, Color{190, 180, 204, 255});
    const Vector2 fan{1130, 476};
    inkCircle(fan, 26, Color{92, 86, 112, 255}, 3);
    for (int b = 0; b < 3; ++b) {
        const float a = t * 9.0f + float(b) * 2.094f;
        DrawLineEx(fan, {fan.x + std::cos(a) * 22, fan.y + std::sin(a) * 22}, 6, Color{204, 200, 218, 255});
    }
    for (int g = 0; g < 4; ++g) DrawLineEx({1170, 454 + float(g) * 14.0f}, {1228, 454 + float(g) * 14.0f}, 3, fade(kInk, 0.5f));
    // string lights between two poles
    DrawLineEx({-40, 200}, {-40, 512}, 8, kInk);
    DrawLineEx({1320, 200}, {1320, 512}, 8, kInk);
    static const Color kBulbs[4] = {{255, 230, 90, 255}, {255, 110, 190, 255}, {90, 230, 255, 255}, {150, 255, 120, 255}};
    Vector2 prev{-40, 206};
    for (int i = 0; i <= 40; ++i) {
        const float u = float(i) / 40.0f;
        const Vector2 p{-40 + u * 1360, 206 + std::sin(u * 3.14159265f) * 92};
        DrawLineEx(prev, p, 2, kInk);
        prev = p;
        const bool on = (i + beat_) % 2 == 0;
        const Color bc = kBulbs[i % 4];
        DrawCircleV({p.x, p.y + 8}, 6, on ? bc : mix(bc, kInk, 0.6f));
        if (on && !low_) gfx::glow({p.x, p.y + 8}, 30 + 10 * pulse_, fade(bc, 0.5f));
    }
    // the roof's floor
    gfx::gradientRect({kFull.x, 566, kFull.width, kFull.y + kFull.height - 566}, Color{150, 116, 150, 255}, Color{66, 46, 80, 255});
    for (int i = -26; i <= 26; ++i) DrawLineEx({640 + float(i) * 90.0f, 566}, {640 + float(i) * 290.0f, 1000}, 2, fade(kInk, 0.18f));
    for (int r = 0; r < 6; ++r) {
        const float y = 566 + std::pow(1.55f, float(r)) * 22 - 10;
        DrawLineEx({kFull.x, y}, {kFull.x + kFull.width, y}, 2, fade(kInk, 0.18f));
    }
}

// ------------------------------------------------------------------- club --

// The club: an equalizer wall, a mirror ball, lasers, a dance floor lighting
// up on the beat (the crowd is drawn in front).
void Stage::drawClub(float t) const {
    gfx::gradientRect(kFull, Color{10, 6, 26, 255}, Color{40, 14, 66, 255});
    const Rectangle wall{170, 30, 940, 430};
    inkRect({wall.x - 10, wall.y - 10, wall.width + 20, wall.height + 20}, Color{24, 20, 40, 255}, 5);
    const int cols = 24, rows = 11;
    const float cw = wall.width / float(cols), ch = wall.height / float(rows);
    for (int c = 0; c < cols; ++c) {
        const float level = 0.22f + 0.36f * (0.5f + 0.5f * std::sin(t * 2.3f + float(c) * 0.55f)) +
                            0.4f * pulse_ * (0.5f + 0.5f * std::sin(float(c) * 1.7f + float(beat_)));
        const int lit = int(level * float(rows));
        for (int r = 0; r < rows; ++r) {
            const int up = rows - 1 - r;
            const float u = float(up) / float(rows - 1);
            const Color col = u < 0.5f ? mix(Color{40, 220, 255, 255}, Color{255, 60, 200, 255}, u * 2)
                                       : mix(Color{255, 60, 200, 255}, Color{255, 230, 80, 255}, (u - 0.5f) * 2);
            DrawRectangleRec({wall.x + float(c) * cw + 2, wall.y + float(r) * ch + 2, cw - 4, ch - 4}, up < lit ? col : fade(col, 0.1f));
        }
    }
    // the mirror ball and the light it throws
    const Vector2 ball{640, -60};
    DrawLineEx({640, -230}, {640, ball.y - 70}, 3, Color{160, 160, 180, 255});
    inkCircle(ball, 70, Color{150, 156, 186, 255});
    for (int i = 0; i < 7; ++i) {
        const float lat = -1.2f + float(i) * 0.4f;
        const float y = ball.y + std::sin(lat) * 64, rr = std::cos(lat) * 66;
        for (int k = 0; k < 14; ++k) {
            const float a = t * 0.9f + float(k) * 0.4488f;
            const float z = std::cos(a);
            if (z <= 0.05f) continue;
            const float x = ball.x + std::sin(a) * rr;
            const float s = 9.0f * z * std::cos(lat) + 2;
            const float shine = float(hash3(i, k, 0) % 100) / 100.0f;
            DrawRectangleRec({x - s * 0.5f, y - 4.5f, s, 9}, mix(Color{120, 126, 160, 255}, WHITE, shine * z));
        }
    }
    gfx::glow(ball, 150 + 40 * pulse_, Color{200, 220, 255, 70});
    if (!low_)
        for (int i = 0; i < 18; ++i) {
            const float a = t * 0.5f + float(i) * 0.349f;
            const Vector2 p{640 + std::sin(a) * (300 + float(i % 4) * 160), 160 + std::cos(a * 1.3f + float(i)) * 200};
            gfx::glow(p, 18, fade(i % 2 ? Color{90, 230, 255, 255} : Color{255, 120, 220, 255}, 0.6f));
        }
    // lasers sweeping from the ceiling's corners
    if (!low_) {
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 8; ++i) {
            const bool left = i < 4;
            const Vector2 o{left ? -120.0f : 1400.0f, -160};
            const float a = (left ? 0.9f : 2.24f) + std::sin(t * 1.4f + float(i) * 0.8f) * 0.35f;
            const Vector2 e{o.x + std::cos(a) * 1700, o.y + std::sin(a) * 1700};
            const Color lc = i % 2 ? Color{60, 255, 120, 255} : Color{255, 60, 160, 255};
            DrawLineEx(o, e, 3, fade(lc, 0.45f + 0.4f * pulse_));
            DrawLineEx(o, e, 10, fade(lc, 0.12f));
        }
        EndBlendMode();
    }
    speaker({-80, 230, 170, 160}, pulse_);
    speaker({-80, 400, 170, 160}, pulse_);
    speaker({1190, 230, 170, 160}, pulse_);
    speaker({1190, 400, 170, 160}, pulse_);
    // the dance floor, in perspective
    const float fy = 560;
    gfx::gradientRect({kFull.x, fy, kFull.width, kFull.y + kFull.height - fy}, Color{24, 10, 40, 255}, Color{8, 4, 14, 255});
    const Vector2 vp{640, 300};
    static const Color kTile[3] = {{255, 60, 200, 255}, {40, 220, 255, 255}, {255, 230, 80, 255}};
    float y0 = fy;
    for (int r = 0; r < 7; ++r) {
        const float y1 = y0 + 26.0f + float(r) * 22.0f;
        const float s0 = (y0 - vp.y) / (fy - vp.y), s1 = (y1 - vp.y) / (fy - vp.y);
        for (int c = -14; c < 14; ++c) {
            const float tw = 130;
            const int pick = (r + c + 30 + beat_) % 4;
            const Color col = pick < 3 ? fade(kTile[pick], 0.22f + 0.45f * pulse_) : Color{20, 12, 34, 255};
            gfx::quad({vp.x + float(c) * tw * s0, y0}, {vp.x + float(c + 1) * tw * s0, y0}, {vp.x + float(c + 1) * tw * s1, y1},
                      {vp.x + float(c) * tw * s1, y1}, col);
        }
        DrawLineEx({kFull.x, y0}, {kFull.x + kFull.width, y0}, 3, kInk);
        y0 = y1;
    }
    for (int c = -14; c <= 14; ++c) {
        const float s1 = (y0 - vp.y) / (fy - vp.y);
        DrawLineEx({vp.x + float(c) * 130, fy}, {vp.x + float(c) * 130 * s1, y0}, 3, kInk);
    }
}

// ------------------------------------------------------------------ alley --

// The back alley: brick walls under a red moon, a neon LIVE sign, graffiti,
// a fire barrel, puddles that hold the moon.
void Stage::drawAlley(float t) const {
    gfx::gradientRect({kFull.x, kFull.y, kFull.width, 580 - kFull.y}, Color{8, 4, 18, 255}, Color{86, 14, 44, 255});
    for (const Star& s : stars_)
        if (s.y < 260) DrawCircleV({s.x, s.y}, s.s * 0.4f, fade(WHITE, 0.25f + 0.35f * (0.5f + 0.5f * std::sin(t * 2 + s.ph))));
    const Vector2 moon{640, 110};
    gfx::glow(moon, 300, Color{255, 50, 70, (unsigned char)(90 + 60 * pulse_)});
    DrawCircleV(moon, 110, Color{226, 52, 72, 255});
    DrawCircleV({610, 80}, 22, Color{196, 38, 60, 255});
    DrawCircleV({676, 132}, 30, Color{196, 38, 60, 255});
    DrawCircleV({662, 66}, 12, Color{196, 38, 60, 255});
    // the walls: the back one, and the tall side buildings
    bricks({160, 250, 960, 330}, Color{116, 42, 58, 255}, Color{58, 20, 36, 255}, 64, 24, !low_);
    DrawLineEx({160, 250}, {1120, 250}, 6, kInk);
    bricks({kFull.x, -60, 170 - kFull.x, 640}, Color{72, 30, 54, 255}, Color{36, 14, 30, 255}, 70, 26, !low_);
    bricks({1110, -60, kFull.x + kFull.width - 1110, 640}, Color{72, 30, 54, 255}, Color{36, 14, 30, 255}, 70, 26, !low_);
    DrawLineEx({170, -60}, {170, 580}, 6, kInk);
    DrawLineEx({1110, -60}, {1110, 580}, 6, kInk);
    for (int w = 0; w < 4; ++w) {  // windows, one lit violet
        const float x = w < 2 ? -190.0f + float(w) * 170 : 1180.0f + float(w - 2) * 170;
        inkRect({x, 0, 70, 90}, w == 1 ? Color{150, 90, 220, 255} : Color{24, 14, 30, 255});
        DrawLineEx({x + 35, 0}, {x + 35, 90}, 3, kInk);
    }
    // a fire escape on the left building
    for (int f = 0; f < 3; ++f) {
        const float y = 120 + float(f) * 150.0f;
        DrawLineEx({-170, y}, {150, y}, 6, kInk);
        DrawLineEx({-170, y - 46}, {150, y - 46}, 3, kInk);
        for (float x = -170; x <= 150; x += 32) DrawLineEx({x, y}, {x, y - 46}, 2, kInk);
        if (f < 2) DrawLineEx({f % 2 ? -150.0f : 130.0f, y}, {f % 2 ? 130.0f : -150.0f, y + 150}, 6, kInk);
    }
    // graffiti: looping spray tags with a dark shadow
    static const Color kSpray[3] = {{90, 255, 120, 255}, {255, 220, 60, 255}, {60, 200, 255, 255}};
    static const float kTagX[3] = {200, 790, 960};
    for (int g = 0; g < 3; ++g) {
        for (int pass = 0; pass < 2; ++pass) {
            const float off = pass == 0 ? 4.0f : 0.0f;
            const Color c = pass == 0 ? fade(kInk, 0.6f) : kSpray[g];
            Vector2 prev{};
            for (int s = 0; s <= 40; ++s) {
                const float u = float(s) / 40.0f;
                const float a = u * 6.2831853f * 2.0f + float(g);
                const Vector2 q{kTagX[g] + u * 120 + std::cos(a) * 14 + off, 520 + std::sin(a) * 18 - u * 10 + off};
                if (s > 0) DrawLineEx(prev, q, 6, c);
                prev = q;
            }
            DrawCircleV({prev.x + 8, prev.y - 2}, 4, c);
        }
    }
    // the neon sign, flickering now and then (not with flashing lights off)
    const bool on = !flashing_ || hash3(int(t * 8), 3, 3) % 11 != 0;
    const Color neon{255, 70, 150, 255};
    inkRect({530, 290, 220, 84}, Color{30, 14, 30, 255});
    if (on) gfx::glow({640, 332}, 170, fade(neon, 0.35f + 0.25f * pulse_));
    gfx::text(gfx::FontId::Title, "LIVE", 640, 300, 60, on ? neon : mix(neon, kInk, 0.7f), gfx::Align::Center, 4.0f);
    // a dumpster and the fire barrel
    inkRect({24, 476, 170, 100}, Color{44, 120, 80, 255});
    inkRect({16, 462, 186, 16}, Color{34, 96, 64, 255});
    for (int i = 0; i < 4; ++i) DrawLineEx({54 + float(i) * 36.0f, 488}, {54 + float(i) * 36.0f, 566}, 3, fade(kInk, 0.4f));
    const Vector2 bar{1190, 580};
    gfx::glow({bar.x, bar.y - 130}, 170 + 24 * pulse_, Color{255, 120, 40, 120});
    for (int k = 0; k < 3; ++k) {
        const float h = 70 - float(k) * 18 + std::sin(t * 9 + float(k) * 2) * 10 + 12 * pulse_;
        const float w = 36 - float(k) * 9;
        const Color fc = k == 0 ? Color{255, 90, 40, 255} : k == 1 ? Color{255, 170, 50, 255} : Color{255, 240, 140, 255};
        const float top = bar.y - 96;
        gfx::tri({bar.x - w, top}, {bar.x + w, top}, {bar.x + std::sin(t * 7 + float(k)) * 8, top - h}, fc);
        gfx::tri({bar.x - w * 0.9f, top}, {bar.x, top}, {bar.x - w * 0.6f + std::sin(t * 11 + float(k)) * 6, top - h * 0.7f}, fc);
        gfx::tri({bar.x, top}, {bar.x + w * 0.9f, top}, {bar.x + w * 0.6f + std::cos(t * 10 + float(k)) * 6, top - h * 0.65f}, fc);
    }
    inkRect({bar.x - 40, bar.y - 96, 80, 96}, Color{126, 72, 50, 255});
    for (int r = 0; r < 3; ++r) DrawLineEx({bar.x - 40, bar.y - 80 + float(r) * 30}, {bar.x + 40, bar.y - 80 + float(r) * 30}, 4, fade(kInk, 0.6f));
    // wet asphalt and puddles
    gfx::gradientRect({kFull.x, 578, kFull.width, kFull.y + kFull.height - 578}, Color{48, 32, 54, 255}, Color{12, 8, 18, 255});
    DrawLineEx({kFull.x, 578}, {kFull.x + kFull.width, 578}, 4, kInk);
    for (Vector2 c : {Vector2{150, 690}, Vector2{640, 660}, Vector2{1170, 700}}) {
        DrawEllipse(int(c.x), int(c.y), 120, 20, Color{30, 20, 44, 255});
        DrawEllipse(int(c.x + 10), int(c.y), 46, 8, fade(Color{226, 52, 72, 255}, 0.35f + 0.2f * pulse_));
    }
}

// ------------------------------------------------------------- synthwave --

// Custom songs: the menus' night in the song's colour (stars, a striped sun,
// a grid floor running on the beat, glassy cubes turning in the air).
void Stage::drawSynthwave(float t) const {
    const Color tint = accent_;
    constexpr float horizon = 560;
    const Color skyTop = mix(Color{6, 4, 22, 255}, tint, 0.10f), skyLow = mix(Color{70, 18, 86, 255}, tint, 0.40f);
    gfx::gradientRect({kFull.x, kFull.y, kFull.width, horizon - kFull.y + 2}, skyTop, skyLow);
    for (const Star& s : stars_) {
        if (s.y > horizon - 90) continue;
        const float tw = 0.5f + 0.5f * std::sin(t * (1.0f + s.s * 0.25f) + s.ph);
        gfx::star({s.x, s.y}, s.s * (0.7f + 0.3f * tw + 0.25f * pulse_), fade(WHITE, 0.3f + 0.7f * tw));
    }
    const Vector2 sun{640, horizon - 40};
    const float sunR = 190 + 8 * pulse_;
    gfx::glow(sun, sunR * 1.9f, fade(mix(Color{255, 120, 170, 255}, tint, 0.3f), 0.35f));
    DrawCircleGradient(int(sun.x), int(sun.y), sunR, fade(Color{255, 226, 90, 255}, 0.9f), fade(mix(Color{255, 70, 150, 255}, tint, 0.25f), 0.9f));
    for (int k = 0; k < 6; ++k) {
        const float y = sun.y + 18 + float(k) * 24 + std::fmod(t * 8.0f, 24.0f);
        DrawRectangleRec({sun.x - sunR - 4, y, sunR * 2 + 8, 3 + float(k) * 2.4f},
                         mix(skyTop, skyLow, saturate((y - kFull.y) / (horizon - kFull.y))));
    }
    gfx::gradientRect({kFull.x, horizon, kFull.width, kFull.y + kFull.height - horizon}, mix(Color{24, 6, 36, 255}, tint, 0.22f),
                      Color{4, 2, 12, 255});
    const Color line = mix(Color{255, 80, 200, 255}, tint, 0.35f);
    BeginBlendMode(BLEND_ADDITIVE);
    const float run = std::fmod(t * 0.55f, 1.0f);
    for (int i = 0; i < 16; ++i) {
        const float z = float(i) + 1.0f - run;
        const float y = horizon + 320.0f / z - 8.0f;
        if (y < horizon || y > kFull.y + kFull.height) continue;
        DrawLineEx({kFull.x, y}, {kFull.x + kFull.width, y}, 1.5f + 1.5f / z, fade(line, std::min(0.75f, 0.12f + 0.9f / z) * (0.8f + 0.4f * pulse_)));
    }
    for (int i = -40; i <= 40; ++i)
        DrawLineEx({640.0f + float(i) * 40.0f, horizon}, {640.0f + float(i) * 330.0f, kFull.y + kFull.height}, 1.6f,
                   fade(line, 0.32f + 0.2f * pulse_));
    DrawLineEx({kFull.x, horizon}, {kFull.x + kFull.width, horizon}, 2.5f, fade(line, 0.9f));
    EndBlendMode();
    static const Color kCubeColors[5] = {{255, 79, 163, 255}, {40, 220, 255, 255}, {130, 255, 90, 255}, {170, 100, 255, 255}, {255, 222, 64, 255}};
    for (int i = 0; i < 7; ++i) {
        const float fi = float(i);
        const float x = std::fmod(fi * 380.0f + t * (9.0f + fi * 2.0f), 2700.0f) - 700.0f;
        const float y = 90.0f + std::fmod(fi * 97.0f, 260.0f) + std::sin(t * 0.7f + fi * 1.7f) * 14.0f;
        const float size = (50.0f + std::fmod(fi * 37.0f, 60.0f)) * (1.0f + 0.06f * pulse_);
        const float ax = t * (0.35f + 0.08f * fi) + fi, ay = t * (0.5f - 0.05f * fi) + fi * 2.0f;
        const Color c = mix(kCubeColors[i % 5], tint, 0.25f);
        gfx::cube({x, y}, size, ax, ay, c, 0.22f, true, horizon + 8.0f);
        gfx::cube({x, y}, size, ax, ay, c, 0.85f, false, horizon);
    }
}

void Stage::drawBack(double now) const {
    const float t = float(now);
    switch (kind_) {
        case 0: drawRooftop(t); break;
        case 1: drawClub(t); break;
        case 2: drawAlley(t); break;
        default: drawSynthwave(t); break;
    }
}

void Stage::drawFront(double now) const {
    motes_.draw(true);
    const float t = float(now);
    if (kind_ == 0) {
        speaker({-150, 560, 170, 200}, pulse_);
        speaker({1260, 560, 170, 200}, pulse_);
    } else if (kind_ == 1) {  // the crowd, jumping on the beat
        for (int i = 0; i < 16; ++i) {
            const float x = -220 + float(i) * 112.0f + float(i % 2) * 30;
            const float y = 716 + float(i % 3) * 12 - 14 * pulse_ * ((i + beat_) % 2 ? 1.0f : 0.35f);
            const Color body{14, 8, 26, 255};
            if (hash3(i, beat_ / 2, 1) % 3 == 0) DrawLineEx({x + 30, y + 40}, {x + 56, y - 56}, 18, body);
            DrawRectangleRounded({x - 46, y + 28, 92, 140}, 0.5f, 8, body);
            DrawCircleV({x, y}, 34, body);
            DrawRing({x, y}, 31, 35, 200, 340, 12, fade(i % 2 ? Color{255, 80, 200, 255} : Color{60, 220, 255, 255}, 0.6f));
        }
    } else if (kind_ == 2) {
        for (int i = 0; i < 8; ++i) {
            const float x = std::fmod(float(i) * 230.0f + t * 20.0f, 1900.0f) - 300;
            gfx::softCircle({x, 760}, 220, Color{40, 8, 20, 110});
        }
        amp({-110, 560, 170, 150});
        amp({1220, 560, 170, 150});
    }
}

void Stage::drawOverlay() const {
    if (flash_ > 0 && flashing_) DrawRectangleRec({0, 0, gfx::W, gfx::H}, Color{255, 220, 220, (unsigned char)(flash_ * 160)});
}

}  // namespace gh
