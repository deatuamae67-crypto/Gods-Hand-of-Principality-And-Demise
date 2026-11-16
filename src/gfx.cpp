// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "gfx.hpp"
#include "keys.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "common.hpp"
#include "rlgl.h"

namespace gh::gfx {

namespace {
Font g_title{}, g_ui{};
bool g_titleOwned = false, g_uiOwned = false;
Texture2D g_glow{};
Texture2D g_note[5]{};
Texture2D g_receptor[2]{};  // arrow, diamond

Color lerpColor(Color a, Color b, float t) {
    t = saturate(t);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

float segDist(float px, float py, float ax, float ay, float bx, float by) {
    const float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    const float l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0.0f;
    t = saturate(t);
    const float dx = wx - vx * t, dy = wy - vy * t;
    return std::sqrt(dx * dx + dy * dy);
}

// Distance from (u,v) to the centre line of a note glyph (unit square [-1,1]).
float glyphDist(int shape, float u, float v) {
    if (shape == 0) {  // arrow pointing left
        float d = segDist(u, v, -0.50f, 0.0f, 0.60f, 0.0f);
        d = std::min(d, segDist(u, v, -0.60f, 0.0f, -0.02f, -0.58f));
        d = std::min(d, segDist(u, v, -0.60f, 0.0f, -0.02f, 0.58f));
        return d;
    }
    // diamond with a centre gem
    const float r = 0.6f;
    float d = segDist(u, v, 0, -r, r, 0);
    d = std::min(d, segDist(u, v, r, 0, 0, r));
    d = std::min(d, segDist(u, v, 0, r, -r, 0));
    d = std::min(d, segDist(u, v, -r, 0, 0, -r));
    d = std::min(d, std::sqrt(u * u + v * v));
    return d;
}

Texture2D makeGlyph(int shape, Color base, bool receptor, int size) {
    Image img = GenImageColor(size, size, BLANK);
    Color* px = static_cast<Color*>(img.data);
    const float pw = 2.0f / float(size);
    const float r = 0.16f, o = 0.075f;
    const Color outline = receptor ? Color{205, 210, 230, 255} : Color{22, 12, 38, 255};
    const Color dark = receptor ? Color{40, 38, 56, 255} : lerpColor(base, BLACK, 0.45f);
    const Color light = receptor ? Color{118, 116, 140, 255} : lerpColor(base, WHITE, 0.55f);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (x + 0.5f) * pw - 1.0f, v = (y + 0.5f) * pw - 1.0f;
            const float d = glyphDist(shape, u, v);
            const float aOuter = saturate((r + o - d) / pw + 0.5f);
            const float aInner = saturate((r - d) / pw + 0.5f);
            const float k = saturate(1.0f - d / r);
            Color fill = lerpColor(dark, light, std::pow(k, 0.8f));
            fill = lerpColor(fill, WHITE, (receptor ? 0.05f : 0.18f) * saturate(-v));
            Color c = lerpColor(outline, fill, aInner);
            c.a = (unsigned char)(aOuter * (receptor ? 235.0f : 255.0f));
            px[y * size + x] = c;
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

Font loadFont(const std::string& path, int size, bool& owned) {
    owned = false;
    if (!FileExists(path.c_str())) return GetFontDefault();
    std::vector<int> cps;
    for (int c = 32; c < 256; ++c) cps.push_back(c);
    cps.push_back(0x2014);  // em dash
    cps.push_back(0x2026);  // ellipsis
    Font f = LoadFontEx(path.c_str(), size, cps.data(), int(cps.size()));
    if (!IsFontValid(f) || f.texture.id == 0) return GetFontDefault();
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    owned = true;
    return f;
}

}  // namespace

const Font& fontOf(FontId f) { return f == FontId::Title ? g_title : g_ui; }

float randf(float lo, float hi) { return lo + (hi - lo) * float(std::rand()) / float(RAND_MAX); }

void init(const std::string& baseDir) {
    g_title = loadFont(baseDir + "assets/fonts/LuckiestGuy-Regular.ttf", 112, g_titleOwned);
    g_ui = loadFont(baseDir + "assets/fonts/Rajdhani-Bold.ttf", 72, g_uiOwned);

    Image gi = GenImageColor(128, 128, BLANK);
    Color* px = static_cast<Color*>(gi.data);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            const float dx = (x + 0.5f) / 64.0f - 1.0f, dy = (y + 0.5f) / 64.0f - 1.0f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float a = saturate(1.0f - r);
            px[y * 128 + x] = Color{255, 255, 255, (unsigned char)(a * a * 255.0f)};
        }
    g_glow = LoadTextureFromImage(gi);
    UnloadImage(gi);
    GenTextureMipmaps(&g_glow);
    SetTextureFilter(g_glow, TEXTURE_FILTER_TRILINEAR);

    for (int d = 0; d < 5; ++d) g_note[d] = makeGlyph(d == kCenter ? 1 : 0, dirColor(d), false, 384);
    g_receptor[0] = makeGlyph(0, WHITE, true, 384);
    g_receptor[1] = makeGlyph(1, WHITE, true, 384);
}

void shutdown() {
    if (g_titleOwned) UnloadFont(g_title);
    if (g_uiOwned) UnloadFont(g_ui);
    UnloadTexture(g_glow);
    for (auto& t : g_note) UnloadTexture(t);
    for (auto& t : g_receptor) UnloadTexture(t);
}

// ---------------------------------------------------------------- canvas --

static int g_targetW = 0, g_targetH = 0;
static float g_viewW = W;
static RenderTexture2D g_frameRt{};  // rendering mode: frames drawn here
void setRenderTargetSize(int w, int h) {
    g_targetW = w > 0 ? w : (g_frameRt.id ? g_frameRt.texture.width : 0);
    g_targetH = h > 0 ? h : (g_frameRt.id ? g_frameRt.texture.height : 0);
}

void beginFrameTarget(const RenderTexture2D& rt) {
    g_frameRt = rt;
    setRenderTargetSize(0, 0);
    BeginTextureMode(rt);
}

void endFrameTarget() {
    EndTextureMode();
    g_frameRt = {};
    setRenderTargetSize(0, 0);
}

bool frameTargetActive() { return g_frameRt.id != 0; }

void beginTexture(const RenderTexture2D& rt) { BeginTextureMode(rt); }

void endTexture() {
    EndTextureMode();
    if (g_frameRt.id) BeginTextureMode(g_frameRt);  // back to the frame being rendered
}

int targetWidth() { return g_targetW > 0 ? g_targetW : GetScreenWidth(); }
int targetHeight() { return g_targetH > 0 ? g_targetH : GetScreenHeight(); }

void setViewWidth(float w) { g_viewW = std::clamp(w, 64.0f, W); }
float viewWidth() { return g_viewW; }

// A render texture holds the whole canvas; the window shows 0..g_viewW of it.
static float shownWidth() { return g_targetW > 0 ? W : g_viewW; }

float canvasScale() {
    const float sw = float(g_targetW > 0 ? g_targetW : GetScreenWidth()), sh = float(g_targetH > 0 ? g_targetH : GetScreenHeight());
    return std::min(sw / shownWidth(), sh / H);
}

Vector2 canvasOrigin() {
    const float sw = float(g_targetW > 0 ? g_targetW : GetScreenWidth()), sh = float(g_targetH > 0 ? g_targetH : GetScreenHeight());
    const float s = canvasScale();
    return {(sw - shownWidth() * s) * 0.5f, (sh - H * s) * 0.5f};
}

Vector2 toCanvas(Vector2 p) {
    const Vector2 o = canvasOrigin();
    const float s = canvasScale();
    return {(p.x - o.x) / s, (p.y - o.y) / s};
}

Camera2D screenCamera(float zoom, Vector2 focus, float rotationDeg) {
    const float s = canvasScale();
    const Vector2 o = canvasOrigin();
    Camera2D cam{};
    cam.offset = {o.x + s * W * 0.5f, o.y + s * H * 0.5f};
    cam.target = focus;
    cam.rotation = rotationDeg;
    cam.zoom = s * zoom;
    return cam;
}

void beginCanvas(float zoom, Vector2 focus, float rotationDeg) { BeginMode2D(screenCamera(zoom, focus, rotationDeg)); }
void endCanvas() { EndMode2D(); }

void drawLetterbox() {
    const float sw = float(targetWidth()), sh = float(targetHeight());
    const float s = canvasScale();
    const Vector2 o = canvasOrigin();
    const float right = o.x + g_viewW * s;
    if (o.x > 0.5f) DrawRectangleRec({0, 0, o.x + 1, sh}, BLACK);
    if (sw - right > 0.5f) DrawRectangleRec({right - 1, 0, sw - right + 2, sh}, BLACK);
    if (o.y > 0.5f) {
        DrawRectangleRec({0, 0, sw, o.y + 1}, BLACK);
        DrawRectangleRec({0, sh - o.y - 1, sw, o.y + 2}, BLACK);
    }
}

// ------------------------------------------------------------------ text --

Vector2 measure(FontId f, std::string_view s, float size) {
    const std::string str(s);
    const Font& font = fontOf(f);
    return MeasureTextEx(font, str.c_str(), size, f == FontId::Title ? size * 0.02f : 0.0f);
}

void text(FontId f, std::string_view s, float x, float y, float size, Color c, Align a, float outline, Color oc) {
    const std::string str(s);
    const Font& font = fontOf(f);
    const float spacing = f == FontId::Title ? size * 0.02f : 0.0f;
    float px = x;
    if (a != Align::Left) {
        const float w = MeasureTextEx(font, str.c_str(), size, spacing).x;
        px = a == Align::Center ? x - w * 0.5f : x - w;
    }
    if (outline > 0) {
        oc.a = (unsigned char)(oc.a * (c.a / 255.0f));
        static const float dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7f, 0.7f}, {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
        for (auto& d : dirs) DrawTextEx(font, str.c_str(), {px + d[0] * outline, y + d[1] * outline}, size, spacing, oc);
    }
    DrawTextEx(font, str.c_str(), {px, y}, size, spacing, c);
}

void glowText(FontId f, std::string_view s, float x, float y, float size, Color c, Color g, Align a) {
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 6; ++i) {
        const float ang = float(i) * 1.047f;
        const float r = size * 0.04f;
        text(f, s, x + std::cos(ang) * r, y + std::sin(ang) * r, size, fade(g, 0.18f), a);
    }
    EndBlendMode();
    text(f, s, x, y, size, c, a, size * 0.035f);
}

// --------------------------------------------------------------- colours --

Color rgb(unsigned hex, unsigned char a) {
    return Color{(unsigned char)((hex >> 16) & 0xFF), (unsigned char)((hex >> 8) & 0xFF), (unsigned char)(hex & 0xFF), a};
}
Color fade(Color c, float a) { c.a = (unsigned char)(c.a * saturate(a)); return c; }
Color mix(Color a, Color b, float t) { return lerpColor(a, b, t); }
Color brighten(Color c, float amount) {
    return amount >= 0 ? lerpColor(c, Color{255, 255, 255, c.a}, amount) : lerpColor(c, Color{0, 0, 0, c.a}, -amount);
}

// ---------------------------------------------------------------- shapes --

void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) DrawTriangle(a, c, b, col);
    else DrawTriangle(a, b, c, col);
}
// A triangle with its own colour at each corner, blended across it (drawn as
// raylib draws its triangles: a quad on the shapes texture, counter-clockwise).
void triColors(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc) {
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) {
        std::swap(b, c);
        std::swap(cb, cc);
    }
    const Texture2D tex = GetShapesTexture();
    const Rectangle r = GetShapesTextureRectangle();
    const float u0 = r.x / float(tex.width), v0 = r.y / float(tex.height);
    const float u1 = (r.x + r.width) / float(tex.width), v1 = (r.y + r.height) / float(tex.height);
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(ca.r, ca.g, ca.b, ca.a);
    rlTexCoord2f(u0, v0);
    rlVertex2f(a.x, a.y);
    rlColor4ub(cb.r, cb.g, cb.b, cb.a);
    rlTexCoord2f(u0, v1);
    rlVertex2f(b.x, b.y);
    rlTexCoord2f(u1, v1);
    rlVertex2f(b.x, b.y);
    rlColor4ub(cc.r, cc.g, cc.b, cc.a);
    rlTexCoord2f(u1, v0);
    rlVertex2f(c.x, c.y);
    rlEnd();
    rlSetTexture(0);
}
void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    tri(a, b, c, col);
    tri(a, c, d, col);
}
void convex(const std::vector<Vector2>& pts, Color col) {
    for (size_t i = 1; i + 1 < pts.size(); ++i) tri(pts[0], pts[i], pts[i + 1], col);
}
void capsule(Vector2 a, Vector2 b, float r, Color col) {
    DrawLineEx(a, b, r * 2.0f, col);
    DrawCircleV(a, r, col);
    DrawCircleV(b, r, col);
}
void gradientRect(Rectangle r, Color top, Color bottom) { DrawRectangleGradientEx(r, top, bottom, top, bottom); }

