// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Options > GRAPHICS > Note renderer: notes drawn by the graphics card (GPU,
// the usual way) or by the processor (CPU), with the x86-64 assembly routine
// or with C++. On the processor every note is a picture made once (with the
// note skin, its colours and size) and blended into a picture of the screen
// (note_raster), which reaches the graphics card in one piece. Notes that are
// turned, mines and lanes turned by scripts stay on the GPU.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "note_raster.hpp"
#include "raylib.h"

namespace gh {

class NoteRenderer {
public:
    enum Mode { kGpu = 0, kCpuAsm = 1, kCpuC = 2, kAuto = 3 };
    // Auto: the GPU, unless OpenGL itself runs on the processor (Mesa's
    // llvmpipe, SwiftShader, Windows' basic driver...), where the assembly
    // renderer is faster than the GL one.
    static bool softwareOpenGl();
    ~NoteRenderer();
    void setMode(int mode);
    int mode() const { return mode_; }
    bool cpu() const { return mode_ != kGpu; }
    bool usingAsm() const { return mode_ == kCpuAsm && asmOk_ && raster::haveAsm(); }
    const char* name() const;
    // Before drawing (no camera or render target active): makes the pictures
    // of notes asked for and not seen before.
    void prepare(int maxPerFrame = 8);
    // Around the notes, in canvas units; pixelsPerUnit = window pixels per unit.
    void beginFrame(float pixelsPerUnit);
    // false: not drawn here (the caller draws it on the GPU).
    bool note(int dir, Vector2 c, float size, float alpha, bool psychLook);
    // Puts the notes collected so far on screen now, so something the GPU
    // draws next (a receptor, a hold, a mine, a turned note) covers them as
    // before. Call it with no shader active and outside turned lanes.
    void flush();
    bool pending() const { return pending_ > 0; }
    // Whether r (canvas units) touches the notes collected since the last
    // flush: only then does drawing r on the GPU need a flush first.
    bool covers(Rectangle r) const;
    void endFrame();  // the rest goes to the graphics card and on screen
    void setSmooth(bool smooth);  // Options > Anti-aliasing, for a zoomed HUD
    void unload();
    // The last frame, for the F3 overlay.
    double lastMs() const { return lastMs_; }
    int lastNotes() const { return lastNotes_; }
    uint64_t lastPixels() const { return lastPixels_; }

private:
    struct Want {
        int dir, px;
        bool psych;
    };
    static uint64_t keyOf(int dir, int px, bool psych) { return uint64_t(dir & 15) | uint64_t(px) << 4 | uint64_t(psych) << 20; }
    raster::Sprite bake(const Want& w) const;
    void upload();
    void drawLayer();
    int mode_ = kGpu;
    bool auto_ = false;  // the mode was picked by Auto
    bool asmOk_ = true, asmChecked_ = false, inFrame_ = false;
    float scale_ = 1.0f;
    raster::Layer layer_;
    Texture2D tex_{};
    std::unordered_map<uint64_t, raster::Sprite> sprites_;
    std::vector<Want> wanted_;
    uint32_t look_ = 0;  // the Psych interface's look the pictures were made with
    double t0_ = 0, lastMs_ = 0;
    int notes_ = 0, lastNotes_ = 0, pending_ = 0;
    float boxL_ = 0, boxT_ = 0, boxR_ = 0, boxB_ = 0;  // around the pending notes, canvas units
    std::vector<uint32_t> staging_;                    // a part of the picture on its way to the GPU
    uint64_t pixels_ = 0, lastPixels_ = 0;
    bool smooth_ = true;
};

}  // namespace gh
