// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Issue #24: the audio visualizer of a song, in the top right corner. Its bars
// follow the music and its colours follow the chart: whose turn it is, how
// thick the notes are, the player's hits and misses, and their health.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "raylib.h"
#include "spectrum.hpp"

namespace gh {

class Mixer;

class AudioVisualizer {
public:
    struct State {           // what the chart is doing now
        Color side[2]{};      // the opponent's and the player's colours (their health bar's)
        int focus = 1;        // whose section it is: 0 the opponent, 1 the player
        float nps = 0;        // notes a second around now, both sides
        float health = 1;     // 0..2
        bool playing = true;  // the song is going
    };
    // Once a frame. `behind`: how many samples the newest mixed one is ahead
    // of what is heard now (the device's buffer).
    void update(double dt, const Mixer& mixer, size_t behind, const State& st);
    void hit(int rating);  // the player hit a note: 0 the best rating .. 3 the worst
    void opponentHit();
    void miss();
    void beat();
    void draw(Rectangle area, float alpha) const;

private:
    Spectrum spec_{40, 4096};
    std::vector<float> buf_;
    float base_[3] = {0.6f, 0.4f, 1.0f};  // the colour of whose turn it is, eased (0..1 RGB)
    Color side_[2]{};
    float heat_ = 0;    // how thick the notes are, 0..1
    float spam_ = 0;    // spam: the bars turn to a running rainbow, 0..1
    float danger_ = 0;  // low health, 0..1
    float hitPulse_ = 0, oppPulse_ = 0, missPulse_ = 0, beatPulse_ = 0;
    Color hitColor_{255, 255, 255, 255};
    double t_ = 0;
};

}  // namespace gh
