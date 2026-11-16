// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The built-in cast, drawn in code as cartoons with a thick dark outline:
//  - Hype (id "herald"), the player: a hype man in an orange beanie and a teal
//    hoodie, headphones round the neck, a microphone in hand;
//  - Principal P (id "principality"): a strict school principal with a violet
//    beehive, cat-eye glasses and a megaphone;
//  - DJ Hand (id "hand"): a cartoon glove in shades, floating over the decks;
//  - D-Mize (id "demise"): a punk skeleton with a red mohawk, a leather jacket
//    and a spiked collar;
//  - a Shade: Hype's outline in a song's colour, for songs with no cast.
// They have a glossy "Frutiger Aero" finish: every shape is lit from above
// with a glassy shine on its upper part, the outlines are a deep blue, the
// colours lean to aqua, lime and white, and soap bubbles drift around them
// (more pour out of a microphone while it sings).
// Each one is drawn facing right (+x) in its own units with the feet at (0, 0);
// a mirrored transform turns it to the left.

#include "characters.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

#include "common.hpp"
#include "gfx.hpp"

namespace gh {

using gfx::fade;
using gfx::mix;

CharId charFromName(const std::string& name) {
    const std::string n = toLower(name);
    if (n == "herald") return CharId::Herald;
    if (n == "principality") return CharId::Principality;
    if (n == "hand") return CharId::Hand;
    if (n == "demise") return CharId::Demise;
    return CharId::Shade;
}

const char* charDisplayName(CharId id) {
    switch (id) {
        case CharId::Herald: return "Hype";
        case CharId::Principality: return "Principal P";
        case CharId::Hand: return "DJ Hand";
        case CharId::Demise: return "D-Mize";
        default: return "Shade";
    }
}

Color charColor(CharId id) {
    switch (id) {
        case CharId::Herald: return Color{48, 182, 240, 255};        // Hype's hoodie
        case CharId::Principality: return Color{164, 100, 255, 255};  // the beehive
        case CharId::Hand: return Color{70, 170, 255, 255};          // the glove's cuff
        case CharId::Demise: return Color{255, 48, 84, 255};         // the mohawk
        default: return Color{180, 140, 210, 255};
    }
}

void CharacterState::sing(int dir, double now, double holdSec) {
    singDir = dir;
    singStart = now;
    singUntil = now + std::max(0.32, holdSec + 0.18);
    missUntil = -10;
}
void CharacterState::miss(int dir, double now) {
    singDir = dir;
    singStart = now;
    singUntil = now + 0.35;
    missUntil = now + 0.35;
}
void CharacterState::beat(double now) {
    if (now > singUntil) beatAt = now;
}

namespace {

constexpr Color kInk{18, 38, 76, 255};       // the outlines: a deep, glassy blue
constexpr Color kMissTone{96, 84, 170, 255};  // a miss turns the colours to this
constexpr float kLine = 4.2f;                 // outline width, in character units
constexpr float kPi = 3.14159265f;

struct Xf {
    Vector2 o{};
    float s = 1, sx = 1, sy = 1, rot = 0;
    bool flip = false;
    Vector2 operator()(float x, float y) const {
        if (flip) x = -x;
        x *= sx * s;
        y *= sy * s;
        const float c = std::cos(rot), n = std::sin(rot);
        return {o.x + x * c - y * n, o.y + x * n + y * c};
    }
    float len(float v) const { return v * s * (sx + sy) * 0.5f; }
};

void poly(const Xf& X, std::initializer_list<Vector2> pts, Color c) {
    std::vector<Vector2> v;
    v.reserve(pts.size());
    for (const Vector2& p : pts) v.push_back(X(p.x, p.y));
    gfx::convex(v, c);
}

void ell(const Xf& X, float cx, float cy, float rx, float ry, Color c, int seg = 30) {
    std::vector<Vector2> v;
    v.reserve(size_t(seg));
    for (int i = 0; i < seg; ++i) {
        const float a = float(i) / float(seg) * 2.0f * kPi;
        v.push_back(X(cx + std::cos(a) * rx, cy + std::sin(a) * ry));
    }
    gfx::convex(v, c);
}

void cap(const Xf& X, float ax, float ay, float bx, float by, float r, Color c) {
    gfx::capsule(X(ax, ay), X(bx, by), X.len(r), c);
}

void line(const Xf& X, float ax, float ay, float bx, float by, float w, Color c) {
    DrawLineEx(X(ax, ay), X(bx, by), X.len(w), c);
}

// ------------------------------------------------- the glossy Aero finish --

Color withAlpha(Color c, unsigned char a) {
    c.a = a;
    return c;
}

// A colour lit from above: t is 0 at a shape's top (lighter) and 1 at its
// bottom (deeper).
Color shadeAt(Color c, float t) {
    const Color hi = withAlpha(mix(c, WHITE, 0.45f), c.a), lo = withAlpha(mix(c, kInk, 0.3f), c.a);
    return t < 0.5f ? mix(hi, c, t * 2.0f) : mix(c, lo, (t - 0.5f) * 2.0f);
}

void ellLit(const Xf& X, float cx, float cy, float rx, float ry, Color c, int seg = 34) {
    const Vector2 o = X(cx, cy);
    Vector2 prev = X(cx + rx, cy);
    Color prevC = shadeAt(c, 0.5f);
    for (int i = 1; i <= seg; ++i) {
        const float a = float(i) / float(seg) * 2.0f * kPi;
        const Vector2 p = X(cx + std::cos(a) * rx, cy + std::sin(a) * ry);
        const Color pc = shadeAt(c, 0.5f + 0.5f * std::sin(a));
        gfx::triColors(o, prev, p, c, prevC, pc);
        prev = p;
        prevC = pc;
    }
}

void convexLit(const Xf& X, const std::vector<Vector2>& pts, Color c) {
    float y0 = 1e9f, y1 = -1e9f;
    for (const Vector2& q : pts) {
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y);
    }
    const float h = std::max(0.001f, y1 - y0);
    std::vector<Vector2> v;
    std::vector<Color> col;
    for (const Vector2& q : pts) {
        v.push_back(X(q.x, q.y));
        col.push_back(shadeAt(c, (q.y - y0) / h));
    }
    for (size_t i = 1; i + 1 < v.size(); ++i) gfx::triColors(v[0], v[i], v[i + 1], col[0], col[i], col[i + 1]);
}

// The glassy shine on a shape's upper part: white, fading out downwards.
void shineEll(const Xf& X, float cx, float cy, float rx, float ry) {
    const float sx = cx - rx * 0.16f, sy = cy - ry * 0.42f, srx = rx * 0.62f, sry = ry * 0.36f;
    auto white = [](float t) { return Color{255, 255, 255, (unsigned char)(std::max(0.0f, 1.0f - t) * 175.0f)}; };
    const Vector2 o = X(sx, sy);
    Vector2 prev = X(sx + srx, sy);
    Color prevC = white(0.5f);
    for (int i = 1; i <= 24; ++i) {
        const float a = float(i) / 24.0f * 2.0f * kPi;
        const Vector2 p = X(sx + std::cos(a) * srx, sy + std::sin(a) * sry);
        const Color pc = white(0.5f + 0.5f * std::sin(a));
        gfx::triColors(o, prev, p, white(0.5f), prevC, pc);
        prev = p;
        prevC = pc;
    }
}

void shinePoly(const Xf& X, const std::vector<Vector2>& pts) {
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const Vector2& q : pts) {
        x0 = std::min(x0, q.x);
        x1 = std::max(x1, q.x);
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y);
    }
    const float w = x1 - x0, h = y1 - y0;
    if (w < 22 || h < 22) return;
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    std::vector<Vector2> v;
    std::vector<Color> col;
    for (const Vector2& q : pts) {
        const float x = cx + (q.x - cx) * 0.8f, y = cy + (q.y - cy) * 0.8f - h * 0.08f;
        const float t = (y - y0) / h;
        v.push_back(X(x, y));
        col.push_back(Color{255, 255, 255, (unsigned char)(std::max(0.0f, 0.55f - t) / 0.55f * 150.0f)});
    }
    for (size_t i = 1; i + 1 < v.size(); ++i) gfx::triColors(v[0], v[i], v[i + 1], col[0], col[i], col[i + 1]);
}

