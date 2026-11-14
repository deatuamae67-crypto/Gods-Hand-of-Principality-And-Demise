// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The processor note renderer: the x86-64 assembly blend gives exactly the
// C++ one's pixels, and the layer clips, clears and reports its rows.
// gh_tests --raster runs only these (the Windows CI does, for the MSVC assembly).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "note_raster.hpp"

using namespace gh;

extern int g_fail, g_pass;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) ++g_pass;                                                      \
        else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

namespace {

uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return r | (g << 8) | (b << 16) | (a << 24); }

uint32_t randomPremultiplied(std::mt19937& rng) {
    const uint32_t a = rng() & 255u;
    return rgba(a ? rng() % (a + 1) : 0, a ? rng() % (a + 1) : 0, a ? rng() % (a + 1) : 0, a);
}

void testBlendValues() {
    using raster::blendRowC;
    // Opaque source at full alpha replaces the pixel; a clear source leaves it.
    uint32_t d = rgba(10, 20, 30, 255);
    const uint32_t opaque = rgba(200, 100, 50, 255);
    blendRowC(&d, &opaque, 1, 256);
    CHECK(d == opaque);
    d = rgba(10, 20, 30, 255);
    const uint32_t clear = 0;
    blendRowC(&d, &clear, 1, 256);
    CHECK(d == rgba(10, 20, 30, 255));
    // Alpha 0: nothing changes. Half alpha over black: half the colour.
    d = rgba(10, 20, 30, 255);
    blendRowC(&d, &opaque, 1, 0);
    CHECK(d == rgba(10, 20, 30, 255));
    d = rgba(0, 0, 0, 255);
    blendRowC(&d, &opaque, 1, 128);
    CHECK(d == rgba(100, 50, 25, 255));
    // Premultiplied half-transparent white over an empty layer stays as it is.
    d = 0;
    const uint32_t halfWhite = rgba(128, 128, 128, 128);
    blendRowC(&d, &halfWhite, 1, 256);
    CHECK(d == halfWhite);
}

void testAsmMatchesC() {
    CHECK(raster::selfTest());
    std::mt19937 rng(77);
    std::vector<uint32_t> src(41), a(42), b(42);
    int mismatches = 0;
    for (uint32_t alpha = 0; alpha <= 256; ++alpha) {
        for (size_t n = 1; n <= 40; n += 3) {
            for (uint32_t& p : src) p = randomPremultiplied(rng);
            for (size_t i = 0; i < a.size(); ++i) a[i] = b[i] = rng();
            const size_t off = n & 1;  // also pixels that start between 8-byte boundaries
            raster::blendRowC(a.data() + off, src.data(), n, alpha);
            raster::blendRowAsm(b.data() + off, src.data(), n, alpha);
            if (std::memcmp(a.data(), b.data(), a.size() * 4) != 0) ++mismatches;
        }
    }
    CHECK(mismatches == 0);
    std::printf("  note renderer: %s\n", raster::haveAsm() ? "x86-64 assembly (SSE2) matches C++" : "C++ only on this processor");
}

void testLayer() {
    raster::Layer l;
    l.resize(10, 8);
    CHECK(l.width() == 10 && l.height() == 8);
    raster::Sprite s;
    s.w = 4;
    s.h = 3;
    s.px.assign(12, rgba(255, 255, 255, 255));
    l.begin();
    CHECK(l.uploadTop() == l.uploadBottom());  // nothing to send
    l.blit(s, -2, 6, 1.0f, true);              // clipped on the left and at the bottom
    CHECK(l.blendedPixels() == 4);             // 2 columns x 2 rows
    CHECK(l.uploadTop() == 6 && l.uploadBottom() == 8);
    CHECK(l.pixels()[6 * 10 + 0] == rgba(255, 255, 255, 255) && l.pixels()[6 * 10 + 2] == 0);
    l.blit(s, 20, 0, 1.0f, false);  // outside: nothing
    CHECK(l.blendedPixels() == 4);
    // Next frame: those rows are cleared, and sent again with the new ones.
    l.begin();
    CHECK(l.pixels()[6 * 10 + 0] == 0);
    l.blit(s, 3, 1, 0.5f, false);
    CHECK(l.uploadTop() == 1 && l.uploadBottom() == 8);
    CHECK(l.pixels()[1 * 10 + 3] == rgba(127, 127, 127, 127));  // 255 * 128 / 256, rounded down
    l.begin();
    CHECK(l.uploadTop() == 1 && l.uploadBottom() == 4);  // only the rows to clear
    l.begin();
    CHECK(l.uploadTop() == l.uploadBottom());
}

void testSpans() {
    raster::Sprite s;
    s.w = 5;
    s.h = 3;
    s.px.assign(15, 0u);
    s.px[1 * 5 + 1] = s.px[1 * 5 + 3] = rgba(255, 255, 255, 255);  // middle row: columns 1..3
    raster::findSpans(s);
    CHECK(s.spans.size() == 3 && s.spans[0] == 0 && s.spans[2] == 0);
    CHECK((s.spans[1] & 0xFFFFu) == 1 && (s.spans[1] >> 16) == 4);
    // With spans, only those pixels are blended, and the same pixels result.
    raster::Layer a, b;
    a.resize(8, 6);
    b.resize(8, 6);
    a.begin();
    b.begin();
    raster::Sprite full = s;
    full.spans.clear();
    a.blit(s, 2, 1, 1.0f, true);
    b.blit(full, 2, 1, 1.0f, true);
    CHECK(a.blendedPixels() == 3 && b.blendedPixels() == 15);
    CHECK(std::memcmp(a.pixels(), b.pixels(), 8 * 6 * 4) == 0);
    CHECK(a.uploadTop() == 2 && a.uploadBottom() == 3);  // only the row with something in it
}

// How long a screen of notes takes each way (printed, not checked).
void timeBlends() {
    std::mt19937 rng(5);
    raster::Sprite s;
    s.w = s.h = 160;
    s.px.resize(size_t(s.w) * s.h);
    for (uint32_t& p : s.px) p = randomPremultiplied(rng);
    raster::Layer l;
    l.resize(1408, 848);
    for (int useAsm = 0; useAsm < 2; ++useAsm) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 20; ++frame) {
            l.begin();
            for (int i = 0; i < 400; ++i) l.blit(s, int(rng() % 1300), int(rng() % 760), 0.9f, useAsm != 0);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
        std::printf("  400 notes of 160 px: %.2f ms a frame with %s\n", ms, useAsm ? "the assembly" : "C++");
    }
}

}  // namespace

void runRasterTests(bool timings) {
    testBlendValues();
    testAsmMatchesC();
    testLayer();
    testSpans();
    if (timings) timeBlends();
}
