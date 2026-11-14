// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// God's Hand binary charts (.ghc, issue #22): a Psych chart in a fraction of
// its JSON size, read many times faster.
//
// What the JSON holds besides the notes (song data, sections, events...) is
// kept as it is, as JSON (compressed); the notes are columns (times, lengths,
// lanes, stacks, types) of small numbers - each time as the change of the
// gap between notes, in nanoseconds - packed with deflate in blocks of
// 262144 notes, which load on every core at once. Deflate and not a
// stronger packer: past this point a smaller file costs more time to unpack
// than it saves to read.
#pragma once

#include <cstdint>
#include <string>

#include "chart_stream.hpp"

namespace gh {

constexpr const char* kBinaryChartExt = ".ghc";

// The file starts like a binary chart.
bool isBinaryChart(const std::string& path);

// level: deflate's, 1 (fastest) to 9 (smallest).
bool writeBinaryChart(const std::string& path, const std::string& skeleton, const StreamedNotes& sn, int level,
                      std::string& err);
// What streamChartSkeleton gives for the chart it was made from.
bool readBinaryChart(const std::string& path, std::string& skeleton, StreamedNotes& sn, std::string& err, int threads = -1);
// The JSON part only (song name, characters... for the song list).
bool readBinarySkeleton(const std::string& path, std::string& skeleton);

// Any chart the game reads (Psych JSON, PulseForge...) to a .ghc file.
struct ConvertStats {
    uint64_t inBytes = 0, outBytes = 0, notes = 0;
    double seconds = 0;
};
bool convertChartToBinary(const std::string& src, const std::string& dst, std::string& err, ConvertStats* stats = nullptr,
                          int level = 3);

}  // namespace gh