// A tube's shine: a pale stripe along the side the light comes from.
void shineCap(const Xf& X, float ax, float ay, float bx, float by, float r) {
    const float dx = bx - ax, dy = by - ay;
    const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
    float nx = -dy / len, ny = dx / len;
    if (nx * -0.4f + ny * -0.9f < 0) {  // the light is up and to the left
        nx = -nx;
        ny = -ny;
    }
    const float o = r * 0.42f;
    cap(X, ax + nx * o, ay + ny * o, bx + nx * o, by + ny * o, r * 0.3f, Color{255, 255, 255, 80});
}

float hash01(float x) {
    const float s = std::sin(x) * 43758.5453f;
    return s - std::floor(s);
}

// A soap bubble: a clear film with a bright rim and a shine.
void bubble(Vector2 c, float r, float a) {
    if (a <= 0.01f || r < 0.5f) return;
    auto A = [&](float v) { return (unsigned char)std::clamp(v * a, 0.0f, 255.0f); };
    DrawCircleV(c, r, Color{170, 235, 255, A(36)});
    DrawRing(c, r * 0.8f, r, 0, 360, 28, Color{190, 245, 255, A(90)});
    DrawRing(c, r * 0.94f, r * 1.04f, 0, 360, 28, Color{255, 255, 255, A(170)});
    DrawCircleV({c.x - r * 0.38f, c.y - r * 0.38f}, r * 0.22f, Color{255, 255, 255, A(220)});
    DrawCircleV({c.x + r * 0.4f, c.y + r * 0.42f}, r * 0.08f, Color{255, 255, 255, A(150)});
}

// Bubbles rising from (x, y) across w for h units (the character's units);
// each one's place comes from the time alone.
void bubbles(const Xf& X, float x, float y, float w, float h, int n, double now, float speed, float seed) {
    for (int i = 0; i < n; ++i) {
        const float s = seed + float(i) * 7.31f;
        const float ph = std::fmod(float(now) * speed * (0.55f + 0.45f * hash01(s)) + hash01(s * 1.7f), 1.0f);
        const float bx = x + (hash01(s * 2.3f) - 0.5f) * w + std::sin(ph * 9.0f + s) * 6.0f;
        const float by = y - ph * h;
        const float r = 4.0f + 8.0f * hash01(s * 3.1f);
        bubble(X(bx, by), X.len(r), std::sin(ph * kPi));
    }
}

// A ring between radii r0 and r1 from angle a0 to a1; ry squashes it.
void arc(const Xf& X, float cx, float cy, float r0, float r1, float a0, float a1, Color c, float ry = 1.0f, int seg = 24) {
    for (int i = 0; i < seg; ++i) {
        const float b0 = a0 + (a1 - a0) * float(i) / float(seg), b1 = a0 + (a1 - a0) * float(i + 1) / float(seg);
        gfx::quad(X(cx + std::cos(b0) * r0, cy + std::sin(b0) * r0 * ry), X(cx + std::cos(b1) * r0, cy + std::sin(b1) * r0 * ry),
                  X(cx + std::cos(b1) * r1, cy + std::sin(b1) * r1 * ry), X(cx + std::cos(b0) * r1, cy + std::sin(b0) * r1 * ry), c);
    }
}

// The lower (or upper) half of an ellipse, as a convex outline.
std::vector<Vector2> halfEllipse(float cx, float cy, float rx, float ry, bool lower, int seg = 14) {
    std::vector<Vector2> v;
    for (int i = 0; i <= seg; ++i) {
        const float a = kPi * float(i) / float(seg);
        v.push_back({cx + std::cos(a) * rx, cy + (lower ? 1.0f : -1.0f) * std::sin(a) * ry});
    }
    return v;
}

// Shapes sharing one dark outline, the cartoon look: every outline is drawn
// first and the colours over them, so the line runs only round the whole.
class Ink {
public:
    explicit Ink(const Xf& X, float width = kLine, bool gloss = true) : X_(X), w_(width), gloss_(gloss) {}
    Ink& e(float cx, float cy, float rx, float ry, Color col) {
        items_.push_back({0, {cx, cy, rx, ry, 0}, {}, col});
        return *this;
    }
    Ink& c(float ax, float ay, float bx, float by, float r, Color col) {
        items_.push_back({1, {ax, ay, bx, by, r}, {}, col});
        return *this;
    }
    Ink& p(std::initializer_list<Vector2> pts, Color col) {
        items_.push_back({2, {}, std::vector<Vector2>(pts), col});
        return *this;
    }
    Ink& pv(std::vector<Vector2> pts, Color col) {
        items_.push_back({2, {}, std::move(pts), col});
        return *this;
    }
    void draw() const {
        for (const Item& it : items_) one(it, true);
        for (const Item& it : items_) one(it, false);
    }

private:
    struct Item {
        int kind;
        float f[5];
        std::vector<Vector2> pts;
        Color col;
    };
    void one(const Item& it, bool outline) const {
        const float o = outline ? w_ : 0.0f;
        const Color col = outline ? kInk : it.col;
        if (!outline && gloss_) {  // lit from above, with a shine
            switch (it.kind) {
                case 0:
                    ellLit(X_, it.f[0], it.f[1], it.f[2], it.f[3], col);
                    if (it.f[2] >= 9 && it.f[3] >= 9) shineEll(X_, it.f[0], it.f[1], it.f[2], it.f[3]);
                    break;
                case 1:
                    cap(X_, it.f[0], it.f[1], it.f[2], it.f[3], it.f[4], col);
                    if (it.f[4] >= 6) shineCap(X_, it.f[0], it.f[1], it.f[2], it.f[3], it.f[4]);
                    break;
                default:
                    convexLit(X_, it.pts, col);
                    shinePoly(X_, it.pts);
                    break;
            }
            return;
        }
        switch (it.kind) {
            case 0: ell(X_, it.f[0], it.f[1], it.f[2] + o, it.f[3] + o, col, 34); break;
            case 1: cap(X_, it.f[0], it.f[1], it.f[2], it.f[3], it.f[4] + o, col); break;
            default: {
                std::vector<Vector2> v;
                v.reserve(it.pts.size());
                for (const Vector2& q : it.pts) v.push_back(X_(q.x, q.y));
                if (!outline) {
                    gfx::convex(v, col);
                    break;
                }
                const float w = X_.len(o);
                for (size_t i = 0; i < v.size(); ++i) {
                    DrawLineEx(v[i], v[(i + 1) % v.size()], w * 2.0f, col);
                    DrawCircleV(v[i], w, col);
                }
                break;
            }
        }
    }
    Xf X_;
    float w_;
    bool gloss_;
    std::vector<Item> items_;
};

// An arm or a leg bent in the middle: `bend` pushes the joint to the right of
// the line from a to b (character units).
void limb(Ink& k, float ax, float ay, float bx, float by, float r, float bend, Color col) {
    const float dx = bx - ax, dy = by - ay;
    const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
    const float jx = (ax + bx) * 0.5f - dy / len * bend, jy = (ay + by) * 0.5f + dx / len * bend;
    k.c(ax, ay, jx, jy, r, col).c(jx, jy, bx, by, r * 0.9f, col);
}

