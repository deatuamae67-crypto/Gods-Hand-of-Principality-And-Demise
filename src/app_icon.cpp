// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "app_icon.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "characters.hpp"
#include "common.hpp"
#include "gfx.hpp"
#include "platform.hpp"

#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace gh::appicon {

namespace {

Color lerpColor(Color a, Color b, float t) { return gfx::mix(a, b, saturate(t)); }

// The menus' sun: a disc fading from yellow at the top to pink at the bottom
// (a fan of triangles: the colours follow y exactly), cut by sky-coloured
// stripes that widen toward the horizon.
void stripedSun(Vector2 c, float r, float S, float skyY0, float skyY1, Color skyTop, Color skyLow) {
    const Color top{255, 232, 102, 255}, bottom{255, 58, 150, 255};
    auto at = [&](float y) { return lerpColor(top, bottom, (y - (c.y - r)) / (2 * r)); };
    constexpr int kSeg = 96;
    for (int i = 0; i < kSeg; ++i) {
        const float a0 = float(i) / kSeg * 6.2831853f, a1 = float(i + 1) / kSeg * 6.2831853f;
        const Vector2 p0{c.x + std::cos(a0) * r, c.y + std::sin(a0) * r}, p1{c.x + std::cos(a1) * r, c.y + std::sin(a1) * r};
        gfx::triColors(c, p0, p1, at(c.y), at(p0.y), at(p1.y));
    }
    auto sky = [&](float y) { return lerpColor(skyTop, skyLow, (y - skyY0) / (skyY1 - skyY0)); };
    auto half = [&](float y) {
        const float d = y - c.y;
        return d * d >= r * r ? 0.0f : std::sqrt(r * r - d * d);
    };
    for (int k = 0; k < 5; ++k) {
        const float y0 = c.y + r * (0.08f + 0.19f * float(k)), h = r * (0.035f + 0.022f * float(k));
        const float y1 = std::min(y0 + h, c.y + r);
        if (y0 >= y1) continue;
        const float w0 = half(y0) + S * 0.002f, w1 = half(y1) + S * 0.002f;
        gfx::quad({c.x - w0, y0}, {c.x + w0, y0}, {c.x + w1, y1}, {c.x - w1, y1}, sky((y0 + y1) * 0.5f));
    }
}

// `box`: the icon's square; `fill`: the area to cover (larger for Android's
// layers, which reach past the part a launcher shows).
void background(Rectangle box, Rectangle fill) {
    const float S = box.width;
    auto P = [&](float x, float y) { return Vector2{box.x + x * S, box.y + y * S}; };
    const float horizon = box.y + 0.72f * S;
    const float left = fill.x, right = fill.x + fill.width, bottom = fill.y + fill.height;
    const Color skyTop{20, 12, 62, 255}, skyLow{132, 34, 138, 255};
    if (fill.y < box.y) DrawRectangleRec({left, fill.y, fill.width, box.y - fill.y + 1}, skyTop);
    gfx::gradientRect({left, box.y, fill.width, horizon - box.y + 1}, skyTop, skyLow);

    // A few stars, as the menus twinkle them.
    static const float kStars[][3] = {{0.13f, 0.13f, 0.020f}, {0.29f, 0.25f, 0.010f}, {0.84f, 0.11f, 0.022f}, {0.71f, 0.25f, 0.011f},
                                      {0.10f, 0.45f, 0.012f}, {0.91f, 0.43f, 0.013f}, {0.49f, 0.07f, 0.009f}};
    for (const auto& s : kStars) gfx::star(P(s[0], s[1]), s[2] * S, WHITE);

    // The sun sitting on the horizon, then its light over the sky.
    const Vector2 sun{box.x + 0.5f * S, horizon - 0.05f * S};
    const float sunR = 0.30f * S;
    stripedSun(sun, sunR, S, box.y, horizon, skyTop, skyLow);
    gfx::glow(sun, sunR * 1.75f, gfx::fade(Color{255, 110, 175, 255}, 0.42f));

    // The floor: a grid running toward the viewer.
    gfx::gradientRect({left, horizon, fill.width, box.y + S - horizon}, Color{46, 10, 74, 255}, Color{10, 3, 26, 255});
    if (bottom > box.y + S) DrawRectangleRec({left, box.y + S - 1, fill.width, bottom - (box.y + S) + 1}, Color{10, 3, 26, 255});
    const Color line{255, 84, 206, 255};
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 1; i <= 9; ++i) {
        const float z = float(i);
        const float y = horizon + 0.176f * S / z - 0.02f * S;
        if (y <= horizon || y >= bottom) continue;
        DrawLineEx({left, y}, {right, y}, S * (0.004f + 0.006f / z), gfx::fade(line, std::min(0.8f, 0.2f + 0.7f / z)));
    }
    // The lines toward the vanishing point, spread out as far as the area goes.
    const float cx = box.x + 0.5f * S, run = (bottom - horizon) / (0.28f * S);
    for (int i = -12; i <= 12; ++i)
        DrawLineEx({cx + float(i) * 0.06f * S, horizon}, {cx + float(i) * (0.06f + 0.30f * run) * S, bottom}, S * 0.005f,
                   gfx::fade(line, 0.42f));
    DrawLineEx({left, horizon}, {right, horizon}, S * 0.008f, gfx::fade(line, 0.95f));
    EndBlendMode();
}

