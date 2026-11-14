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
    const PsychUi& ui = psychUi();
    // Room for the whole note: our arrows fill a square; a Psych skin's frame
    // may be wider or taller, and its trim offsets move it off centre.
    int bw = w.px + 4, bh = w.px + 4;
    if (w.psych) {
        const Vector2 e = ui.noteExtent(w.dir, float(w.px));
        bw = std::max(bw, int(std::ceil(e.x)) + 4);
        bh = std::max(bh, int(std::ceil(e.y)) + 4);
    }
    bw += bw & 1;  // even: the centre falls on a pixel corner, as note() places it
    bh += bh & 1;
    RenderTexture2D rt = LoadRenderTexture(bw, bh);
    if (!rt.id) return s;
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    // Premultiplied colours and the real coverage in alpha (the usual blend
    // would square the alpha of a picture drawn into an empty one).
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
    const Vector2 c{bw * 0.5f, bh * 0.5f};
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
        SetTextureFilter(tex_, smooth_ ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    }
    if (psychUi().generation() != look_) {
        look_ = psychUi().generation();
        sprites_.clear();
    }
    layer_.begin();
    t0_ = seconds();
    notes_ = pending_ = 0;
    pixels_ = 0;
    inFrame_ = tex_.id != 0;
}

void NoteRenderer::setSmooth(bool smooth) {
    if (smooth == smooth_) return;
    smooth_ = smooth;
    if (tex_.id) SetTextureFilter(tex_, smooth_ ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
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
    // A note reaching past the picture (moved there by a script, maybe
    // brought on screen by a camera) is drawn whole by the GPU instead.
    if (x < 0 || y < 0 || x + s.w > layer_.width() || y + s.h > layer_.height()) return false;
    layer_.blit(s, x, y, alpha, usingAsm());
    ++notes_;
    if (s.right <= s.left) return true;  // nothing visible
    // The box of its visible pixels, in canvas units.
    const float l = float(x + s.left) / scale_ - kMargin, t = float(y + s.top) / scale_ - kMargin;
    const float r = float(x + s.right) / scale_ - kMargin, b = float(y + s.bottom) / scale_ - kMargin;
    if (pending_++ == 0) {
        boxL_ = l, boxT_ = t, boxR_ = r, boxB_ = b;
    } else {
        boxL_ = std::min(boxL_, l), boxT_ = std::min(boxT_, t);
        boxR_ = std::max(boxR_, r), boxB_ = std::max(boxB_, b);
    }
    return true;
}

bool NoteRenderer::covers(Rectangle r) const {
    return inFrame_ && pending_ > 0 && r.x < boxR_ && r.x + r.width > boxL_ && r.y < boxB_ && r.y + r.height > boxT_;
}

// What was drawn or cleared since the last upload goes to the graphics
// card, so the texture always matches the picture: whole rows, or only the
// columns that changed when they are a small part of them.
void NoteRenderer::upload() {
    const int top = layer_.uploadTop(), bottom = layer_.uploadBottom();
    const int left = layer_.uploadLeft(), right = layer_.uploadRight();
    if (bottom <= top || right <= left) return;
    const int W = layer_.width();
    if ((right - left) * 3 > W * 2) {
        UpdateTextureRec(tex_, {0, float(top), float(W), float(bottom - top)}, layer_.pixels() + size_t(top) * size_t(W));
        return;
    }
    const size_t cols = size_t(right - left);
    staging_.resize(cols * size_t(bottom - top));
    for (int y = top; y < bottom; ++y)
        std::memcpy(&staging_[size_t(y - top) * cols], layer_.pixels() + size_t(y) * size_t(W) + size_t(left), cols * 4);
    UpdateTextureRec(tex_, {float(left), float(top), float(cols), float(bottom - top)}, staging_.data());
}

// Only the part of the picture with notes in it is drawn.
void NoteRenderer::drawLayer() {
    const float pad = 1.0f / scale_;  // a texel more, for the filtered edge
    const float l = std::max(-kMargin, boxL_ - pad), t = std::max(-kMargin, boxT_ - pad);
    const float r = std::min(gfx::W + kMargin, boxR_ + pad), b = std::min(gfx::H + kMargin, boxB_ + pad);
    if (r <= l || b <= t) return;
    const Rectangle src{(l + kMargin) * scale_, (t + kMargin) * scale_, (r - l) * scale_, (b - t) * scale_};
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    DrawTexturePro(tex_, src, {l, t, r - l, b - t}, {0, 0}, 0, WHITE);
    EndBlendMode();
    rlDrawRenderBatchActive();  // drawn before the texture changes again
}

void NoteRenderer::flush() {
    if (!inFrame_ || pending_ == 0) return;
    upload();
    drawLayer();
    pixels_ += layer_.blendedPixels();
    layer_.begin();  // the notes that follow go into a clear picture
    pending_ = 0;
}

void NoteRenderer::endFrame() {
    if (!inFrame_) return;
    inFrame_ = false;
    upload();  // also rows only cleared, so the next frame starts from a clear texture
    if (pending_ > 0) drawLayer();
    pending_ = 0;
    lastMs_ = (seconds() - t0_) * 1000.0;
    lastNotes_ = notes_;
    lastPixels_ = pixels_ + layer_.blendedPixels();
}

}  // namespace gh