struct Pose {
    float dx = 0, dy = 0, sx = 1, sy = 1, rot = 0, pop = 0;
    int dir = -1;
    bool singing = false, missing = false;
    float bop = 0;
};

Pose computePose(const CharacterState& s, double now) {
    Pose p;
    p.singing = now < s.singUntil && s.singDir >= 0;
    p.missing = now < s.missUntil;
    p.bop = float(std::exp(-(now - s.beatAt) * 7.0));
    if (p.singing) {
        const float pop = float(std::exp(-(now - s.singStart) * 12.0));
        p.pop = pop;
        p.dir = s.singDir;
        switch (s.singDir) {
            case 0: p.dx = -12 - 10 * pop; p.rot = -0.06f; break;
            case 3: p.dx = 12 + 10 * pop; p.rot = 0.06f; break;
            case 2: p.dy = -10 - 10 * pop; p.sy = 1.05f + 0.04f * pop; p.sx = 0.97f; break;
            case 1: p.dy = 8 + 6 * pop; p.sy = 0.92f - 0.03f * pop; p.sx = 1.05f; break;
            default: break;
        }
    } else {
        p.sy = 1.0f - 0.035f * p.bop;
        p.sx = 1.0f + 0.02f * p.bop;
    }
    return p;
}

// Which way a pose reaches, in the character's own terms: the screen's left
// and right are its front or its back depending on where it faces.
enum Slot { kIdle, kUp, kDown, kFront, kBack };
Slot slotOf(const Pose& P, bool flip) {
    if (!P.singing) return kIdle;
    switch (P.dir) {
        case 0: return flip ? kFront : kBack;
        case 3: return flip ? kBack : kFront;
        case 2: return kUp;
        case 1: return kDown;
        default: return kIdle;
    }
}

struct Face {
    float open = 0;                // mouth: 0 shut .. 1 wide
    float lookX = 0.3f, lookY = 0;  // where the eyes look (+x: forward)
    float blink = 1;               // 0 shut
    bool singing = false, miss = false;
    int dir = -1;
};

Face faceFor(const Pose& P, double now, bool flip, float phase) {
    Face f;
    f.singing = P.singing;
    f.miss = P.missing;
    f.dir = P.dir;
    if (P.singing && !P.missing) {
        f.open = std::min(1.0f, 0.45f + 0.5f * P.pop + 0.15f * std::fabs(std::sin(float(now) * 15.0f)));
        const float m = flip ? -1.0f : 1.0f;
        if (P.dir == 0) f.lookX = -m;
        if (P.dir == 3) f.lookX = m;
        if (P.dir == 2) f.lookY = -1;
        if (P.dir == 1) f.lookY = 1;
    } else if (P.missing) {
        f.open = 0.35f;
    }
    if (!P.singing && std::fmod(float(now) + phase, 3.9f) < 0.12f) f.blink = 0;
    return f;
}

// A cartoon eye: white, a pupil and a shine; a line when it blinks, and a
// squeezed chevron on a miss (side: +1 the near eye, -1 the far one).
void eye(const Xf& X, float cx, float cy, float rx, float ry, const Face& f, Color white, float side) {
    if (f.miss) {
        line(X, cx - side * rx, cy - ry * 0.5f, cx + side * rx * 0.6f, cy, 3.4f, kInk);
        line(X, cx + side * rx * 0.6f, cy, cx - side * rx, cy + ry * 0.5f, 3.4f, kInk);
        return;
    }
    if (f.blink < 0.5f) {
        line(X, cx - rx, cy + 1, cx + rx, cy + 1, 3.2f, kInk);
        return;
    }
    Ink(X, 3.0f).e(cx, cy, rx, ry, white).draw();
    const float px = cx + f.lookX * rx * 0.36f, py = cy + f.lookY * ry * 0.3f;
    ell(X, px, py, rx * 0.48f, ry * 0.5f, kInk);
    ell(X, px - rx * 0.18f, py - ry * 0.2f, rx * 0.17f, rx * 0.17f, WHITE);
}

// The mouth: shut (a smirk, a grin full of teeth, or a line of lipstick) or
// open with teeth and a tongue, round when singing up and flat when down.
void mouth(const Xf& X, float cx, float cy, float w, const Face& f, Color lips, bool grin) {
    const Color inside{70, 16, 40, 255};
    if (f.open < 0.01f) {
        if (grin) {
            Ink(X, 3.2f, false).pv(halfEllipse(cx, cy - 3, w * 1.2f, w * 0.75f, true), inside).draw();
            poly(X, {{cx - w * 1.02f, cy - 3}, {cx + w * 1.02f, cy - 3}, {cx + w * 0.9f, cy + w * 0.16f}, {cx - w * 0.9f, cy + w * 0.16f}},
                 WHITE);
        } else if (lips.a > 0) {
            line(X, cx - w, cy, cx + w * 0.8f, cy - 1, 4.4f, lips);
        } else {
            line(X, cx - w, cy, cx + w * 0.55f, cy, 3.4f, kInk);
            line(X, cx + w * 0.55f, cy, cx + w, cy - 4, 3.4f, kInk);
        }
        return;
    }
    const float rx = w * (0.72f + 0.28f * f.open) * (f.dir == 2 ? 0.82f : 1.0f);
    const float ry = w * (0.28f + 0.62f * f.open) * (f.dir == 1 ? 0.75f : 1.0f);
    if (lips.a > 0) {
        ell(X, cx, cy, rx + 3.2f, ry + 3.2f, lips);
        ell(X, cx, cy, rx, ry, inside);
    } else {
        Ink(X, 3.2f, false).e(cx, cy, rx, ry, inside).draw();
    }
    ell(X, cx, cy - ry * 0.55f, rx * 0.55f, ry * 0.25f, Color{250, 250, 245, 255});
    ell(X, cx + rx * 0.1f, cy + ry * 0.55f, rx * 0.55f, ry * 0.32f, Color{255, 110, 130, 255});
}

void sweat(const Xf& X, float x, float y) {
    const Color drop{150, 220, 255, 255};
    Ink(X, 2.6f).e(x, y + 5, 5.5f, 6.5f, drop).p({{x - 4.6f, y + 3}, {x + 4.6f, y + 3}, {x, y - 9}}, drop).draw();
    ell(X, x - 1.8f, y + 3, 1.6f, 2.2f, WHITE);
}

// A microphone held at (hx, hy), pointing along (dx, dy).
void mic(const Xf& X, float hx, float hy, float dx, float dy, Color grille) {
    const float l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
    dx /= l;
    dy /= l;
    const float gx = hx + dx * 36, gy = hy + dy * 36;
    Ink(X, 3.4f).c(hx - dx * 6, hy - dy * 6, hx + dx * 26, hy + dy * 26, 5.5f, Color{48, 46, 62, 255}).e(gx, gy, 11.5f, 11.5f, grille).draw();
    line(X, gx - dy * 10, gy + dx * 10, gx + dy * 10, gy - dx * 10, 1.6f, fade(kInk, 0.45f));
    line(X, gx - dx * 10, gy - dy * 10, gx + dx * 10, gy + dy * 10, 1.6f, fade(kInk, 0.45f));
    ell(X, gx - 3.5f, gy - 3.5f, 3.2f, 3.2f, fade(WHITE, 0.75f));
}

// ------------------------------------------------------------------- Hype --

struct HypeLook {
    Color skin, skinShade, hair, beanie, beanieDark, pom, hoodie, hoodieDark, pants, shoe, sole, phones, eyeWhite;
    bool shade = false;
    Color glow = WHITE;
};

