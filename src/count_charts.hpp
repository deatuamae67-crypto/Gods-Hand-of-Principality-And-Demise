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
//   R-Slice's .raid ("RAID", versions 1 and 2): microsecond events in
//   deflated chunks of ops - literal runs, and instances of a table of
//   patterns repeated, moved in time, pitch and lane, grouped and clipped -
//   with a 16-bit stack an event; the chart's fields in a JSON section;
//   CRC-32s on the header, the sections and every chunk.
//
//   God's Hand's own .ghn ("GHN1", below): any number of notes.
//
//   The threadripper engine's .thr (thr_chart.hpp), as strand reads it.
//
// They play as the game's own charts: each cell is one note that counts as
// all of its notes (Chart::countOf), so a chart of trillions of notes is a
// chart of as many notes as it has cells.
//
// .ghn, God's Hand Notes (all integers little-endian; varints LEB128):
//   [0]  "GHN1", u32 version (1), u32 flags (0), u32 blocks,
//   [16] u64 meta offset, u32 meta packed size (0: stored as it is),
//        u32 meta size, u32 meta CRC-32,
//   [36] u64 directory offset, u32 directory size, u32 directory CRC-32,
//   [52] u64 data offset, [60] u32 CRC-32 of bytes 0-59.
//   Meta: JSON - title, characters, stage, keys a side, speed, note types,
//        tempo, camera focus, sections, Psych events, and the notes of each
//        side and of the file as decimal numbers of any length.
//   Directory, a block after another:
//     0 (notes): varint notes, raw size, packed size (0: stored as it is),
//        u32 CRC-32 of the raw bytes, zigzag first time (ns), varint offset
//        in the data.
//     1 (repeat): varint source block, varint copies (up to 2^64 - 1),
//        zigzag shift (ns), zigzag start (ns): copy j of the source's notes
//        moved to start + j * shift.
//   A notes block (deflated), in columns: zigzag time steps (ns), varint
//   lane * 2 + side, varint kind + flags * 128, varint type, varint length
//   (ns), and the count minus 1 as a varint of any length.
// Times in nanoseconds (292 years either way), 2048 lanes, sustains, note
// types and events; blocks decode on every core; copies that fall in the
// same millisecond of a lane fold into one note of all of them (the game
// judges a lane a millisecond at a time), so trillions of notes over a song
// load as at most a note a millisecond a lane.
#pragma once

#include <string>

#include "chart.hpp"

namespace gh {

// What kind of count chart the file is, from its first bytes (none: 0).
enum class CountChartKind { None = 0, Fnc1, Raid, Ghn, Thr };
constexpr const char* kCountChartExt = ".ghn";
// AutoChart's charts (issue #36): a .ghn marked as made by AutoChart, with
// its own magic ("GHAC"), so nothing but God's Hand reads it - and God's
// Hand plays it with the shame watermark and never converts it to another
// engine's format.
constexpr const char* kAutoChartExt = ".ghac";
CountChartKind countChartKind(const std::string& path);
inline bool isCountChart(const std::string& path) { return countChartKind(path) != CountChartKind::None; }
bool isAutoChartFile(const std::string& path);  // "GHAC": made by AutoChart

// Reads one into `out` (finalized). False with `err` set when the file is
// not one, is cut short or does not add up (counts, checksums).
bool loadCountChart(const std::string& path, Chart& out, std::string& err);

// The iutk engine's FNC1 version 3 (its converter's): for tests and for
// handing a chart to iutk. Lanes past 4 a side and lengths are not kept.
bool writeFnc1(const std::string& path, const Chart& chart, std::string& err);

// A chart as .ghn (as .ghac, "GHAC", when chart.autoCharted). What the
// writer did, for the converter's report.
struct GhnStats {
    uint64_t notes = 0;        // notes of the chart (cells)
    uint32_t blocks = 0, repeats = 0;  // notes blocks; repeat entries
    uint64_t bytes = 0;        // file size
    Count total;               // every note counted
};
bool writeGhn(const std::string& path, const Chart& chart, std::string& err, GhnStats* stats = nullptr);

// A chart as God's Hand's binary chart (.ghc), each counted note listed:
// up to 2^31 notes (more: keep it .ghn). notes: how many were listed.
bool writeChartAsGhc(const std::string& path, const Chart& chart, std::string& err, uint64_t* notes = nullptr);
// The same notes as a bigit chart (.ghb, bigit_chart.hpp).
bool writeChartAsGhb(const std::string& path, const Chart& chart, std::string& err, uint64_t* notes = nullptr);

// godshand --convert <chart> <out>: any chart the game reads to .ghn, .ghc,
// the iutk engine's .bin or the threadripper engine's .thr, by the output's
// extension. Prints what it did; 0 when it worked.
int convertChartFile(const std::string& in, const std::string& out);

}  // namespace gh
