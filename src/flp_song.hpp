// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// A whole FL Studio project as one chart, as SNIFF reads a pattern
// (https://github.com/JordanSantiagoYT/SNIFF) but over the song's playlist -
// SNIFF's own "convert from playlist" to-do (issue #36): every clip of every
// pattern, in its window, with every note of every channel.
//
//   Lanes: SNIFF's keys are C, C#, D and D# for left, down, up and right;
//   the same keys of every octave give the same lanes (the lane is the
//   pitch modulo 4), so no note is left out.
//   Sides: a channel and its "(Alt)" twin are one instrument; the first
//   instrument with notes in the rack is the player's, the next the
//   opponent's (or the channels given).
//   Holds: SNIFF's rule - played softly (velocity under 64) or a beat long
//   or more, the note holds for its length less a step.
//   Alt Animation: notes of an "(Alt)" channel, and portamento notes (as
//   SNIFF's Rust version marks them).
//
// Projects of gigabytes stream through: each pattern keeps its distinct
// notes with how many times each is there, and the playlist's clips add
// their counts, so a chart of trillions of notes is a chart of as many
// notes as it has (time, side, lane) cells. Reads FL Studio 20 to 26.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "chart.hpp"

namespace gh {

struct FlpSongOptions {
    std::vector<int> playerChannels, opponentChannels;  // empty: by instrument, as above
};

struct FlpSongStats {
    std::string version;        // FL Studio's, as saved in the project
    int ppq = 0;
    double bpm = 0;
    uint64_t noteRecords = 0;   // notes in the patterns
    uint64_t clips = 0;         // pattern clips in the playlist
    uint64_t cells = 0;         // (time, side, lane, hold, alt) cells of the chart
    Count notes, player, opponent;
    std::vector<std::string> channels;  // the rack: "name (player)", ...
};

// progress: 0..1 through the file (may be empty).
bool flpSongToChart(const std::string& path, Chart& out, std::string& err, const FlpSongOptions& opt = {},
                    FlpSongStats* stats = nullptr, const std::function<void(double)>& progress = {});

}  // namespace gh