HypeLook hypeLook(bool shade, Color tint, bool missing) {
    HypeLook k{};
    k.skin = {186, 120, 78, 255};
    k.skinShade = {150, 92, 60, 255};
    k.hair = {46, 30, 36, 255};
    k.beanie = {130, 214, 50, 255};
    k.beanieDark = {80, 156, 28, 255};
    k.pom = {242, 250, 255, 255};
    k.hoodie = {48, 182, 240, 255};
    k.hoodieDark = {26, 128, 196, 255};
    k.pants = {46, 70, 128, 255};
    k.shoe = {248, 250, 255, 255};
    k.sole = {130, 214, 50, 255};
    k.phones = {120, 220, 250, 255};
    k.eyeWhite = WHITE;
    Color* all[] = {&k.skin, &k.skinShade, &k.hair, &k.beanie, &k.beanieDark, &k.pom, &k.hoodie,
                    &k.hoodieDark, &k.pants, &k.shoe, &k.sole, &k.phones};
    if (shade) {
        k.shade = true;
        k.glow = mix(tint, WHITE, 0.55f);
        for (Color* c : all) *c = mix(mix(*c, tint, 0.3f), Color{12, 8, 20, 255}, 0.62f);
        k.eyeWhite = k.glow;
    }
    if (missing)
        for (Color* c : all) *c = mix(*c, kMissTone, 0.38f);
    return k;
}

// The head, around its centre.
void hypeHead(const Xf& H, const Face& f, const HypeLook& k) {
    // hair poking out at the back, under the beanie
    Ink(H).p({{-50, -18}, {-34, -22}, {-42, 4}}, k.hair).p({{-48, -2}, {-34, -8}, {-40, 18}}, k.hair).draw();
    // head and nose, then the ear
    Ink(H).e(0, 2, 44, 42, k.skin).e(42, 16, 9, 8, k.skin).draw();
    Ink(H, 3.0f).e(-18, 10, 9, 11, k.skin).draw();
    ell(H, -18, 10, 4, 6, k.skinShade);
    // the beanie: pompom and dome, ribs, folded cuff
    std::vector<Vector2> dome;
    for (int i = 0; i <= 18; ++i) {
        const float a = kPi + kPi * float(i) / 18.0f;
        dome.push_back({-3 + std::cos(a) * 49, -22 + std::sin(a) * 44});
    }
    Ink(H).e(-12, -68, 14, 13, k.pom).pv(dome, k.beanie).draw();
    for (int i = -2; i <= 2; ++i)
        line(H, -3 + float(i) * 15.0f, -30, -3 + float(i) * 9.0f, -58 + float(std::abs(i)) * 6.0f, 2.4f, k.beanieDark);
    Ink(H).p({{-54, -32}, {49, -32}, {51, -15}, {-55, -13}}, k.beanieDark).draw();
    for (int i = 0; i < 7; ++i) line(H, -46 + float(i) * 14.0f, -29, -46 + float(i) * 14.0f, -17, 2.0f, fade(k.beanie, 0.75f));
    // brows: easy-going, up when singing, worried on a miss
    if (f.miss) {
        line(H, 5, -4, 21, -11, 3.8f, k.hair);
        line(H, 27, -11, 40, -6, 3.8f, k.hair);
    } else {
        const float up = f.singing ? -3.0f : 0.0f;
        line(H, 4, -7 + up, 21, -10 + up, 3.8f, k.hair);
        line(H, 27, -10 + up, 40, -7 + up, 3.8f, k.hair);
    }
    // eyes: the near one bigger (three-quarter view)
    if (k.shade) {
        for (int i = 0; i < 2; ++i) {
            const float x = i ? 32.0f : 13.0f, y = i ? 10.0f : 11.0f, r = i ? 7.0f : 8.5f;
            ell(H, x, y, r, f.blink < 0.5f || f.miss ? 1.8f : r * 1.25f, k.glow);
            gfx::glow(H(x, y), H.len(30), fade(k.glow, 0.5f));
        }
    } else {
        eye(H, 13, 11, 9.5f, 12.5f, f, k.eyeWhite, 1);
        eye(H, 32, 10, 8.0f, 11.0f, f, k.eyeWhite, -1);
    }
    mouth(H, 23, 32, 11, f, BLANK, false);
    if (f.miss) sweat(H, -30, -4);
}

void drawHype(const CharacterState& s, const Pose& P, Vector2 feet, float sc, double now, bool flip, bool shade) {
    const HypeLook k = hypeLook(shade, s.tint, P.missing);
    Xf base{feet, sc};
    ell(base, 0, 0, 66, 11, fade(BLACK, 0.3f));
    if (shade) gfx::glow({feet.x, feet.y - 150 * sc}, 220 * sc, fade(s.tint, 0.28f + 0.2f * P.bop));
    bubbles(base, 0, -30, 220, 320, 7, now, 0.16f, 1.0f);
    Xf X{{feet.x + P.dx * sc, feet.y + P.dy * sc}, sc, P.sx, P.sy, P.rot, flip};
    const Slot slot = slotOf(P, flip);
    const float sway = P.singing ? 0.0f : std::sin(float(now) * 2.0f) * 2.0f;

    // the far arm, behind the body
    static const Vector2 kFar[5] = {{-30, -104}, {-52, -232}, {-36, -96}, {-6, -128}, {-84, -150}};
    static const float kFarBend[5] = {10, -10, 10, 8, -10};
    const Vector2 fh{kFar[slot].x, kFar[slot].y + sway};
    {
        Ink a(X);
        limb(a, -22, -164, fh.x, fh.y, 12, kFarBend[slot], mix(k.hoodie, kInk, 0.22f));
        a.draw();
        Ink(X).e(fh.x, fh.y, 10, 9, k.skinShade).draw();
    }
    // legs and sneakers
    {
        Ink legs(X);
        limb(legs, -14, -90, -20, -26, 14.5f, -4, k.pants);
        limb(legs, 12, -90, 20, -26, 15, 4, k.pants);
        legs.draw();
    }
    for (float x : {-16.0f, 26.0f}) {
        Ink(X).e(x, -15, 25, 13, k.shoe).p({{x - 25, -10}, {x + 27, -10}, {x + 27, 0}, {x - 25, 0}}, k.sole).draw();
        line(X, x - 14, -19, x + 10, -14, 2.6f, k.sole);
    }
    // the hoodie: hood on the back, body, shoulders, waistband, pocket
    Ink(X)
        .e(-30, -182, 26, 16, k.hoodieDark)
        .p({{-40, -178}, {36, -178}, {46, -88}, {-44, -88}}, k.hoodie)
        .e(-36, -166, 15, 15, k.hoodie)
        .e(32, -166, 15, 15, k.hoodie)
        .e(1, -90, 46, 10, k.hoodie)
        .draw();
    Ink(X, 2.6f).p({{-24, -130}, {26, -130}, {32, -102}, {-30, -102}}, k.hoodieDark).draw();
    line(X, -44, -96, 46, -96, 2.4f, k.hoodieDark);
    // headphones round the neck: the band behind, the neck, the cups
    cap(X, -22, -186, 26, -188, 5.5f, mix(k.phones, kInk, 0.45f));
    Ink(X).c(2, -178, 4, -202, 12, k.skin).draw();
    Ink(X).e(-22, -184, 11, 14, k.phones).e(28, -186, 12, 15, k.phones).draw();
    ell(X, -22, -184, 5, 8, mix(k.phones, kInk, 0.4f));
    ell(X, 28, -186, 5.5f, 8.5f, mix(k.phones, kInk, 0.4f));
    line(X, -4, -170, -8, -140, 2.2f, k.shoe);
    line(X, 10, -170, 13, -142, 2.2f, k.shoe);
    // the head bobs on the beat
    Xf H = X;
    H.o = X(6, -232 + 3.0f * P.bop);
    hypeHead(H, faceFor(P, now, flip, 0.7f), k);
    // the near arm and the microphone
    static const Vector2 kNear[5] = {{36, -124}, {80, -252}, {44, -96}, {102, -178}, {-60, -164}};
    static const Vector2 kMicDir[5] = {{0.25f, -1}, {0.3f, -1}, {0.35f, 1}, {1, -0.2f}, {-1, -0.3f}};
    static const float kNearBend[5] = {12, 12, 12, 12, -12};
    const Vector2 nh{kNear[slot].x, kNear[slot].y + sway};
    {
        Ink a(X);
        limb(a, 16, -168, nh.x, nh.y, 12.5f, kNearBend[slot], k.hoodie);
        a.draw();
    }
    mic(X, nh.x, nh.y, kMicDir[slot].x, kMicDir[slot].y, Color{206, 212, 228, 255});
    Ink(X).e(nh.x, nh.y, 11, 10, k.skin).draw();
    if (P.singing && !P.missing)  // the voice comes out as bubbles
        bubbles(X, nh.x + kMicDir[slot].x * 40, nh.y + kMicDir[slot].y * 30, 34, 110, 4, now, 0.9f, 11.0f);
}