Texture2D& glowTexture() { return g_glow; }

void glow(Vector2 c, float radius, Color col) {
    BeginBlendMode(BLEND_ADDITIVE);
    DrawTexturePro(g_glow, {0, 0, 128, 128}, {c.x, c.y, radius * 2, radius * 2}, {radius, radius}, 0, col);
    EndBlendMode();
}
void softCircle(Vector2 c, float radius, Color col) {
    DrawTexturePro(g_glow, {0, 0, 128, 128}, {c.x, c.y, radius * 2, radius * 2}, {radius, radius}, 0, col);
}

namespace {
struct Vec3 {
    float x, y, z;
};
}  // namespace

// A star as the menus draw it: a round, soft centre and a thin ray along
// each axis (the big ones get four shorter diagonal rays too).
void star(Vector2 c, float r, Color col) {
    softCircle(c, r * 1.1f, fade(col, 0.55f));
    const float w = std::max(0.8f, r * 0.16f);
    for (int k = 0; k < 4; ++k) {
        const float a = float(k) * 1.5707963f;
        const Vector2 d{std::cos(a), std::sin(a)}, n{-d.y, d.x};
        tri({c.x + n.x * w, c.y + n.y * w}, {c.x - n.x * w, c.y - n.y * w}, {c.x + d.x * r * 2.4f, c.y + d.y * r * 2.4f}, col);
        if (r > 5.5f) {
            const float b = a + 0.7853982f;
            const Vector2 e{std::cos(b), std::sin(b)}, m{-e.y, e.x};
            tri({c.x + m.x * w * 0.6f, c.y + m.y * w * 0.6f}, {c.x - m.x * w * 0.6f, c.y - m.y * w * 0.6f},
                     {c.x + e.x * r * 1.2f, c.y + e.y * r * 1.2f}, fade(col, 0.7f));
        }
    }
    DrawCircleV(c, std::max(1.0f, r * 0.34f), col);
}