void foreground(Rectangle box, CharId who) {
    const float S = box.width;
    const Vector2 c{box.x + 0.5f * S, box.y + 0.5f * S};
    gfx::glow(c, 0.42f * S, gfx::fade(Color{110, 220, 255, 255}, 0.30f));
    drawIcon(who, WHITE, {c.x, c.y + 0.03f * S}, 0.80f * S, false, 0.0, false);
}

constexpr int kSuper = 2;  // render textures have no MSAA: draw bigger, then shrink

// Draws into a px x px render texture cleared to `clear` and reads it back.
// `scale`: the icon's square as a part of the picture (Android's layers keep
// the figure inside the middle a launcher shows).
Image capture(int px, Color clear, int layers, CharId who, float scale) {
    RenderTexture2D rt = LoadRenderTexture(px, px);
    BeginTextureMode(rt);
    ClearBackground(clear);
    const Rectangle fill{0, 0, float(px), float(px)};
    const float side = float(px) * scale, at = (float(px) - side) * 0.5f;
    const Rectangle box{at, at, side, side};
    if (layers & Background) background(box, fill);
    if (layers & Foreground) foreground(box, who);
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    UnloadRenderTexture(rt);
    ImageFlipVertical(&img);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return img;
}

unsigned char* pixels(Image& img) { return static_cast<unsigned char*>(img.data); }

void makeOpaque(Image& img) {
    unsigned char* p = pixels(img);
    for (int i = 0, n = img.width * img.height; i < n; ++i) p[i * 4 + 3] = 255;
}

// See-through corners: a rounded square (corner radius 20% of the side),
// edges smoothed over a pixel.
void roundCorners(Image& img) {
    const int n = img.width;
    const float r = 0.2f * float(n);
    unsigned char* p = pixels(img);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float fx = float(x) + 0.5f, fy = float(y) + 0.5f;
            const float cx = std::clamp(fx, r, float(n) - r), cy = std::clamp(fy, r, float(n) - r);
            const float d = std::hypot(fx - cx, fy - cy);
            if (d <= r - 0.5f) continue;
            const float cover = saturate(r + 0.5f - d);
            unsigned char& a = p[(y * n + x) * 4 + 3];
            a = static_cast<unsigned char>(std::lround(float(a) * cover));
        }
}

// The foreground alone, with its own transparency: drawn once on black and
// once on white, a pixel's opacity is how little the backdrop shows through
// (the glove's glossy, half-transparent highlights and its glow included).
Image foregroundAlone(int px, CharId who, float scale) {
    Image onBlack = capture(px, BLACK, Foreground, who, scale), onWhite = capture(px, WHITE, Foreground, who, scale);
    makeOpaque(onBlack);
    makeOpaque(onWhite);
    unsigned char* b = pixels(onBlack);
    const unsigned char* w = pixels(onWhite);
    for (int i = 0, n = px * px; i < n; ++i) {
        int diff = 0;
        for (int k = 0; k < 3; ++k) diff = std::max(diff, int(w[i * 4 + k]) - int(b[i * 4 + k]));
        const int a = std::clamp(255 - diff, 0, 255);
        for (int k = 0; k < 3; ++k) b[i * 4 + k] = a > 0 ? static_cast<unsigned char>(std::min(255, int(b[i * 4 + k]) * 255 / a)) : 0;
        b[i * 4 + 3] = static_cast<unsigned char>(a);
    }
    UnloadImage(onWhite);
    return onBlack;
}