// ------------------------------------------------------------ Principal P --

void principalHead(const Xf& H, const Face& f, bool missing) {
    auto tone = [&](Color c) { return missing ? mix(c, kMissTone, 0.38f) : c; };
    const Color hair = tone({156, 96, 255, 255}), hairHi = tone({198, 158, 255, 255}), hairDark = tone({96, 48, 188, 255});
    const Color skin = tone({246, 206, 178, 255});
    // the beehive, behind the head
    Ink(H).e(-10, -52, 34, 42, hair).e(-6, -12, 40, 32, hair).e(-36, 10, 12, 16, hair).draw();
    arc(H, -10, -54, 12, 17, 3.3f, 5.7f, hairHi, 1.0f, 16);
    arc(H, -10, -44, 22, 26, 3.5f, 5.2f, hairHi, 1.0f, 16);
    // the face: a pointed nose, a firm chin
    Ink(H).e(4, 6, 31, 37, skin).p({{28, -2}, {50, 16}, {30, 20}}, skin).p({{-6, 34}, {32, 22}, {30, 36}, {14, 44}}, skin).draw();
    // a gold hoop earring
    arc(H, -16, 28, 4.5f, 7.5f, 0, 2 * kPi, Color{255, 206, 60, 255}, 1.25f, 18);
    // cat-eye glasses
    const Color lens{216, 246, 255, 255};
    Ink(H, 3.8f).e(14, 6, 11.5f, 8.5f, lens).e(36, 5, 9.5f, 7.5f, lens).draw();
    poly(H, {{0, 2}, {6, -6}, {-4, -8}}, kInk);
    if (f.blink < 0.5f) {
        line(H, 7, 7, 21, 7, 2.6f, kInk);
        line(H, 30, 6, 42, 6, 2.6f, kInk);
    } else {
        const float pr = f.miss ? 2.0f : 3.4f;
        ell(H, 14 + f.lookX * 4, 7 + f.lookY * 2.5f, pr, pr * 1.15f, kInk);
        ell(H, 36 + f.lookX * 3.5f, 6 + f.lookY * 2.2f, pr * 0.9f, pr * 1.05f, kInk);
    }
    line(H, 8, 3, 13, -1, 2.0f, fade(WHITE, 0.9f));
    // brows: stern, down toward the nose; up on a miss
    if (f.miss) {
        line(H, 4, -10, 20, -15, 3.8f, hairDark);
        line(H, 30, -15, 44, -11, 3.8f, hairDark);
    } else {
        const float r = f.singing ? 2.5f : 0.0f;
        line(H, 3, -12 - r, 22, -6, 3.8f, hairDark);
        line(H, 29, -6, 45, -11 - r, 3.8f, hairDark);
    }
    mouth(H, 23, 27, 9, f, tone({224, 40, 84, 255}), false);
    if (f.miss) sweat(H, -22, -14);
}

// A megaphone: its narrow end, where it points, and the side its handle hangs.
struct Horn {
    Vector2 from, d, side;
};

Horn hornOf(Vector2 from, Vector2 dir) {
    const float l = std::max(0.001f, std::sqrt(dir.x * dir.x + dir.y * dir.y));
    Horn h{from, {dir.x / l, dir.y / l}, {}};
    h.side = {-h.d.y, h.d.x};
    if (h.side.y < 0) h.side = {-h.side.x, -h.side.y};  // the handle hangs below
    return h;
}

Vector2 hornGrip(const Horn& h) { return {h.from.x + h.d.x * 16 + h.side.x * 20, h.from.y + h.d.y * 16 + h.side.y * 20}; }

void drawHorn(const Xf& X, const Horn& h, bool loud, double now) {
    const float L = 58, narrow = 7, wide = 25;
    auto at = [&](float t, float sgn) {
        const float hw = narrow + (wide - narrow) * t;
        return Vector2{h.from.x + h.d.x * L * t + h.side.x * hw * sgn, h.from.y + h.d.y * L * t + h.side.y * hw * sgn};
    };
    const Vector2 g = hornGrip(h);
    Ink(X, 3.2f).c(h.from.x + h.d.x * 14 + h.side.x * 6, h.from.y + h.d.y * 14 + h.side.y * 6, g.x, g.y, 4.5f, Color{60, 58, 76, 255}).draw();
    Ink(X).p({at(0, -1), at(0, 1), at(1, 1), at(1, -1)}, Color{248, 248, 252, 255}).draw();
    poly(X, {at(0.6f, -1), at(0.6f, 1), at(0.78f, 1), at(0.78f, -1)}, Color{60, 200, 255, 255});
    const Vector2 r0 = at(1, -1), r1 = at(1, 1);
    line(X, r0.x, r0.y, r1.x, r1.y, 5, Color{196, 196, 212, 255});
    if (loud) {  // shout lines
        const float a0 = std::atan2(h.d.y, h.d.x);
        for (int i = -1; i <= 1; ++i) {
            const float a = a0 + float(i) * 0.4f;
            const float n = 70 + std::fmod(float(now) * 60.0f + float(i + 1) * 7.0f, 14.0f);
            line(X, h.from.x + std::cos(a) * n, h.from.y + std::sin(a) * n, h.from.x + std::cos(a) * (n + 14),
                 h.from.y + std::sin(a) * (n + 14), 3, kInk);
        }
    }
}

