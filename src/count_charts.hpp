// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Charts that count their notes instead of listing every one (issue #36):
// every (time, lane) holds how many notes are stacked there.
//
//   iutk engine's .bin ("FNC1", versions 2, 3 and 4): 8 lanes (0-3 the
//   opponent's, 4-7 the player's), times in whole milliseconds, a 64-bit
//   count a cell. Version 4 packs the three columns as run-length varints
//   with a CRC-32 each.
//
// They play as the game's own charts: each cell is one note that counts as
// all of its notes (Chart::countOf), so a chart of trillions of notes is a
// chart of as many notes as it has cells.
#pragma once

#include <string>

#include "chart.hpp"

namespace gh {

// What kind of count chart the file is, from its first bytes (none: 0).
enum class CountChartKind { None = 0, Fnc1 };
CountChartKind countChartKind(const std::string& path);
inline bool isCountChart(const std::string& path) { return countChartKind(path) != CountChartKind::None; }

// Reads one into `out` (finalized). False with `err` set when the file is
// not one, is cut short or does not add up (counts, checksums).
bool loadCountChart(const std::string& path, Chart& out, std::string& err);

// The iutk engine's FNC1 version 3 (its converter's): for tests and for
// handing a chart to iutk. Lanes past 4 a side and lengths are not kept.
bool writeFnc1(const std::string& path, const Chart& chart, std::string& err);

}  // namespace gh