Image renderFor(int size, int layers, bool rounded, CharId who, float scale = 1.0f) {
    const int px = size * kSuper;
    Image img;
    if (layers == Foreground) {
        img = foregroundAlone(px, who, scale);
    } else {
        img = capture(px, BLACK, layers, who, scale);
        makeOpaque(img);  // blending over the cleared target leaves the alpha channel below 1 here and there
        if (rounded) roundCorners(img);
    }
    ImageResize(&img, size, size);
    return img;
}

#if defined(__APPLE__)
// The Dock shows the program's own picture: NSApp.applicationIconImage, set
// through the Objective-C runtime (the program has no app bundle).
void setDockIcon(const Image& img) {
    int bytes = 0;
    unsigned char* png = ExportImageToMemory(img, ".png", &bytes);
    if (!png || bytes <= 0) return;
    using MsgId = id (*)(id, SEL);
    using MsgData = id (*)(id, SEL, const void*, unsigned long);
    using MsgObj = id (*)(id, SEL, id);
    using MsgSet = void (*)(id, SEL, id);
    id data = reinterpret_cast<MsgData>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSData")), sel_registerName("dataWithBytes:length:"),
                                                       png, static_cast<unsigned long>(bytes));
    MemFree(png);
    if (!data) return;
    id image = reinterpret_cast<MsgId>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSImage")), sel_registerName("alloc"));
    image = reinterpret_cast<MsgObj>(objc_msgSend)(image, sel_registerName("initWithData:"), data);
    if (!image) return;
    id app = reinterpret_cast<MsgId>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSApplication")), sel_registerName("sharedApplication"));
    if (app) reinterpret_cast<MsgSet>(objc_msgSend)(app, sel_registerName("setApplicationIconImage:"), image);
    reinterpret_cast<void (*)(id, SEL)>(objc_msgSend)(image, sel_registerName("release"));
}
#endif

}  // namespace

void draw(Rectangle box, int layers) {
    if (layers & Background) background(box, box);
    if (layers & Foreground) foreground(box, CharId::Hand);
}

Image render(int size, int layers, bool rounded) { return renderFor(size, layers, rounded, CharId::Hand); }

void applyToWindow() {
    if (platform::kAndroid) return;  // the launcher's icon is the APK's
#if defined(_WIN32)
    // The program's own icon resource (GLFW_ICON, src/windows/godshand.rc)
    // is the window's already, with a picture drawn for each size.
#elif defined(__APPLE__)
    Image big = render(512, Both, true);
    setDockIcon(big);
    UnloadImage(big);
#else
    // X11 picks the size it needs from these (Wayland uses the .desktop
    // file's). 128 pixels at most: taskbars and window switchers show less,
    // and the game starts a little sooner.
    static const int kSizes[] = {16, 24, 32, 48, 64, 128};
    std::vector<Image> icons;
    Image big = render(128, Both, true);
    for (int s : kSizes) {
        Image copy = ImageCopy(big);
        if (s != 128) ImageResize(&copy, s, s);
        icons.push_back(copy);
    }
    SetWindowIcons(icons.data(), int(icons.size()));
    for (Image& i : icons) UnloadImage(i);
    UnloadImage(big);
#endif
}

bool exportAll(const std::string& dir) {
    std::string d = dir.empty() ? std::string(".") : dir;
    if (d.back() != '/' && d.back() != '\\') d += '/';
    MakeDirectory(d.c_str());
    bool ok = true;
    auto save = [&](Image img, const std::string& name) {
        ok = ExportImage(img, (d + name).c_str()) && ok;
        UnloadImage(img);
    };
    save(render(1024, Both, true), "icon-1024.png");
    save(render(1024, Both, false), "icon-square-1024.png");
    // Android's adaptive icon: 108 x 108 dp layers of which a launcher shows
    // the middle 72 dp, and the figure stays within the middle 66 dp.
    save(renderFor(1024, Background, false, CharId::Hand, 72.0f / 108.0f), "android-background-1024.png");
    save(renderFor(1024, Foreground, false, CharId::Hand, 0.62f), "android-foreground-1024.png");
    return ok;
}

}  // namespace gh::appicon
