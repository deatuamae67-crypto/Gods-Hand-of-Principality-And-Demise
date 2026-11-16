// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Animated backgrounds for each song.
#pragma once

#include <vector>

#include "gfx.hpp"

namespace gh {

class Stage {
public:
    // kind: 0 the school rooftop at sunset, 1 the club, 2 the punk alley,
    // 3 the synthwave night (custom songs, in the song's colour)
    void init(int kind, Color accent, bool lowDetail, bool flashing);
    void update(float dt, double now);
    void onBeat(int beat);
    void drawBack(double now) const;
    void drawFront(double now) const;
    void drawOverlay() const;  // screen-space flashes
    int kind() const { return kind_; }

private:
    void drawRooftop(float t) const;
    void drawClub(float t) const;
    void drawAlley(float t) const;
    void drawSynthwave(float t) const;

    int kind_ = 0;
    Color accent_ = WHITE;
    bool low_ = false, flashing_ = true;
    float flash_ = 0, pulse_ = 0;
    int beat_ = 0;
    struct Star { float x, y, s, ph; };
    std::vector<Star> stars_;
    struct Block { float x, w, h; };  // the skyline's buildings
    std::vector<Block> far_, near_;
    gfx::Particles motes_;
    float spawn_ = 0;
};

}  // namespace gh