// A glassy cube turning in the air: lit faces (a highlight where the light
// hits them straight), bright edges. mirror: its reflection in the floor
// (flipped about floorY, flattened, faint).
void cube(Vector2 at, float size, float ax, float ay, Color base, float alpha, bool mirror, float floorY) {
    static const Vec3 kCorner[8] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    static const int kFace[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {4, 5, 1, 0}, {3, 2, 6, 7}};
    static const Vec3 kNormal[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    const float cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
    auto rot = [&](Vec3 v) {
        const Vec3 a{v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy};    // about the vertical axis
        return Vec3{a.x, a.y * cx - a.z * sx, a.y * sx + a.z * cx};      // then about the horizontal one
    };
    Vector2 p[8];
    for (int i = 0; i < 8; ++i) {
        const Vec3 v = rot(kCorner[i]);
        const float f = 4.0f / (4.0f + v.z);  // perspective
        float y = at.y + v.y * size * 0.5f * f;
        if (mirror) y = floorY + (floorY - y) * 0.55f;
        p[i] = {at.x + v.x * size * 0.5f * f, y};
    }
    const Vec3 light{-0.45f, -0.7f, -0.55f};
    for (int fi = 0; fi < 6; ++fi) {
        const Vec3 n = rot(kNormal[fi]);
        if (n.z > -0.02f) continue;  // turned away
        const float lit = std::max(0.0f, -(n.x * light.x + n.y * light.y + n.z * light.z));
        const float shine = std::pow(lit, 10.0f);
        const Color face = mix(mix(Color{10, 8, 30, 255}, base, 0.35f + 0.65f * lit), WHITE, shine * 0.7f);
        const int* q = kFace[fi];
        quad(p[q[0]], p[q[1]], p[q[2]], p[q[3]], fade(face, alpha * (0.45f + 0.35f * lit)));
        for (int k = 0; k < 4; ++k) DrawLineEx(p[q[k]], p[q[(k + 1) % 4]], mirror ? 1.0f : 2.0f, fade(brighten(base, 0.45f), alpha));
    }
}

// ----------------------------------------------------------------- notes --

int laneDir(int keys, int lane) {
    static const int dirs[10][9] = {
        {},
        {kCenter},
        {kLeft, kRight},
        {kLeft, kCenter, kRight},
        {kLeft, kDown, kUp, kRight},
        {kLeft, kDown, kCenter, kUp, kRight},
        {kLeft, kUp, kRight, kLeft, kDown, kRight},
        {kLeft, kUp, kRight, kCenter, kLeft, kDown, kRight},
        {kLeft, kDown, kUp, kRight, kLeft, kDown, kUp, kRight},
        {kLeft, kDown, kUp, kRight, kCenter, kLeft, kDown, kUp, kRight},
    };
    keys = clampv(keys, 1, kMaxKeys);
    lane = clampv(lane, 0, keys - 1);
    if (keys > ek::kTableKeys) return ek::manyDir(keys, lane);
    if (keys > 9) return ek::kDirs[keys - 10][lane];
    return dirs[keys][lane];
}

Color dirColor(int dir) {
    switch (dir) {
        case kLeft: return Color{255, 92, 138, 255};
        case kDown: return Color{54, 181, 255, 255};
        case kUp: return Color{255, 201, 60, 255};
        case kRight: return Color{60, 224, 160, 255};
        default: return Color{201, 166, 255, 255};
    }
}

static float dirRotation(int dir) {
    switch (dir) {
        case kUp: return 90.0f;
        case kRight: return 180.0f;
        case kDown: return 270.0f;
        default: return 0.0f;
    }
}

void drawNote(int dir, Vector2 c, float size, float alpha, float scale, float extraRotDeg) {
    const Texture2D& t = g_note[clampv(dir, 0, 4)];
    const float s = size * scale;
    DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, {c.x, c.y, s, s}, {s * 0.5f, s * 0.5f},
                   dirRotation(dir) + extraRotDeg, fade(WHITE, alpha));
}