void drawPrincipal(const CharacterState& s, const Pose& P, Vector2 feet, float sc, double now, bool flip) {
    auto tone = [&](Color c) { return P.missing ? mix(c, kMissTone, 0.38f) : c; };
    const Color blazer = tone({228, 66, 148, 255}), blazerDark = tone({170, 36, 106, 255});
    const Color skirt = tone({98, 36, 94, 255}), legs = tone({72, 52, 96, 255}), shoes = tone({168, 28, 70, 255});
    const Color shirt = tone({250, 250, 252, 255}), tie = tone({255, 208, 44, 255}), skin = tone({246, 206, 178, 255});
    Xf base{feet, sc};
    ell(base, 0, 0, 52, 9, fade(BLACK, 0.3f));
    bubbles(base, 0, -30, 200, 400, 6, now, 0.14f, 2.0f);
    Xf X{{feet.x + P.dx * sc, feet.y + P.dy * sc}, sc, P.sx, P.sy, P.rot, flip};
    const Slot slot = slotOf(P, flip);
    // the far arm: a hand on the hip when quiet
    static const Vector2 kFar[5] = {{-38, -168}, {-46, -330}, {-42, -150}, {-8, -206}, {-96, -240}};
    static const float kFarBend[5] = {26, -8, 20, 12, -10};
    {
        Ink a(X);
        limb(a, -28, -246, kFar[slot].x, kFar[slot].y, 9.5f, kFarBend[slot], blazerDark);
        a.draw();
        Ink(X).e(kFar[slot].x, kFar[slot].y, 8, 7.5f, skin).draw();
    }
    // legs and heels
    {
        Ink l(X);
        limb(l, -9, -120, -13, -22, 7, -3, legs);
        limb(l, 9, -120, 13, -22, 7.5f, 3, legs);
        l.draw();
    }
    for (float x : {-13.0f, 13.0f})
        Ink(X, 3.4f).p({{x - 8, -26}, {x + 12, -20}, {x + 22, -6}, {x - 8, -6}}, shoes).c(x - 6, -8, x - 7, 0, 2.6f, shoes).draw();
    // the pencil skirt and the blazer
    Ink(X).p({{-30, -172}, {30, -172}, {26, -108}, {-26, -108}}, skirt).draw();
    Ink(X).p({{-36, -254}, {34, -254}, {32, -160}, {-34, -160}}, blazer).e(-34, -246, 13, 10, blazer).e(32, -246, 13, 10, blazer).draw();
    poly(X, {{-12, -254}, {12, -254}, {0, -216}}, shirt);
    Ink(X, 2.4f).p({{-3.5f, -250}, {3.5f, -250}, {5, -220}, {0, -212}, {-5, -220}}, tie).draw();
    Ink(X, 2.6f).p({{-15, -254}, {-4, -222}, {-21, -238}}, blazerDark).p({{15, -254}, {4, -222}, {21, -238}}, blazerDark).draw();
    for (int i = 0; i < 2; ++i) ell(X, 12, -198 + float(i) * 16.0f, 2.6f, 2.6f, Color{255, 208, 60, 255});
    poly(X, {{18, -236}, {30, -236}, {24, -228}}, Color{80, 230, 255, 255});
    // neck and head
    Ink(X).c(2, -252, 4, -270, 8, skin).draw();
    Xf H = X;
    H.o = X(6, -298 + 3.0f * P.bop);
    principalHead(H, faceFor(P, now, flip, 1.9f), P.missing);
    // the near arm and the megaphone
    static const Vector2 kFrom[5] = {{46, -190}, {46, -284}, {44, -254}, {48, -268}, {-60, -262}};
    static const Vector2 kDir[5] = {{0.38f, 0.92f}, {0.45f, -0.9f}, {0.72f, 0.7f}, {1, -0.08f}, {-1, -0.08f}};
    static const float kBend[5] = {14, 10, 12, 12, -12};
    const Horn horn = hornOf(kFrom[slot], kDir[slot]);
    const Vector2 grip = hornGrip(horn);
    {
        Ink a(X);
        limb(a, 22, -244, grip.x, grip.y, 9.5f, kBend[slot], blazer);
        a.draw();
    }
    drawHorn(X, horn, P.singing && !P.missing, now);
    Ink(X).e(grip.x, grip.y, 8, 7.5f, skin).draw();
    if (P.singing && !P.missing)
        bubbles(X, horn.from.x + horn.d.x * 70, horn.from.y + horn.d.y * 70, 40, 110, 4, now, 0.9f, 12.0f);
}

// ---------------------------------------------------------------- DJ Hand --

struct GloveLook {
    Color glove, cuff, shades;
};

GloveLook gloveLook(bool missing) {
    auto tone = [&](Color c) { return missing ? mix(c, kMissTone, 0.38f) : c; };
    return {tone({252, 252, 255, 255}), tone({70, 170, 255, 255}), tone({22, 20, 32, 255})};
}

// The glove around the middle of its back: the face is drawn there.
void djGlove(const Xf& G, const Face& f, const GloveLook& c, float spread, float curl, bool point, double now) {
    Ink(G).e(0, 76, 46, 15, c.cuff).draw();  // the rolled cuff
    Ink k(G);
    k.p({{-34, 28}, {34, 28}, {40, 68}, {-40, 68}}, c.glove);
    k.e(0, 0, 58, 60, c.glove);
    static const float kLen[4] = {50, 66, 70, 58};
    float baseX[4], tipX[4], tipY[4];
    for (int i = 0; i < 4; ++i) {
        const float bx = -39.0f + float(i) * 26.0f;
        float len = kLen[i] * curl;
        if (point) len = i == 3 ? kLen[i] * 1.12f : kLen[i] * 0.42f;  // the index finger points
        const float wig = std::sin(float(now) * 3.0f + float(i) * 1.3f) * 2.5f;
        baseX[i] = bx;
        tipX[i] = bx + (bx / 39.0f) * 15.0f * spread + wig;
        tipY[i] = -42 - len;
        k.c(bx, -38, tipX[i], tipY[i], 14.5f, c.glove);
    }
    k.c(46, 8, 80, -20, 14.5f, c.glove);  // thumb
    k.draw();
    for (int i = 0; i < 3; ++i) {  // the lines between the fingers
        const float mx = (baseX[i] + baseX[i + 1]) * 0.5f;
        const float py = -60.0f * std::sqrt(std::max(0.0f, 1.0f - (mx / 58.0f) * (mx / 58.0f)));
        const float ty = std::max(tipY[i], tipY[i + 1]) + 16;
        if (ty < py - 2) line(G, mx, py, (tipX[i] + tipX[i + 1]) * 0.5f, ty, 2.6f, kInk);
    }
    for (int i = -4; i <= 4; ++i)  // a gold chain on the cuff
        Ink(G, 2.2f).e(float(i) * 9.5f, 78 - float(std::abs(i)) * 0.6f, 4.4f, 3.6f, Color{255, 208, 60, 255}).draw();
    // the face, a little forward (three-quarter view): shades and a grin
    const float fx = 6;
    Ink(G, 3.4f)
        .p({{fx - 42, -20}, {fx - 4, -20}, {fx - 8, 2}, {fx - 38, 0}}, c.shades)
        .p({{fx + 6, -20}, {fx + 44, -20}, {fx + 40, 0}, {fx + 10, 2}}, c.shades)
        .c(fx - 4, -16, fx + 6, -16, 3.2f, c.shades)
        .draw();
    line(G, fx - 32, -14, fx - 22, -5, 2.6f, fade(WHITE, 0.8f));
    line(G, fx + 16, -14, fx + 26, -5, 2.6f, fade(WHITE, 0.8f));
    if (f.miss) {
        line(G, fx - 36, -26, fx - 10, -34, 3.8f, kInk);
        line(G, fx + 12, -34, fx + 40, -27, 3.8f, kInk);
    } else if (f.singing) {
        line(G, fx - 36, -31, fx - 8, -31, 3.8f, kInk);
        line(G, fx + 10, -31, fx + 40, -33, 3.8f, kInk);
    }
    mouth(G, fx + 2, 28, 16, f, BLANK, true);
    if (f.miss) sweat(G, fx + 46, -36);
}

