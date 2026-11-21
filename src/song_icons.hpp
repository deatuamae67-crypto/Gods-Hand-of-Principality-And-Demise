// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Freeplay's song icons (issue #36): a mod song shows its opponent's health
// icon, the way Psych Engine's Freeplay does - the character its chart names
// ("player2"), that character's "healthicon" (characters/<name>.json), and
// images/icons/icon-<icon>.png from the song's own mod or any other one.
#pragma once

#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"
#include "song_scan.hpp"

namespace gh {

// The icons of a list of songs, looked up and decoded on threads of their
// own (a few at a time), the textures made on the drawing thread.
class SongIcons {
public:
    ~SongIcons() { clear(); }
    // The icon of `e`, or nullptr while it loads, for built-in songs and for
    // songs without one. frames: how many frames the picture holds side by
    // side (Psych icons: normal, losing and sometimes winning).
    const Texture2D* get(const SongEntry& e, int* frames = nullptr);
    void clear();

private:
    struct Slot {
        bool done = false;
        Texture2D tex{};
        int frames = 1;
        std::future<Image> job;
    };
    std::unordered_map<std::string, Slot> slots_;
    int busy_ = 0;
};

// Draws frame 0 of a Psych icon in a size x size square centred on c.
void drawSongIcon(const Texture2D& tex, int frames, Vector2 c, float size, float alpha = 1.0f);

}  // namespace gh
