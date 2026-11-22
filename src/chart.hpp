// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Chart model shared by built-in songs and imported FNF-style charts.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "count.hpp"
#include "psych.hpp"

namespace gh {

enum NoteKind : uint8_t { kNormal = 0, kMine = 1 };
enum NoteFlag : uint8_t {
    kFlagNoAnim = 1,      // "No Animation"
    kFlagAlt = 2,         // "-alt" sing animation
    kFlagGF = 4,          // girlfriend sings it ("GF Sing", gfSection)
    kFlagHey = 8,         // "Hey!" note
    kFlagNoMissAnim = 16,
    kFlagIgnore = 32,     // passes without a miss (scripts: ignoreNote)
    kFlagBigCount = 64,   // counts for more notes than the note holds: Chart::bigCounts[count32()]
};

// 24 bytes, as charts hold millions of them.
struct Note {
    double time = 0;          // ms
    float length = 0;         // ms of sustain (0 = tap): exact to 1 ms for 4.6 hours
    uint16_t lane = 0;        // 0..keys-1 (up to 1023)
    uint8_t side : 1 = 0;     // 0 = opponent, 1 = player
    uint8_t kind : 7 = kNormal;
    uint8_t flags = 0;
    uint16_t type = 0;   // index into Chart::noteTypes (0 = default)
    // The notes of the file stacked here (same lane within 1 ms; a cell of an
    // iutk or God's Hand count chart): each one counts. Up to 4,294,967,295
    // in the note itself (stack, stackHi); more in Chart::bigCounts.
    uint16_t stack = 1;
    uint16_t stackHi = 0;
    uint32_t count32() const { return uint32_t(stack) | uint32_t(stackHi) << 16; }
    void setCount32(uint32_t c) {
        stack = uint16_t(c);
        stackHi = uint16_t(c >> 16);
    }
};
static_assert(sizeof(Note) == 24);

constexpr uint32_t kMaxStack = 0xFFFFFFFFu;  // in a note itself; more goes to Chart::bigCounts

struct TempoPoint { double timeMs = 0; double bpm = 120; double beat = 0; };
struct FocusPoint { double timeMs = 0; uint8_t side = 0; };
// A section of a legacy/Psych chart (Psych's SONG.notes[i]).
struct SectionInfo {
    double timeMs = 0;
    bool mustHit = true, gf = false, alt = false;
    double beats = 4, bpm = 0;  // length in beats, tempo in force (the chart editor keeps both)
};

struct Chart {
    std::string title;
    std::string difficulty;
    std::string opponent;  // character id (built-ins) or source name
    std::string player;
    std::string gf, stage;     // Psych scene fields (empty if absent)
    std::vector<std::string> noteTypes{""};
    std::vector<psych::ChartEvent> events;  // sorted by time
    int keys = 4;  // 1 to 1024 a side (the larger side's when they differ)
    // Leather Engine charts give each side its own key count ("keyCount" the
    // opponent's, "playerKeyCount" the player's): side 0 the opponent, side 1
    // the player; 0 = keys.
    int sideKeys[2] = {0, 0};
    int keysOf(int side) const { return sideKeys[side & 1] ? sideKeys[side & 1] : keys; }
    double speed = 2.0;        // FNF scroll speed (pixels/ms = 0.45 * speed)
    // Made by AutoChart (issue #36): played with the shame watermark, and
    // saved only as .ghac, which no other engine reads.
    bool autoCharted = false;
    std::vector<Note> notes;   // sorted by time
    std::vector<TempoPoint> tempo;
    std::vector<FocusPoint> focus;
    std::vector<SectionInfo> sections;  // empty for V-Slice charts and built-in songs
    double lengthMs = 0;       // end of the last note
    Count playerNotes, opponentNotes;  // stacked notes included, exact at any size
    Count rawNoteCount;  // notes in the file, before stacked notes were merged
    // Counts too large for a note (kFlagBigCount), up to 2^256 - 1 each.
    std::vector<Count> bigCounts;
    Count countOf(const Note& n) const {
        if (!(n.flags & kFlagBigCount)) return Count(n.count32() ? n.count32() : 1);
        return n.count32() < bigCounts.size() ? bigCounts[n.count32()] : Count(1);
    }
    void setCount(Note& n, const Count& c);  // in the note when it fits, else in bigCounts

    double bpmAt(double ms) const;
    double beatAt(double ms) const;
    double msAtBeat(double beat) const;
    double stepMs(double ms) const { return 15000.0 / bpmAt(ms); }
    int focusAt(double ms) const;  // side the camera should follow
    int sectionAt(double ms) const;  // index into sections (-1: before the first, or none)

    // Sorts notes, removes stacked duplicates, fills tempo beats and stats.
    void finalize(double baseBpm);
    void mirror();  // swap player/opponent (play as the opponent)
    uint16_t typeIndex(const std::string& name);
};

// Parses an FNF-family chart: legacy/Psych 0.x ("song.notes[].sectionNotes"),
// Psych 1.0 ("format": "psych_v1"), or V-Slice ("notes": {diff: [...]}) when
// metadataJson (the -metadata.json text) is given.
// extraKeys = false plays every chart with 4 keys (lane = value % 4), like
// Psych and JS Engine; the chart editor always keeps the chart's own keys.
bool parseChart(std::string_view json, const std::string& difficulty, Chart& out, std::string& err,
                std::string_view vsliceMetadata = {}, bool extraKeys = true);

bool readTextFile(const std::string& path, std::string& out);

struct StreamedNotes;
bool parseChartStreamed(std::string_view skeleton, const StreamedNotes& notes, const std::string& difficulty, Chart& out,
                        std::string& err, bool extraKeys = true);

}  // namespace gh
