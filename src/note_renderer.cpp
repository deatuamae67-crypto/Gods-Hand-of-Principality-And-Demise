// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "note_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstring>
#include <string>

#include "gfx.hpp"
#include "platform.hpp"
#include "psych_ui.hpp"
#include "rlgl.h"
#include "sysinfo.hpp"

namespace gh {

namespace {

// The picture reaches this far past the canvas (canvas units), for notes a
// zoomed-out HUD camera shows beyond the edges.
constexpr float kMargin = 64.0f;

double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

}  // namespace

NoteRenderer::~NoteRenderer() { unload(); }

const char* NoteRenderer::name() const {
    if (mode_ == kGpu) return auto_ ? "Auto: GPU" : "GPU";
    if (mode_ == kCpuAsm) {
        if (!usingAsm()) return auto_ ? "Auto: CPU (C++, no x86 ASM here)" : "CPU (C++, no x86 ASM here)";
        return auto_ ? "Auto: CPU (x86 ASM)" : "CPU (x86 ASM)";
    }
    return "CPU (C++)";
}

bool NoteRenderer::softwareOpenGl() {
    static const bool soft = [] {
        std::string r = sysinfo::hardware().gpu;
        for (char& c : r) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (const char* s : {"llvmpipe", "softpipe", "swiftshader", "software", "basic render"})
            if (r.find(s) != std::string::npos) return true;
        return false;
    }();
    return soft;
}

void NoteRenderer::setMode(int mode) {
    mode = std::clamp(mode, 0, 3);
    auto_ = mode == kAuto;
    if (auto_) mode = softwareOpenGl() ? kCpuAsm : kGpu;
    if (mode == mode_) return;
    mode_ = mode;
    if (mode_ == kGpu) unload();
}

void NoteRenderer::unload() {
    if (tex_.id) UnloadTexture(tex_);
    tex_ = {};
    layer_.resize(0, 0);
    sprites_.clear();
    wanted_.clear();
    inFrame_ = false;
}

void NoteRenderer::prepare(int maxPerFrame) {
    if (!cpu()) return;
    if (mode_ == kCpuAsm && !asmChecked_) {
        asmChecked_ = true;
        asmOk_ = raster::selfTest();  // a wrong result never reaches the screen
        if (!asmOk_) platform::log("note renderer: the x86-64 assembly differs from C++; using C++");
    }
    const uint32_t look = psychUi().generation();
    if (look != look_) {
        look_ = look;
        sprites_.clear();
    }
    for (int done = 0; done < maxPerFrame && !wanted_.empty(); ++done) {
        const Want w = wanted_.back();
        wanted_.pop_back();
        if (sprites_.size() > 256) sprites_.clear();  // sizes tweened by scripts: start over
        sprites_[keyOf(w.dir, w.px, w.psych)] = bake(w);
    }
}

raster::Sprite NoteRenderer::bake(const Want& w) const {
    raster::Sprite s;
    const int side = int(std::ceil(w.px * 1.35f)) + 4;  // Psych notes are a little taller than wide
    RenderTexture2D rt = LoadRenderTexture(side, side);
    if (!rt.id) return s;
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    // Premultiplied colours and the real coverage in alpha (the usual blend
    // would square the alpha of a picture drawn into an empty one).
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    const Vector2 c{side * 0.5f, side * 0.5f};
    const PsychUi& ui = psychUi();
    bool drawn = false;
    if (w.psych) {
        ui.beginDir(w.dir);
        drawn = ui.note(w.dir, c, float(w.px), 1.0f, 0.0f);
        ui.endDir();
    }
    if (!drawn) gfx::drawNote(w.dir, c, float(w.px), 1.0f, 1.0f, 0.0f);
    EndBlendMode();
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    UnloadRenderTexture(rt);
    if (!img.data) return s;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageFlipVertical(&img);  // render textures are stored upside down
    s.w = img.width;
    s.h = img.height;
    s.px.resize(size_t(s.w) * size_t(s.h));
    std::memcpy(s.px.data(), img.data, s.px.size() * 4);
    UnloadImage(img);
    raster::findSpans(s);  // blits skip the clear columns around the arrow
    return s;
}

void NoteRenderer::beginFrame(float pixelsPerUnit) {
    if (!cpu()) return;
    // Window pixels, in quarter steps (pictures stay reusable while a window
    // is resized), at most 1.5 per unit (1920x1080).
    const float scale = std::clamp(std::round(pixelsPerUnit * 4.0f) / 4.0f, 0.5f, 1.5f);
    if (scale != scale_) {
        scale_ = scale;
        sprites_.clear();
    }
    const int w = int((gfx::W + 2 * kMargin) * scale_), h = int((gfx::H + 2 * kMargin) * scale_);
    if (!tex_.id || layer_.width() != w || layer_.height() != h) {
        if (tex_.id) UnloadTexture(tex_);
        layer_.resize(w, h);
        Image img = GenImageColor(w, h, BLANK);
        tex_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(tex_, TEXTURE_FILTER_BILINEAR);
    }
    if (psychUi().generation() != look_) {
        look_ = psychUi().generation();
        sprites_.clear();
    }
    layer_.begin();
    t0_ = seconds();
    notes_ = 0;
    inFrame_ = tex_.id != 0;
}

bool NoteRenderer::note(int dir, Vector2 c, float size, float alpha, bool psychLook) {
    if (!inFrame_) return false;
    const int px = int(std::lround(size * scale_));
    if (px < 4 || px > 1024) return false;
    const auto it = sprites_.find(keyOf(dir, px, psychLook));
    if (it == sprites_.end()) {
        // Made before the next frame; the GPU draws it this time.
        const bool asked = std::any_of(wanted_.begin(), wanted_.end(),
                                       [&](const Want& w) { return w.dir == dir && w.px == px && w.psych == psychLook; });
        if (!asked && wanted_.size() < 64) wanted_.push_back({dir, px, psychLook});
        return false;
    }
    const raster::Sprite& s = it->second;
    if (s.w <= 0) return false;
    const int x = int(std::lround((c.x + kMargin) * scale_)) - s.w / 2;
    const int y = int(std::lround((c.y + kMargin) * scale_)) - s.h / 2;
    layer_.blit(s, x, y, alpha, usingAsm());
    ++notes_;
    return true;
}

void NoteRenderer::endFrame() {
    if (!inFrame_) return;
    inFrame_ = false;
    const int top = layer_.uploadTop(), bottom = layer_.uploadBottom();
    if (bottom > top)
        UpdateTextureRec(tex_, {0, float(top), float(layer_.width()), float(bottom - top)},
                         layer_.pixels() + size_t(top) * size_t(layer_.width()));
    lastMs_ = (seconds() - t0_) * 1000.0;
    lastNotes_ = notes_;
    lastPixels_ = layer_.blendedPixels();
    if (notes_ == 0) return;
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    DrawTexturePro(tex_, {0, 0, float(tex_.width), float(tex_.height)},
                   {-kMargin, -kMargin, gfx::W + 2 * kMargin, gfx::H + 2 * kMargin}, {0, 0}, 0, WHITE);
    EndBlendMode();
}

}  // namespace gh
