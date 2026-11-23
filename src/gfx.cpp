// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "gfx.hpp"
#include "keys.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "common.hpp"
#include "raymath.h"
#include "rlgl.h"

namespace gh::gfx {

namespace {
Font g_title{}, g_ui{};
bool g_titleOwned = false, g_uiOwned = false;
Texture2D g_glow{};
constexpr Rectangle kGlowSrc{0, 0, 128, 128};  // the glow's part of g_glow
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

    // The soft glow (a white disc fading out, 128 x 128, kGlowSrc) and, at
    // the other end of the same texture, a white square raylib draws its
    // shapes from. Shapes and glows (stars, sparks, lights, the characters'
    // eyes) then share one texture and go to the graphics card together,
    // instead of one draw call each time one follows the other (a sky of
    // stars was hundreds a frame). Nothing looks different: raylib's shapes
    // take the same white, and the glow's mipmaps never reach the square.
    // Mods' sprites, makeGraphic ones too, are drawn from their own textures.
    Image gi = GenImageColor(256, 128, BLANK);
    Color* px = static_cast<Color*>(gi.data);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            const float dx = (x + 0.5f) / 64.0f - 1.0f, dy = (y + 0.5f) / 64.0f - 1.0f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float a = saturate(1.0f - r);
            px[y * 256 + x] = Color{255, 255, 255, (unsigned char)(a * a * 255.0f)};
        }
    for (int y = 0; y < 64; ++y)
        for (int x = 192; x < 256; ++x) px[y * 256 + x] = WHITE;
    g_glow = LoadTextureFromImage(gi);
    UnloadImage(gi);
    GenTextureMipmaps(&g_glow);
    SetTextureFilter(g_glow, TEXTURE_FILTER_TRILINEAR);
    SetShapesTexture(g_glow, {224, 32, 1, 1});  // the middle of the white square (192-255, 0-63)

    for (int d = 0; d < 5; ++d) g_note[d] = makeGlyph(d == kCenter ? 1 : 0, dirColor(d), false, 384);
    g_receptor[0] = makeGlyph(0, WHITE, true, 384);
    g_receptor[1] = makeGlyph(1, WHITE, true, 384);
}