void drawReceptor(int dir, Vector2 c, float size, Color tint, float scale, float extraRotDeg) {
    const Texture2D& t = g_receptor[dir == kCenter ? 1 : 0];
    const float s = size * scale;
    DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, {c.x, c.y, s, s}, {s * 0.5f, s * 0.5f},
                   dirRotation(dir) + extraRotDeg, tint);
}

void drawSustain(int dir, float x, float y0, float y1, float width, float alpha, bool capAtY1) {
    if (std::fabs(y1 - y0) < 0.5f) return;
    const Color base = dirColor(dir);
    const float top = std::min(y0, y1), h = std::fabs(y1 - y0);
    DrawRectangleRec({x - width * 0.5f - 3, top, width + 6, h}, fade(Color{22, 12, 38, 255}, alpha * 0.85f));
    DrawRectangleRec({x - width * 0.5f, top, width, h}, fade(brighten(base, -0.15f), alpha * 0.9f));
    DrawRectangleRec({x - width * 0.18f, top, width * 0.36f, h}, fade(brighten(base, 0.45f), alpha * 0.9f));
    if (capAtY1) {
        DrawCircleV({x, y1}, width * 0.5f + 3, fade(Color{22, 12, 38, 255}, alpha * 0.85f));
        DrawCircleV({x, y1}, width * 0.5f, fade(brighten(base, -0.15f), alpha * 0.9f));
        DrawCircleV({x, y1}, width * 0.18f, fade(brighten(base, 0.45f), alpha * 0.9f));
    }
}