void drawHand(const CharacterState& s, const Pose& P, Vector2 feet, float sc, double now, bool flip) {
    const GloveLook look = gloveLook(P.missing);
    Xf base{feet, sc};
    ell(base, 0, 0, 96, 13, fade(BLACK, 0.32f));
    // the DJ booth stays on the floor
    Xf B{feet, sc, 1, 1, 0, flip};
    Ink(B).p({{-84, -98}, {84, -98}, {76, 0}, {-76, 0}}, Color{36, 104, 156, 255}).draw();
    line(B, -78, -74, 78, -74, 4, look.cuff);
    gfx::glow(B(0, -74), 110 * sc, fade(look.cuff, 0.25f + 0.25f * P.bop));
    for (float x : {-42.0f, 42.0f}) {
        const float pump = 1.0f + 0.12f * P.bop;
        Ink(B, 3.0f).e(x, -36, 20 * pump, 20 * pump, Color{196, 210, 230, 255}).draw();
        ell(B, x, -36, 9 * pump, 9 * pump, Color{20, 40, 70, 255});
        ell(B, x - 4, -40, 3, 3, fade(WHITE, 0.4f));
    }
    Ink(B).p({{-92, -112}, {92, -112}, {86, -96}, {-86, -96}}, Color{200, 214, 232, 255}).draw();
    // the record spins, and scratches while the Hand sings
    const float spin = float(now) * 4.0f + (P.singing ? std::sin(float(now - s.singStart) * 22.0f) * 1.4f : 0.0f);
    Ink(B, 3.0f).e(-32, -118, 40, 10, Color{20, 18, 28, 255}).draw();
    ell(B, -32, -118, 12, 3.2f, Color{130, 214, 50, 255});
    line(B, -32 + std::cos(spin) * 16, -118 + std::sin(spin) * 4, -32 + std::cos(spin) * 36, -118 + std::sin(spin) * 9, 2,
         fade(WHITE, 0.4f));
    line(B, 24, -124, -2, -118, 3, Color{214, 218, 234, 255});
    for (int i = 0; i < 3; ++i)
        Ink(B, 2.2f).e(44 + float(i) * 13.0f, -116, 4.2f, 2.6f, i == 1 ? look.cuff : Color{232, 232, 242, 255}).draw();
    // the glove floats over the decks
    const float hover = std::sin(float(now) * 1.8f) * 8.0f;
    float lift = 0, spread = 1, curl = 1, tilt = 0;
    bool point = false;
    if (P.singing) {
        if (P.dir == 0) { tilt = -0.32f; point = true; }
        if (P.dir == 3) { tilt = 0.32f; point = true; }
        if (P.dir == 2) { lift = -24; spread = 1.5f; }
        if (P.dir == 1) { lift = 16; curl = 0.5f; }
    }
    bubbles(B, 0, -70, 160, 330, 8, now, 0.22f, 3.0f);  // the speakers blow bubbles
    Xf G{{feet.x + P.dx * sc * 0.6f, feet.y + (-236 + hover + lift + P.dy * 0.5f) * sc}, sc, P.sx, P.sy, P.rot + tilt, flip};
    gfx::glow(G(0, 0), 170 * sc, fade(look.cuff, 0.18f + 0.18f * P.bop));
    djGlove(G, faceFor(P, now, flip, 2.6f), look, spread, curl, point, now);
}

// ----------------------------------------------------------------- D-Mize --

struct SkullLook {
    Color bone, boneShade, mohawk, mohawkDark, socket, eyes, teeth;
};

SkullLook skullLook(bool missing) {
    auto tone = [&](Color c) { return missing ? mix(c, kMissTone, 0.38f) : c; };
    return {tone({242, 234, 212, 255}), tone({200, 190, 166, 255}), tone({255, 48, 84, 255}), tone({186, 22, 56, 255}),
            Color{20, 10, 22, 255},     Color{255, 60, 80, 255},     tone({252, 250, 240, 255})};
}

void dmizeHead(const Xf& H, const Face& f, const SkullLook& c, bool glow) {
    // the mohawk: spikes fanning from the back over the top, behind the skull
    {
        Ink k(H);
        static const float kH[5] = {30, 48, 60, 54, 38};
        for (int i = 0; i < 5; ++i) {
            const float a = kPi * (1.12f + 0.19f * float(i));
            const float ca = std::cos(a), sa = std::sin(a);
            const float bx = ca * 32, by = sa * 32 - 4;
            k.p({{bx - sa * 13, by + ca * 13}, {bx + sa * 13, by - ca * 13}, {ca * (32 + kH[i]) - 6, sa * (32 + kH[i]) - 4}}, c.mohawk);
        }
        k.draw();
        for (int i = 0; i < 5; ++i) {
            const float a = kPi * (1.12f + 0.19f * float(i));
            const float ca = std::cos(a), sa = std::sin(a);
            line(H, ca * 30, sa * 30 - 4, ca * (30 + kH[i] * 0.7f) - 4, sa * (30 + kH[i] * 0.7f) - 4, 4, c.mohawkDark);
        }
    }
    // the skull: cranium, cheekbones and the upper jaw under one outline
    Ink(H).e(0, 0, 42, 40, c.bone).e(14, 18, 26, 16, c.bone).p({{0, 18}, {44, 14}, {46, 34}, {2, 36}}, c.bone).draw();
    ell(H, -16, 10, 12, 16, c.boneShade);
    line(H, -22, -30, -12, -18, 2.4f, kInk);  // a crack
    line(H, -12, -18, -18, -8, 2.4f, kInk);
    // the sockets and the eye-lights in them
    ell(H, 10, 2, 12, 14, c.socket);
    ell(H, 33, 1, 9.5f, 12.5f, c.socket);
    if (f.blink >= 0.5f) {
        const float ex = (f.lookX - 0.3f) * 3.0f, ey = f.lookY * 3.0f;
        const float r = f.miss ? 2.4f : 4.4f;
        ell(H, 11 + ex, 4 + ey, r, r * 1.1f, c.eyes);
        ell(H, 34 + ex, 3 + ey, r * 0.85f, r * 0.95f, c.eyes);
        if (glow) {
            gfx::glow(H(11 + ex, 4 + ey), H.len(24), fade(c.eyes, 0.55f));
            gfx::glow(H(34 + ex, 3 + ey), H.len(20), fade(c.eyes, 0.5f));
        }
    }
    // brow ridges: angry when singing, up on a miss
    if (f.miss) {
        line(H, -2, -14, 18, -19, 4, kInk);
        line(H, 26, -19, 44, -13, 4, kInk);
    } else {
        const float r = f.singing ? 3.0f : 0.0f;
        line(H, -2, -18, 20, -12 + r, 4, kInk);
        line(H, 25, -12 + r, 44, -17, 4, kInk);
    }
    poly(H, {{22, 14}, {30, 14}, {26, 23}}, c.socket);  // the nose
    // teeth, and the jaw that drops as the skeleton sings
    const float o = f.open * 14.0f;
    if (o > 0.5f) poly(H, {{4, 31}, {42, 29}, {40, 37 + o}, {6, 38 + o}}, Color{60, 8, 24, 255});
    poly(H, {{4, 24}, {42, 22}, {42, 31}, {4, 32}}, c.teeth);
    for (int i = 1; i < 6; ++i) line(H, 4 + float(i) * 6.4f, 23, 4 + float(i) * 6.4f, 31, 1.6f, kInk);
    Ink(H).p({{-2, 37 + o}, {44, 34 + o}, {42, 49 + o}, {10, 56 + o}, {-4, 47 + o}}, c.bone).draw();
    poly(H, {{4, 37 + o}, {40, 35 + o}, {40, 41 + o}, {4, 43 + o}}, c.teeth);
    for (int i = 1; i < 6; ++i) line(H, 4 + float(i) * 6.4f, 37 + o, 4 + float(i) * 6.4f, 42 + o, 1.6f, kInk);
}

// A jacket sleeve to the elbow, a bone forearm and a bony hand.
void boneArm(const Xf& X, float ax, float ay, float bx, float by, float bend, Color sleeve, Color bone) {
    const float dx = bx - ax, dy = by - ay;
    const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
    const float jx = (ax + bx) * 0.5f - dy / len * bend, jy = (ay + by) * 0.5f + dx / len * bend;
    Ink(X, 3.4f).c(jx, jy, bx, by, 5.0f, bone).draw();
    Ink(X).c(ax, ay, jx, jy, 13.0f, sleeve).draw();
    const float a0 = std::atan2(by - jy, bx - jx);
    Ink k(X, 3.0f);
    for (int i = -1; i <= 1; ++i) {
        const float a = a0 + float(i) * 0.45f;
        k.c(bx, by, bx + std::cos(a) * 15, by + std::sin(a) * 15, 2.8f, bone);
    }
    k.e(bx, by, 7.5f, 7, bone);
    k.draw();
}