void shutdown() {
    if (g_titleOwned) UnloadFont(g_title);
    if (g_uiOwned) UnloadFont(g_ui);
    SetShapesTexture(Texture2D{}, Rectangle{});  // back to raylib's own white pixel
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

namespace {
// A picture's alpha to 1, its colours untouched (rgb = dst, alpha = 1).
void makeOpaque(const RenderTexture2D& rt) {
    BeginTextureMode(rt);
    rlSetBlendFactorsSeparate(RL_ZERO, RL_ONE, RL_ONE, RL_ZERO, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    DrawRectangle(0, 0, rt.texture.width, rt.texture.height, BLACK);
    EndBlendMode();
    EndTextureMode();
}
}  // namespace

void blendFrame(const RenderTexture2D& into, const RenderTexture2D& from, float a) {
    const Texture2D& t = from.texture;
    makeOpaque(from);  // so a is the weight everywhere
    BeginTextureMode(into);
    DrawTextureRec(t, {0, 0, float(t.width), -float(t.height)}, {0, 0}, Fade(WHITE, std::clamp(a, 0.0f, 1.0f)));
    EndTextureMode();
    makeOpaque(into);  // blending lowered its alpha (a^2 + (1 - a)): back to 1
}

void endTexture() {
    EndTextureMode();
    if (g_frameRt.id) BeginTextureMode(g_frameRt);  // back to the frame being rendered
}

Shader loadFragmentShader(const char* body) {
    std::string src;
    switch (rlGetVersion()) {
        case RL_OPENGL_ES_20:
            src = "#version 100\nprecision mediump float;\n#define GH_VARYING varying\n#define GH_TEX texture2D\n#define GH_FRAG gl_FragColor\n";
            break;
        case RL_OPENGL_21:
            src = "#version 120\n#define GH_VARYING varying\n#define GH_TEX texture2D\n#define GH_FRAG gl_FragColor\n";
            break;
        default:
            src = "#version 330\n#define GH_VARYING in\n#define GH_TEX texture\nout vec4 gh_frag;\n#define GH_FRAG gh_frag\n";
            break;
    }
    src += body;
    Shader sh = LoadShaderFromMemory(nullptr, src.c_str());
    if (sh.id == rlGetShaderIdDefault()) sh.id = 0;  // raylib falls back to its own: not this one
    return sh;
}

namespace {
// A 9-tap Gaussian in 5 reads (bilinear filtering takes two taps at once).
const char* kBlurBody = R"GLSL(
GH_VARYING vec2 fragTexCoord;
GH_VARYING vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 dir;
void main() {
    vec2 uv = fragTexCoord;
    vec4 c = GH_TEX(texture0, uv) * 0.2270270270;
    c += (GH_TEX(texture0, uv + dir * 1.3846153846) + GH_TEX(texture0, uv - dir * 1.3846153846)) * 0.3162162162;
    c += (GH_TEX(texture0, uv + dir * 3.2307692308) + GH_TEX(texture0, uv - dir * 3.2307692308)) * 0.0702702703;
    // Opaque: what was drawn see-through into the texture must not let the
    // screen behind it show when the picture is put back.
    GH_FRAG = vec4(c.rgb, 1.0) * colDiffuse * fragColor;
}
)GLSL";
}  // namespace

void drawBlurred(const std::function<void()>& scene, float spread) {
    static RenderTexture2D rt[2] = {};
    static Shader shader{};
    static int locDir = -1;
    static bool tried = false;
    constexpr int w = int(W / 2), h = int(H / 2);
    if (!tried) {
        tried = true;
        shader = loadFragmentShader(kBlurBody);
        if (shader.id) {
            locDir = GetShaderLocation(shader, "dir");
            for (RenderTexture2D& t : rt) {
                t = LoadRenderTexture(w, h);
                SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
                SetTextureWrap(t.texture, TEXTURE_WRAP_CLAMP);
            }
        }
    }
    if (!shader.id || !rt[0].id || !rt[1].id) {
        scene();
        return;
    }
    EndMode2D();
    beginTexture(rt[0]);
    ClearBackground(BLACK);
    Camera2D half{};
    half.zoom = float(w) / W;
    BeginMode2D(half);
    scene();
    EndMode2D();
    endTexture();
    auto pass = [&](const RenderTexture2D& from, const RenderTexture2D& to, Vector2 d) {
        beginTexture(to);
        ClearBackground(BLACK);
        BeginShaderMode(shader);
        SetShaderValue(shader, locDir, &d, SHADER_UNIFORM_VEC2);
        DrawTextureRec(from.texture, {0, 0, float(w), -float(h)}, {0, 0}, WHITE);
        EndShaderMode();
        endTexture();
    };
    for (int i = 1; i <= 2; ++i) {
        pass(rt[0], rt[1], {spread * float(i) / w, 0});
        pass(rt[1], rt[0], {0, spread * float(i) / h});
    }
    BeginMode2D(screenCamera());
    DrawTexturePro(rt[0].texture, {0, 0, float(w), -float(h)}, {0, 0, W, H}, {0, 0}, 0, WHITE);
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

Rectangle visibleArea() {
    // The corners of the picture (-1..1 once projected) taken back through
    // the camera and the projection to the coordinates being drawn in.
    const Matrix inv = MatrixInvert(MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection()));
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (int k = 0; k < 4; ++k) {
        const Vector3 p = Vector3Transform({k & 1 ? 1.0f : -1.0f, k & 2 ? 1.0f : -1.0f, 0}, inv);
        if (k == 0 || p.x < x0) x0 = p.x;
        if (k == 0 || p.x > x1) x1 = p.x;
        if (k == 0 || p.y < y0) y0 = p.y;
        if (k == 0 || p.y > y1) y1 = p.y;
    }
    return {x0, y0, x1 - x0, y1 - y0};
}
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

namespace {
float cross3(Vector2 a, Vector2 b, Vector2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
// Quads on the shapes texture, as raylib draws its shapes; each quad is two
// triangles (q0 q1 q2, q0 q2 q3), each turned the way raylib does not cull
// (what tri does to one triangle).
struct QuadPen {
    explicit QuadPen(Color col) {
        const Texture2D tex = GetShapesTexture();
        const Rectangle r = GetShapesTextureRectangle();
        rlSetTexture(tex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(col.r, col.g, col.b, col.a);
        rlNormal3f(0, 0, 1);
        rlTexCoord2f((r.x + r.width * 0.5f) / float(tex.width), (r.y + r.height * 0.5f) / float(tex.height));
    }
    ~QuadPen() {
        rlEnd();
        rlSetTexture(0);
    }
    void put(Vector2 a, Vector2 b, Vector2 c, Vector2 d) const {
        rlVertex2f(a.x, a.y);
        rlVertex2f(b.x, b.y);
        rlVertex2f(c.x, c.y);
        rlVertex2f(d.x, d.y);
    }
    void tri(Vector2 a, Vector2 b, Vector2 c) const {
        if (cross3(a, b, c) > 0) std::swap(b, c);
        put(a, b, c, c);
    }
};
}  // namespace

// A fan from the first point, two triangles per quad (half the vertices of
// one triangle at a time, all in one go), each turned to face the screen.
void convex(const Vector2* p, size_t n, Color col) {
    if (n < 3) return;
    const QuadPen pen(col);
    for (size_t i = 1; i + 1 < n; i += 2) {
        if (i + 2 >= n) {
            pen.tri(p[0], p[i], p[i + 1]);
            break;
        }
        const float c1 = cross3(p[0], p[i], p[i + 1]), c2 = cross3(p[0], p[i + 1], p[i + 2]);
        if (c1 <= 0 && c2 <= 0) {
            pen.put(p[0], p[i], p[i + 1], p[i + 2]);
        } else if (c1 >= 0 && c2 >= 0) {
            pen.put(p[0], p[i + 2], p[i + 1], p[i]);
        } else {  // not convex there: each on its own
            pen.tri(p[0], p[i], p[i + 1]);
            pen.tri(p[0], p[i + 1], p[i + 2]);
        }
    }
}
void convex(const std::vector<Vector2>& pts, Color col) { convex(pts.data(), pts.size(), col); }
void thickLine(Vector2 a, Vector2 b, float thick, Color col) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0 || thick <= 0) return;
    const float s = thick / (2 * len);
    const Vector2 r{-s * dy, s * dx};
    const Vector2 q0{a.x - r.x, a.y - r.y}, q1{a.x + r.x, a.y + r.y}, q2{b.x + r.x, b.y + r.y}, q3{b.x - r.x, b.y - r.y};
    const QuadPen pen(col);
    if (cross3(q0, q1, q2) > 0) pen.put(q0, q3, q2, q1);
    else pen.put(q0, q1, q2, q3);
}
void capsule(Vector2 a, Vector2 b, float r, Color col) {
    thickLine(a, b, r * 2.0f, col);
    disc(a, r, col);
    disc(b, r, col);
}

namespace {
// Sides for a circle of radius r: each side strays at most 0.25 pixels from
// the true circle (raylib's own rule for circles, at half its error), from 8
// to raylib's 36.
int sidesFor(float r) {
    if (r <= 2.0f) return 8;
    const float k = 1.0f - 0.25f / r;
    const float th = std::acos(std::clamp(2.0f * k * k - 1.0f, -1.0f, 1.0f));
    if (th <= 0.0f) return 36;
    return std::clamp(int(std::ceil(6.2831853f / th)), 8, 36);
}
}  // namespace

void disc(Vector2 c, float r, Color col) {
    if (r <= 0) return;
    DrawCircleSector(c, r, 0, 360, sidesFor(r), col);
}

void circleGradient(Vector2 c, float r, Color inner, Color outer) {
    if (r <= 0) return;
    const int n = sidesFor(r);
    const Texture2D tex = GetShapesTexture();
    const Rectangle sr = GetShapesTextureRectangle();
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlNormal3f(0, 0, 1);
    rlTexCoord2f((sr.x + sr.width * 0.5f) / float(tex.width), (sr.y + sr.height * 0.5f) / float(tex.height));
    // Two sides per quad (centre, then three points of the edge), turned as
    // raylib's own circles are.
    auto edge = [&](int i) {
        const float a = 6.2831853f * float(i) / float(n);
        return Vector2{c.x + std::sin(a) * r, c.y + std::cos(a) * r};
    };
    for (int i = 0; i < n; i += 2) {
        const Vector2 p0 = edge(i), p1 = edge(i + 1), p2 = edge(std::min(i + 2, n));
        rlColor4ub(inner.r, inner.g, inner.b, inner.a);
        rlVertex2f(c.x, c.y);
        rlColor4ub(outer.r, outer.g, outer.b, outer.a);
        rlVertex2f(p0.x, p0.y);
        rlVertex2f(p1.x, p1.y);
        rlVertex2f(p2.x, p2.y);
    }
    rlEnd();
    rlSetTexture(0);
}

void splineCatmullRom(const Vector2* p, int n, float thick, Color col) {
    if (n < 4 || thick <= 0) return;
    const float half = thick * 0.5f;
    disc(p[1], half, col);
    {
        const QuadPen pen(col);
        // The left and right edges where the last piece ended.
        Vector2 prevL{}, prevR{};
        bool started = false;
        for (int i = 0; i + 3 < n; ++i) {
            const Vector2 p1 = p[i], p2 = p[i + 1], p3 = p[i + 2], p4 = p[i + 3];
            const float len = std::hypot(p3.x - p2.x, p3.y - p2.y);
            const int div = std::clamp(int(len / 3.0f) + 2, 2, 24);  // pieces of about 3 pixels
            Vector2 cur = p2;
            for (int j = 1; j <= div; ++j) {
                const float t = float(j) / float(div), t2 = t * t, t3 = t2 * t;
                const float q0 = -t3 + 2.0f * t2 - t, q1 = 3.0f * t3 - 5.0f * t2 + 2.0f;
                const float q2 = -3.0f * t3 + 4.0f * t2 + t, q3 = t3 - t2;
                const Vector2 nxt{0.5f * (p1.x * q0 + p2.x * q1 + p3.x * q2 + p4.x * q3),
                                  0.5f * (p1.y * q0 + p2.y * q1 + p3.y * q2 + p4.y * q3)};
                const float dx = nxt.x - cur.x, dy = nxt.y - cur.y, d = std::sqrt(dx * dx + dy * dy);
                if (d <= 0) continue;
                const float s = half / d;
                if (!started) {
                    prevL = {cur.x + dy * s, cur.y - dx * s};
                    prevR = {cur.x - dy * s, cur.y + dx * s};
                    started = true;
                }
                const Vector2 L{nxt.x + dy * s, nxt.y - dx * s}, R{nxt.x - dy * s, nxt.y + dx * s};
                if (cross3(prevL, prevR, R) > 0) pen.put(prevL, L, R, prevR);
                else pen.put(prevL, prevR, R, L);
                prevL = L;
                prevR = R;
                cur = nxt;
            }
        }
    }
    disc(p[n - 2], half, col);
}
// raylib 5.5's DrawRectangleGradientEx takes its corners as top-left,
// bottom-left, bottom-right, top-right (whatever its parameters are called):
// its own DrawRectangleGradientV passes (top, bottom, bottom, top).
void gradientRect(Rectangle r, Color top, Color bottom) { DrawRectangleGradientEx(r, top, bottom, bottom, top); }

void glow(Vector2 c, float radius, Color col) {
    BeginBlendMode(BLEND_ADDITIVE);
    DrawTexturePro(g_glow, kGlowSrc, {c.x, c.y, radius * 2, radius * 2}, {radius, radius}, 0, col);
    EndBlendMode();
}
void softCircle(Vector2 c, float radius, Color col) {
    DrawTexturePro(g_glow, kGlowSrc, {c.x, c.y, radius * 2, radius * 2}, {radius, radius}, 0, col);
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
            DrawTexturePro(g_glow, kGlowSrc, {p.p.x, p.p.y, s * 2, s * 2}, {s, s}, 0, fade(p.c, t));
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
