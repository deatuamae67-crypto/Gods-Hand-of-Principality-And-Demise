// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Chart files of any size (see chart_stream.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "chart.hpp"

namespace gh {

// noteData values a chart may use: the lanes of both sides, up to 2 x 100
// keys (and the values past them, which fold back), all in one byte.
constexpr int kNoteDataValues = 256;

struct StreamedNote {
    static constexpr uint16_t kAltBit = 0x8000;
    double time = 0;
    float length = 0;
    uint8_t data = 0;   // raw noteData (0..255, lane + side half)
    uint8_t stack = 1;  // notes of the file merged here (same value within 1 ms)
    // Bits 0-14: 1-based index into StreamedNotes::types (0 = none); bit 15
    // (kAltBit): a legacy `true` in slot 3 (an alt animation note).
    uint16_t type = 0;
    int noteData() const { return data; }
    bool alt() const { return (type & kAltBit) != 0; }
    uint16_t typeIdx() const { return uint16_t(type & ~kAltBit); }
};

struct StreamedNotes {
    std::vector<StreamedNote> notes;
    std::vector<uint32_t> rangeStart;  // per sectionNotes array, plus a final end marker
    std::vector<std::string> types;
    int maxData = -1;
    std::array<uint64_t, kNoteDataValues> dataCount{};  // notes of the file per noteData value
    uint64_t rawCount = 0;  // notes in the file before stacked notes were merged
    uint16_t typeIndex(const std::string& name);
};

// Reads the file, sending every sectionNotes array to `notes` and the rest of
// the JSON to `skeleton` (with {"__gh_stream": k} where array k was). In a
// PulseForge chart the top-level "notes" list is streamed the same way, each
// note stored as lane + 20 * owner (0 player, 1 opponent, 2 girlfriend).
// threads: worker threads for the sections' notes (-1: one less than the
// processor's cores, at most 8; 0: all on this thread). sectionCap: sections
// bigger than this many bytes are parsed on this thread, piece by piece
// (0: 8 MB). Either way the result is the same.
bool streamChartSkeleton(const std::string& path, std::string& skeleton, StreamedNotes& notes, std::string& err,
                         int threads = -1, size_t sectionCap = 0);

// What streamChartSkeleton reads from a chart's JSON, built from notes that
// are already in memory: the chart editor saves a .ghc this way, without
// writing the JSON first. Each sectionNotes array starts with beginArray(),
// whose marker goes in the skeleton where the array would be; add() takes
// the notes of the array one by one, in the file's order, and stacks them as
// reading does (the same noteData within 1 ms, up to 255 on one note);
// finish() closes the last array.
class StreamedNotesBuilder {
public:
    explicit StreamedNotesBuilder(StreamedNotes& out);
    std::string beginArray();
    void add(double time, int noteData, double length, const std::string& type = {});
    void finish();

private:
    StreamedNotes& out_;
    std::vector<double> lastT_;
    size_t arrays_ = 0;
};

// Big charts read once are kept in this folder (their notes, compact), and
// the next load reads them back instead of the JSON: a fraction of a second
// for any size. Empty (the default): no cache. The folder is created when
// needed and kept under 4 GB, the oldest files going first.
void setChartCacheDir(const std::string& dir);
std::string chartCacheDir();

// Loads any chart file: small ones are parsed whole, big ones are streamed.
bool loadChartFile(const std::string& path, const std::string& difficulty, Chart& out, std::string& err,
                   std::string_view vsliceMetadata = {}, size_t streamThreshold = 0, bool extraKeys = true);

// Quick look at a chart's song name, opponent and stage without parsing it
// (reads only the start and end of the file).
struct ChartPeek {
    std::string song, player2, stage;
    std::string difficulty;   // PulseForge charts name theirs
    bool isChart = false;
    bool pulseForge = false;  // "pulseforge-chart" or "pulseforge-editor-chart"
};
ChartPeek peekChart(const std::string& path);

uint64_t fileSize(const std::string& path);
bool fileExistsAt(const std::string& path);

}  // namespace gh