// ------------------------------------------------------------- particles --

void Particles::update(float dt, float gravity) {
    for (Particle& p : ps_) {
        p.life -= dt;
        p.v.y += gravity * dt;
        p.p.x += p.v.x * dt;
        p.p.y += p.v.y * dt;
    }
    ps_.erase(std::remove_if(ps_.begin(), ps_.end(), [](const Particle& p) { return p.life <= 0; }), ps_.end());
}

void Particles::draw(bool additive) const {
    if (ps_.empty()) return;
    if (additive) BeginBlendMode(BLEND_ADDITIVE);
    for (const Particle& p : ps_) {
        const float t = saturate(p.life / p.maxLife);
        if (p.kind == 0) {
            const float s = p.size * (0.5f + 0.5f * t);
            DrawTexturePro(g_glow, {0, 0, 128, 128}, {p.p.x, p.p.y, s * 2, s * 2}, {s, s}, 0, fade(p.c, t));
        } else if (p.kind == 1) {
            const float len = std::sqrt(p.v.x * p.v.x + p.v.y * p.v.y) * 0.04f + 2.0f;
            const float inv = 1.0f / std::max(1e-3f, std::sqrt(p.v.x * p.v.x + p.v.y * p.v.y));
            DrawLineEx(p.p, {p.p.x - p.v.x * inv * len, p.p.y - p.v.y * inv * len}, p.size * t + 0.5f, fade(p.c, t));
        } else {
            const float r = p.size * (1.0f - t) + 4.0f;
            DrawRing(p.p, r, r + 3.0f * t + 1.0f, 0, 360, 36, fade(p.c, t));
        }
    }
    if (additive) EndBlendMode();
}

}  // namespace gh::gfx
