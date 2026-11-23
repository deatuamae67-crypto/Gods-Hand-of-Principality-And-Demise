// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// AutoChart's training (issue #36, optional): every charted song of the
// installed mods teaches the model (autochart.hpp) once - its chart, its
// instrumental and its voices. A run goes on from the model saved before and
// saves as it goes, so it can be stopped and taken up again, and it learns
// the songs added since.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "song_scan.hpp"

namespace gh {

struct TrainProgress {
    std::atomic<int> done{0}, total{0}, learned{0}, skipped{0};
    std::atomic<unsigned long long> notes{0};  // note frames learned from
    std::atomic<bool> stop{false}, running{false};
    std::mutex mtx;
    std::string current;  // the song being learned
    std::string message;  // how it ended
};

// The songs a run would learn from: charted, with their own audio, not built in.
std::vector<const SongEntry*> trainableSongs(const std::vector<SongEntry>& songs);

// Learns from `songs` (those the model at modelPath does not know yet) and
// saves it there. Returns the songs learned in this run.
int trainAutoChart(const std::vector<SongEntry>& songs, const std::string& modelPath, TrainProgress& prog);

}  // namespace gh
