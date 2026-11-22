// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through
// issues.

// JSON merging (issue #36): a chart split into parts - as players do to get
// past JS Engine's 16-million-note limit - merged back into one Psych chart,
// with no limit on its notes.
//
// Parts come in two shapes, and both merge:
//   Overlay - every part is the whole song, each holding some of its notes:
//     the sections are put together index by index, every part's notes in
//     them.
//   Append - each part is a stretch of the song after the one before: the
//     sections follow one another, and a part whose notes start again from
//     0 is moved to where the parts before it end.
// Auto picks the shape from the parts themselves.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gh {

enum class MergeMode { Auto = 0, Overlay = 1, Append = 2 };
const char* mergeModeName(MergeMode m);

// What a part holds, for the list.
struct MergePart {
    std::string path, name;
    uint64_t bytes = 0;
    size_t sections = 0;
    uint64_t notes = 0;               // as the file lists them
    double firstMs = 0, lastMs = 0;   // its first and last note
    double spanMs = 0;                // how long its sections last
    std::string song;                 // the song name it gives
    bool ok = false;
    std::string err;
};
MergePart inspectPart(const std::string& path);

// The order the names give: "song-1", "song-2", ..., "song-10" (the number
// that ends the name, as a number), else the names' own order.
void orderParts(std::vector<MergePart>& parts);

// The shape Auto would pick for these parts.
MergeMode detectMergeMode(const std::vector<MergePart>& parts);

struct MergeResult {
    uint64_t notes = 0;
    size_t sections = 0, events = 0;
    MergeMode mode = MergeMode::Auto;  // what was used
    uint64_t bytes = 0;
    double seconds = 0;
};
// Merges the parts, in this order, into `out` (a Psych chart), written as it
// is made: any number of notes, in the memory of one part (a big part's
// notes wait in a temporary file next to `out`). progress (0..1), when
// given, follows it; setting *cancel stops it, leaving no file.
bool mergeChartParts(const std::vector<std::string>& parts, const std::string& out, MergeMode mode, MergeResult& res,
                     std::string& err, std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

// godshand --merge <out.json> <part> ... [--merge-mode auto|overlay|append]:
// the parts in that order (as given), merged; prints what it did. 0 when it
// worked.
int mergeChartFiles(const std::string& out, const std::vector<std::string>& parts, const std::string& mode);

// Parts with more notes than this keep them on disk while merging (tests
// lower it).
extern size_t g_mergeSpillAbove;

}  // namespace gh
