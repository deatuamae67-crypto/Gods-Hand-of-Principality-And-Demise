// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "note_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

#if GH_X86_ASM
// src/asm/blend_x64.S (GCC, Clang) or src/asm/blend_x64.asm (MSVC).
extern "C" void gh_blend_row_sse2(uint32_t* dst, const uint32_t* src, size_t n, uint32_t alpha256);
#define GH_ASM_ROW gh_blend_row_sse2
#define GH_ASM_NAME "x86-64 SSE2"
#elif GH_ARM_ASM
// src/asm/blend_arm64.S (Android, Apple Silicon, ARM Linux).
extern "C" void gh_blend_row_neon(uint32_t* dst, const uint32_t* src, size_t n, uint32_t alpha256);
#define GH_ASM_ROW gh_blend_row_neon
#define GH_ASM_NAME "ARM64 NEON"
#endif

namespace gh::raster {

void blendRowC(uint32_t* dst, const uint32_t* src, size_t n, uint32_t alpha256) {
    for (size_t i = 0; i < n; ++i) {
        const uint32_t s = src[i];
        uint32_t c[4];
        for (int k = 0; k < 4; ++k) c[k] = (((s >> (8 * k)) & 255u) * alpha256) >> 8;
        const uint32_t inv = 255u - c[3];
        const uint32_t d = dst[i];
        uint32_t out = 0;
        for (int k = 0; k < 4; ++k) {
            uint32_t t = ((d >> (8 * k)) & 255u) * inv + 128u;  // x / 255, rounded: (t + (t >> 8)) >> 8
            t = (t + (t >> 8)) >> 8;
            out |= std::min(255u, c[k] + t) << (8 * k);
        }
        dst[i] = out;
    }
}

void blendRowAsm(uint32_t* dst, const uint32_t* src, size_t n, uint32_t alpha256) {
#if defined(GH_ASM_ROW)
    GH_ASM_ROW(dst, src, n, alpha256);
#else
    blendRowC(dst, src, n, alpha256);
#endif
}

bool haveAsm() {
#if defined(GH_ASM_ROW)
    return true;
#else
    return false;
#endif
}

const char* asmName() {
#if defined(GH_ASM_ROW)
    return GH_ASM_NAME;
#else
    return "";
#endif
}

bool selfTest() {
    if (!haveAsm()) return true;
    std::mt19937 rng(1234);
    std::vector<uint32_t> src(67), a(67), b(67);
    for (int round = 0; round < 64; ++round) {
        for (size_t i = 0; i < src.size(); ++i) {
            // Premultiplied: colour channels never above alpha.
            const uint32_t al = rng() & 255u;
            uint32_t p = al << 24;
            for (int k = 0; k < 3; ++k) p |= (al ? rng() % (al + 1) : 0u) << (8 * k);
            src[i] = p;
            a[i] = b[i] = rng();
        }
        const uint32_t alpha = round == 0 ? 256u : uint32_t(rng() % 257u);
        const size_t n = 1 + size_t(round) % 66;  // odd and even counts: the assembly's last pixel
        const size_t off = size_t(round) & 1;     // and pixels not on 8-byte boundaries
        blendRowC(a.data() + off, src.data(), n, alpha);
        blendRowAsm(b.data() + off, src.data(), n, alpha);
        if (std::memcmp(a.data(), b.data(), a.size() * 4) != 0) return false;
    }
    return true;
}

void findSpans(Sprite& s) {
    s.spans.assign(size_t(std::max(0, s.h)), 0u);
    s.left = s.w, s.top = s.h, s.right = 0, s.bottom = 0;
    for (int y = 0; y < s.h; ++y) {
        const uint32_t* row = &s.px[size_t(y) * size_t(s.w)];
        int x0 = 0, x1 = s.w;
        while (x0 < x1 && (row[x0] >> 24) == 0) ++x0;
        while (x1 > x0 && (row[x1 - 1] >> 24) == 0) --x1;
        s.spans[size_t(y)] = x0 < x1 ? uint32_t(x0) | uint32_t(x1) << 16 : 0u;
        if (x0 < x1) {
            s.left = std::min(s.left, x0), s.right = std::max(s.right, x1);
            s.top = std::min(s.top, y), s.bottom = y + 1;
        }
    }
    if (s.right <= s.left) s.left = s.top = s.right = s.bottom = 0;
}

void Layer::resize(int w, int h) {
    if (w == w_ && h == h_) return;
    w_ = std::max(0, w);
    h_ = std::max(0, h);
    px_.assign(size_t(w_) * size_t(h_), 0u);  // the caller makes a new, empty texture too
    top_ = prevTop_ = h_;
    bottom_ = prevBottom_ = 0;
    left_ = prevLeft_ = w_;
    right_ = prevRight_ = 0;
}

void Layer::begin() {
    // Clear what the last frame drew; those rows go to the graphics card again, empty.
    if (bottom_ > top_ && right_ > left_)
        for (int y = top_; y < bottom_; ++y) std::memset(&px_[size_t(y) * w_ + left_], 0, size_t(right_ - left_) * 4);
    prevTop_ = top_;
    prevBottom_ = bottom_;
    prevLeft_ = left_;
    prevRight_ = right_;
    top_ = h_;
    bottom_ = 0;
    left_ = w_;
    right_ = 0;
    blended_ = 0;
}

void Layer::blit(const Sprite& s, int x, int y, float alpha, bool useAsm) {
    if (s.w <= 0 || s.h <= 0 || !(alpha > 0.0f)) return;
    const uint32_t a256 = uint32_t(std::lround(std::min(alpha, 1.0f) * 256.0f));
    if (a256 == 0) return;
    const int x0 = std::max(0, x), x1 = std::min(w_, x + s.w);
    const int y0 = std::max(0, y), y1 = std::min(h_, y + s.h);
    if (x0 >= x1 || y0 >= y1) return;
    void (*row)(uint32_t*, const uint32_t*, size_t, uint32_t) = useAsm ? blendRowAsm : blendRowC;
    const bool spans = s.spans.size() == size_t(s.h);
    int drawnTop = y1, drawnBottom = y0;
    for (int yy = y0; yy < y1; ++yy) {
        int a = x0, b = x1;  // this row's columns, in the layer
        if (spans) {
            const uint32_t sp = s.spans[size_t(yy - y)];
            if (!sp) continue;  // fully transparent row
            a = std::max(a, x + int(sp & 0xFFFFu));
            b = std::min(b, x + int(sp >> 16));
            if (a >= b) continue;
        }
        row(&px_[size_t(yy) * w_ + a], &s.px[size_t(yy - y) * s.w + size_t(a - x)], size_t(b - a), a256);
        blended_ += uint64_t(b - a);
        drawnTop = std::min(drawnTop, yy);
        drawnBottom = yy + 1;
    }
    if (drawnTop >= drawnBottom) return;
    top_ = std::min(top_, drawnTop);
    bottom_ = std::max(bottom_, drawnBottom);
    left_ = std::min(left_, x0);
    right_ = std::max(right_, x1);
}

int Layer::uploadTop() const {
    const bool now = bottom_ > top_, before = prevBottom_ > prevTop_;
    if (now && before) return std::min(top_, prevTop_);
    return now ? top_ : (before ? prevTop_ : 0);
}

int Layer::uploadBottom() const {
    const bool now = bottom_ > top_, before = prevBottom_ > prevTop_;
    if (now && before) return std::max(bottom_, prevBottom_);
    return now ? bottom_ : (before ? prevBottom_ : 0);
}

int Layer::uploadLeft() const {
    const bool now = bottom_ > top_, before = prevBottom_ > prevTop_;
    if (now && before) return std::min(left_, prevLeft_);
    return now ? left_ : (before ? prevLeft_ : 0);
}

int Layer::uploadRight() const {
    const bool now = bottom_ > top_, before = prevBottom_ > prevTop_;
    if (now && before) return std::max(right_, prevRight_);
    return now ? right_ : (before ? prevRight_ : 0);
}

}  // namespace gh::raster