void drawDemise(const CharacterState& s, const Pose& P, Vector2 feet, float sc, double now, bool flip) {
    auto tone = [&](Color c) { return P.missing ? mix(c, kMissTone, 0.38f) : c; };
    const Color jacket = tone({42, 38, 54, 255}), jacketHi = tone({92, 86, 112, 255}), tee = tone({232, 38, 66, 255});
    const Color jeans = tone({40, 50, 84, 255}), boots = tone({30, 26, 38, 255});
    const Color silver{208, 214, 230, 255}, red{255, 48, 84, 255}, strap{30, 26, 38, 255};
    const SkullLook skull = skullLook(P.missing);
    Xf base{feet, sc};
    ell(base, 0, 0, 80, 12, fade(BLACK, 0.35f));
    bubbles(base, 0, -30, 240, 420, 6, now, 0.13f, 4.0f);
    Xf X{{feet.x + P.dx * sc, feet.y + P.dy * sc}, sc, P.sx, P.sy, P.rot, flip};
    const Slot slot = slotOf(P, flip);
    // the far arm
    static const Vector2 kFar[5] = {{-46, -150}, {-64, -318}, {-42, -132}, {-14, -178}, {-96, -214}};
    static const float kFarBend[5] = {12, -10, 12, 10, -12};
    boneArm(X, -36, -234, kFar[slot].x, kFar[slot].y, kFarBend[slot], mix(jacket, kInk, 0.3f), skull.boneShade);
    // ripped jeans and boots
    {
        Ink l(X);
        limb(l, -16, -130, -22, -32, 13, -4, jeans);
        limb(l, 14, -130, 18, -32, 13.5f, 4, jeans);
        l.draw();
    }
    for (Vector2 r : {Vector2{-20, -82}, Vector2{16, -82}}) Ink(X, 2.4f).e(r.x, r.y, 7, 4, skull.bone).draw();
    for (float x : {-20.0f, 22.0f}) {
        Ink(X).e(x + 6, -20, 26, 19, boots).p({{x - 20, -10}, {x + 32, -10}, {x + 32, 0}, {x - 20, 0}}, Color{72, 68, 86, 255}).draw();
        for (int i = 0; i < 3; ++i) line(X, x - 6 + float(i) * 7.0f, -32 + float(i) * 2.0f, x + 2 + float(i) * 7.0f, -27 + float(i) * 2.0f, 2.2f, red);
    }
    // the tee (a lightning bolt on it) and a studded belt
    Ink(X).p({{-30, -238}, {30, -238}, {32, -130}, {-32, -130}}, tee).draw();
    poly(X, {{4, -214}, {-10, -184}, {0, -184}}, kInk);
    poly(X, {{2, -190}, {12, -190}, {-4, -156}}, kInk);
    Ink(X, 3.2f).p({{-34, -142}, {34, -142}, {34, -128}, {-34, -128}}, strap).draw();
    for (int i = 0; i < 6; ++i) ell(X, -28 + float(i) * 11.0f, -135, 2.4f, 2.4f, silver);
    Ink(X, 2.4f).p({{-2, -141}, {12, -141}, {12, -129}, {-2, -129}}, silver).draw();
    // the open leather jacket
    Ink(X).p({{-46, -248}, {-10, -248}, {-16, -136}, {-46, -136}}, jacket).draw();
    Ink(X).p({{8, -248}, {44, -248}, {44, -136}, {16, -136}}, jacket).draw();
    poly(X, {{-10, -248}, {-24, -248}, {-14, -222}}, jacketHi);
    poly(X, {{8, -248}, {22, -248}, {12, -222}}, jacketHi);
    line(X, -12, -242, -17, -140, 2.2f, silver);
    line(X, 10, -242, 17, -140, 2.2f, silver);
    for (int i = 0; i < 3; ++i) {
        ell(X, -42 + float(i) * 8.0f, -244, 2.6f, 2.6f, silver);
        ell(X, 24 + float(i) * 8.0f, -244, 2.6f, 2.6f, silver);
    }
    // the neck and the spiked collar
    Ink(X).c(2, -246, 4, -272, 8.5f, skull.bone).draw();
    Ink(X, 3.2f).p({{-12, -262}, {18, -262}, {18, -250}, {-12, -250}}, strap).draw();
    for (int i = 0; i < 4; ++i) {
        const float x = -7 + float(i) * 8.0f;
        poly(X, {{x - 3, -250}, {x + 3, -250}, {x, -242}}, silver);
    }
    Xf H = X;
    H.o = X(8, -318 + 3.0f * P.bop);
    dmizeHead(H, faceFor(P, now, flip, 3.3f), skull, true);
    // the near arm and the microphone
    static const Vector2 kNear[5] = {{46, -176}, {60, -326}, {50, -134}, {118, -232}, {-74, -230}};
    static const Vector2 kMicDir[5] = {{0.2f, -1}, {0.2f, -1}, {0.3f, 1}, {1, -0.2f}, {-1, -0.25f}};
    static const float kNearBend[5] = {14, 10, 14, 12, -12};
    mic(X, kNear[slot].x, kNear[slot].y, kMicDir[slot].x, kMicDir[slot].y, red);
    boneArm(X, 32, -236, kNear[slot].x, kNear[slot].y, kNearBend[slot], jacket, skull.bone);
    if (P.singing && !P.missing)
        bubbles(X, kNear[slot].x + kMicDir[slot].x * 40, kNear[slot].y + kMicDir[slot].y * 30, 34, 110, 4, now, 0.9f, 13.0f);
}

}  // namespace

void drawCharacter(const CharacterState& s, Vector2 feet, float scale, double now, bool faceLeft) {
    const Pose P = computePose(s, now);
    switch (s.id) {
        case CharId::Herald: drawHype(s, P, feet, scale, now, faceLeft, false); break;
        case CharId::Shade: drawHype(s, P, feet, scale, now, faceLeft, true); break;
        case CharId::Principality: drawPrincipal(s, P, feet, scale, now, faceLeft); break;
        case CharId::Hand: drawHand(s, P, feet, scale, now, faceLeft); break;
        case CharId::Demise: drawDemise(s, P, feet, scale, now, faceLeft); break;
    }
}

// The head alone, as on the health bar: worried while losing.
void drawIcon(CharId id, Color tint, Vector2 c, float size, bool losing, double now, bool faceLeft) {
    Face f;
    f.miss = losing;
    f.open = losing ? 0.35f : 0.0f;
    const float u = size / 100.0f;
    switch (id) {
        case CharId::Herald:
        case CharId::Shade: {
            const float hs = u * 0.74f;
            hypeHead(Xf{{c.x, c.y + 16 * hs}, hs, 1, 1, 0, faceLeft}, f, hypeLook(id == CharId::Shade, tint, false));
            break;
        }
        case CharId::Principality: {
            const float hs = u * 0.68f;
            principalHead(Xf{{c.x, c.y + 24 * hs}, hs, 1, 1, 0, faceLeft}, f, false);
            break;
        }
        case CharId::Hand: {
            const float hs = u * 0.44f;
            djGlove(Xf{{c.x, c.y + 12 * hs}, hs, 1, 1, 0, faceLeft}, f, gloveLook(false), 1.0f, 1.0f, false, now);
            break;
        }
        case CharId::Demise: {
            const float hs = u * 0.6f;
            dmizeHead(Xf{{c.x, c.y + 26 * hs}, hs, 1, 1, 0, faceLeft}, f, skullLook(false), false);
            break;
        }
    }
}

}  // namespace gh
