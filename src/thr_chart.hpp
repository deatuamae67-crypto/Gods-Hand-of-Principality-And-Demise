// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The threadripper engine's charts (.thr), as the strand engine reads them
// (issue #36). A zip archive (zip64 too), its entries stored, deflated or
// packed with Zstandard, holding:
//   <song>-metadata.json  the song's fields ("song": song, bpm, speed,
//       format...) and "tbl": "parts", each with its prefix, keyCount, note
//       types (null for the plain note), notesPerFile, noteFiles, noteCount;
//   notes/<prefix>sections.bin and notes/<prefix>notes-00000.bin, -00001...
// Each .bin: a 32-byte header - u32 magic ("TBLS" sections, "TBLN" notes),
// f64 at 8: its records, u32 at 24: flags (1: filtered) - then the records.
// A section is 40 bytes: u32 at 8 and at 32, its notes' low and high 32 bits;
// u32 at 12, flags (1: mustHitSection). A note is 16 bytes: f64 time (ms),
// f32 sustain length, u16 noteData, u16 type (an index into the part's types).
// Filtered: by blocks of 65536 records, byte k of every record together (byte
// planes), the records' first 8 bytes as 64-bit differences from the one
// before - what makes a long run of notes pack small.
// noteData as Psych: under keyCount is the side of the section's mustHit,
// or - "psych_v1" charts - the player's.
#pragma once

#include <string>

#include "chart.hpp"

namespace gh {

constexpr const char* kThrExt = ".thr";

bool isThrChart(const std::string& path);  // a zip archive named .thr
bool loadThrChart(const std::string& path, Chart& out, std::string& err);
// The chart as .thr (deflated, filtered): each counted note listed, up to
// 2^31 notes in all.
bool writeThrChart(const std::string& path, const Chart& chart, std::string& err);

}  // namespace gh
