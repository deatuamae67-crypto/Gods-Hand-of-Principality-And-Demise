// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Extra keys: charts of 1 to 18 keys a side, as Psych Engine Extra Keys
// (mania 0-17) and the extra keys scripts of JS Engine mods play them.
#pragma once

namespace gh {

constexpr int kMaxKeys = 18;

namespace ek {

// Arrow directions, lane by lane, for 10 to 18 keys (fewer keys: gfx::laneDir's
// own table, which is the same): 0 left, 1 down, 2 up, 3 right, 4 centre. The
// "sharp" arrows of the extra keys scripts point the same ways.
inline constexpr signed char kDirs[9][18] = {
    {0, 1, 2, 3, 4, 4, 0, 1, 2, 3},                          // 10
    {0, 1, 2, 3, 4, 2, 4, 0, 1, 2, 3},                       // 11
    {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},                    // 12
    {0, 1, 2, 3, 0, 1, 4, 2, 3, 0, 1, 2, 3},                 // 13
    {0, 1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 2, 3},              // 14
    {0, 1, 2, 3, 0, 2, 3, 4, 0, 1, 3, 0, 1, 2, 3},           // 15
    {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},        // 16
    {0, 1, 2, 3, 0, 1, 2, 3, 4, 0, 1, 2, 3, 0, 1, 2, 3},     // 17
    {0, 1, 2, 3, 0, 1, 2, 3, 4, 4, 0, 1, 2, 3, 0, 1, 2, 3},  // 18
};

// The keys each lane is played with, for 10 to 18 keys (raylib key codes:
// letters are their capital, ' ' is the space bar).
inline constexpr char kControls[9][19] = {
    "QWERVNUIOP",          // 10
    "QWERVBNUIOP",         // 11
    "QWERCVBNUIOP",        // 12
    "QWERCV BNUIOP",       // 13
    "QWERXCVBNMUIOP",      // 14
    "QWERXCV BNMUIOP",     // 15
    "QWERASDFHJKLYUIO",    // 16
    "QWERASDF HJKLYUIO",   // 17
    "QWERASDFVNHJKLUIOP",  // 18
};

// How wide one lane is (the extra keys scripts' Note.swagWidth): 112 up to
// 4 keys, then narrower so that 18 still fit on half the screen.
inline constexpr float kLaneWidth[kMaxKeys + 1] = {112, 112, 112, 112, 112, 98, 84, 77, 70, 63, 56, 50, 50, 46, 42, 39, 37, 35, 33};

}  // namespace ek
}  // namespace gh
