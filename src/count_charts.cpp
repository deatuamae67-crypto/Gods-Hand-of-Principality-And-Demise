// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "count_charts.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "miniz.h"

namespace gh {

namespace {

// A file read in order through a buffer, with its size known.
class InFile {
public:
    explicit InFile(const std::string& path) : f_(std::fopen(path.c_str(), "rb")) {
        if (!f_) return;
        std::fseek(f_, 0, SEEK_END);
        const long long n = tell64(f_);
        size_ = n > 0 ? uint64_t(n) : 0;
        std::fseek(f_, 0, SEEK_SET);
    }
    ~InFile() {
        if (f_) std::fclose(f_);
    }
    InFile(const InFile&) = delete;
    InFile& operator=(const InFile&) = delete;
    bool ok() const { return f_ != nullptr; }
    uint64_t size() const { return size_; }
    bool read(void* dst, size_t n) { return n == 0 || std::fread(dst, 1, n, f_) == n; }
    bool seek(uint64_t off) { return seek64(f_, (long long)off) == 0; }

private:
    static long long tell64(FILE* f) {
#if defined(_WIN32)
        return _ftelli64(f);
#else
        return (long long)ftello(f);
#endif
    }
    static int seek64(FILE* f, long long off) {
#if defined(_WIN32)
        return _fseeki64(f, off, SEEK_SET);
#else
        return fseeko(f, off_t(off), SEEK_SET);
#endif
    }
    FILE* f_ = nullptr;
    uint64_t size_ = 0;
};

uint32_t le32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t le64(const unsigned char* p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }

// ---------------------------------------------------------------- iutk ----
//
// FNC1, the iutk engine's chart (its ChartBinary.hx reads it, convert-chart.c
// writes it):
//   "FNC1", version (u8)
//   v2: u32 notes; i32 time[notes] (ms); u8 lane[notes]            one note each
//   v3: u32 cells, u64 total; i32 time[cells]; u8 lane[cells]; u64 count[cells]
//   v4: varints - cells, groups (distinct times), total, first time
//       (time + 2^31), the byte lengths of streams D, M and C - then their
//       CRC-32s (u32 each), then the streams. A group is one time: D holds
//       the gap to the previous one minus 1, M which of the 8 lanes have
//       notes (a bit each), C each of those lanes' count minus 1. Every
//       stream is a run of items: a varint header n, odd = one value
//       repeated (n >> 1) + 2 times, even = (n >> 1) + 1 values; M's values
//       are bytes, D's and C's varints.
// Lanes 0-3 are the opponent's, 4-7 the player's (left, down, up, right).
constexpr int kIutkLanes = 8;

void addCell(Chart& out, int32_t timeMs, int lane, const Count& count) {
    Note n;
    n.time = double(timeMs);
    n.lane = uint16_t(lane & 3);
    n.side = lane >= 4 ? 1 : 0;
    out.setCount(n, count);
    out.notes.push_back(n);
}

bool readFnc1V2(InFile& f, Chart& out, std::string& err) {
    unsigned char h[4];
    if (!f.read(h, 4)) { err = "iutk chart: cut short"; return false; }
    const uint32_t notes = le32(h);
    if (notes > 0x7FFFFFFFu || 9 + uint64_t(notes) * 5 > f.size()) { err = "iutk chart: cut short (notes)"; return false; }
    std::vector<unsigned char> times(size_t(notes) * 4), lanes(notes);
    if (!f.read(times.data(), times.size()) || !f.read(lanes.data(), lanes.size())) { err = "iutk chart: cut short"; return false; }
    out.notes.reserve(notes);
    for (uint32_t i = 0; i < notes; ++i) {
        if (lanes[i] >= kIutkLanes) { err = "iutk chart: lane " + std::to_string(lanes[i]) + " out of range"; return false; }
        addCell(out, int32_t(le32(&times[size_t(i) * 4])), lanes[i], Count(1));
    }
    out.rawNoteCount = Count(notes);
    return true;
}

bool readFnc1V3(InFile& f, Chart& out, std::string& err) {
    unsigned char h[12];
    if (!f.read(h, 12)) { err = "iutk chart: cut short"; return false; }
    const uint32_t cells = le32(h);
    const uint64_t declared = le64(h + 4);
    if (cells > 0x7FFFFFFFu || 17 + uint64_t(cells) * 13 > f.size()) { err = "iutk chart: cut short (cells)"; return false; }
    std::vector<unsigned char> times(size_t(cells) * 4), lanes(cells), counts(size_t(cells) * 8);
    if (!f.read(times.data(), times.size()) || !f.read(lanes.data(), lanes.size()) || !f.read(counts.data(), counts.size())) {
        err = "iutk chart: cut short";
        return false;
    }
    out.notes.reserve(cells);
    uint64_t sum = 0;
    for (uint32_t i = 0; i < cells; ++i) {
        if (lanes[i] >= kIutkLanes) { err = "iutk chart: lane " + std::to_string(lanes[i]) + " out of range"; return false; }
        const uint64_t c = le64(&counts[size_t(i) * 8]);
        if (sum + c < sum) { err = "iutk chart: its notes add up past 64 bits"; return false; }
        sum += c;
        if (c) addCell(out, int32_t(le32(&times[size_t(i) * 4])), lanes[i], Count(c));
    }
    if (sum != declared) {
        err = "iutk chart: it says " + std::to_string(declared) + " notes but its cells hold " + std::to_string(sum);
        return false;
    }
    out.rawNoteCount = Count(sum);
    return true;
}

// One of version 4's streams: its bytes (checked against their CRC-32 at the
// end), read as run-length items.
struct V4Stream {
    const unsigned char* p = nullptr;
    const unsigned char* end = nullptr;
    const unsigned char* begin = nullptr;
    bool rawBytes = false;  // M: values are single bytes
    uint64_t remaining = 0, rep = 0;
    bool repeating = false;
    bool bad = false;

    uint64_t varint() {
        uint64_t v = 0;
        for (int s = 0;; s += 7) {
            if (p >= end || s > 63) { bad = true; return 0; }
            const unsigned char b = *p++;
            if (s == 63 && b > 1) { bad = true; return 0; }
            v |= uint64_t(b & 0x7F) << s;
            if (!(b & 0x80)) return v;
        }
    }
    uint64_t value() {
        if (!rawBytes) return varint();
        if (p >= end) { bad = true; return 0; }
        return *p++;
    }
    uint64_t item() {
        if (remaining == 0) {
            const uint64_t h = varint();
            if (bad || h > 0x7FFFFFFFu) { bad = true; return 0; }
            if (h & 1) {
                remaining = (h >> 1) + 2;
                repeating = true;
                rep = value();
            } else {
                remaining = (h >> 1) + 1;
                repeating = false;
            }
        }
        --remaining;
        return repeating ? rep : value();
    }
    bool finished(uint32_t crc) const {
        return !bad && p == end && remaining == 0 && uint32_t(mz_crc32(MZ_CRC32_INIT, begin, size_t(end - begin))) == crc;
    }
};

bool readFnc1V4(InFile& f, Chart& out, std::string& err) {
    // The header's varints: read from the start of the file's rest.
    const uint64_t rest = f.size() - 5;
    std::vector<unsigned char> data(size_t(std::min<uint64_t>(rest, 128)));
    if (!f.read(data.data(), data.size())) { err = "iutk chart: cut short"; return false; }
    V4Stream h;
    h.p = h.begin = data.data();
    h.end = data.data() + data.size();
    const uint64_t cells = h.varint(), groups = h.varint(), declared = h.varint(), first = h.varint();
    const uint64_t lenD = h.varint(), lenM = h.varint(), lenC = h.varint();
    if (h.bad || h.end - h.p < 12) { err = "iutk chart: bad header"; return false; }
    const uint32_t crcD = le32(h.p), crcM = le32(h.p + 4), crcC = le32(h.p + 8);
    const uint64_t bodyStart = 5 + uint64_t(h.p - data.data()) + 12;
    if (cells > 0x7FFFFFFFu || groups > 0x7FFFFFFFu || first > 0xFFFFFFFFu || lenD > 0x7FFFFFFFu || lenM > 0x7FFFFFFFu ||
        lenC > 0x7FFFFFFFu) {
        err = "iutk chart: header out of range";
        return false;
    }
    if (bodyStart + lenD + lenM + lenC != f.size()) { err = "iutk chart: its streams and its size do not match"; return false; }
    if (groups > cells || (groups == 0) != (cells == 0) || cells > groups * kIutkLanes) {
        err = "iutk chart: cell and group counts do not agree";
        return false;
    }
    std::vector<unsigned char> body(size_t(lenD + lenM + lenC));
    if (!f.seek(bodyStart) || !f.read(body.data(), body.size())) { err = "iutk chart: cut short"; return false; }
    V4Stream d, m, c;
    d.p = d.begin = body.data();
    d.end = m.p = m.begin = d.p + lenD;
    m.end = c.p = c.begin = m.p + lenM;
    c.end = c.p + lenC;
    m.rawBytes = true;
    out.notes.reserve(size_t(cells));
    uint64_t time = first, sum = 0, made = 0;
    for (uint64_t g = 0; g < groups; ++g) {
        if (g > 0) {
            const uint64_t gap = d.item();
            if (d.bad || gap > 0xFFFFFFFFu || (time += gap + 1) > 0xFFFFFFFFu) { err = "iutk chart: bad time"; return false; }
        }
        const uint64_t mask = m.item();
        if (m.bad || mask < 1 || mask > 255) { err = "iutk chart: bad lane mask"; return false; }
        const int32_t pos = int32_t(int64_t(time) - 2147483648LL);
        for (int lane = 0; lane < kIutkLanes; ++lane) {
            if (!((mask >> lane) & 1)) continue;
            if (made >= cells) { err = "iutk chart: more cells than it says"; return false; }
            const uint64_t k = c.item();
            if (c.bad || k == UINT64_MAX || sum + (k + 1) < sum) { err = "iutk chart: its notes add up past 64 bits"; return false; }
            sum += k + 1;
            addCell(out, pos, lane, Count(k + 1));
            ++made;
        }
    }
    if (made != cells) { err = "iutk chart: fewer cells than it says"; return false; }
    if (sum != declared) {
        err = "iutk chart: it says " + std::to_string(declared) + " notes but its cells hold " + std::to_string(sum);
        return false;
    }
    if (!d.finished(crcD) || !m.finished(crcM) || !c.finished(crcC)) { err = "iutk chart: damaged (checksums)"; return false; }
    out.rawNoteCount = Count(sum);
    return true;
}

bool loadFnc1(const std::string& path, Chart& out, std::string& err) {
    InFile f(path);
    unsigned char head[5];
    if (!f.ok() || !f.read(head, 5)) { err = "cannot read " + path; return false; }
    out = Chart();
    out.keys = 4;
    out.title = "";
    bool ok = false;
    switch (head[4]) {
        case 2: ok = readFnc1V2(f, out, err); break;
        case 3: ok = readFnc1V3(f, out, err); break;
        case 4: ok = readFnc1V4(f, out, err); break;
        default: err = "iutk chart version " + std::to_string(head[4]) + " is not known"; return false;
    }
    if (!ok) return false;
    out.finalize(120);  // the iutk engine plays every chart at 120 BPM
    return true;
}

}  // namespace

CountChartKind countChartKind(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return CountChartKind::None;
    unsigned char m[5] = {};
    const size_t n = std::fread(m, 1, 5, f);
    std::fclose(f);
    if (n == 5 && std::memcmp(m, "FNC1", 4) == 0 && m[4] >= 2 && m[4] <= 4) return CountChartKind::Fnc1;
    return CountChartKind::None;
}

bool loadCountChart(const std::string& path, Chart& out, std::string& err) {
    switch (countChartKind(path)) {
        case CountChartKind::Fnc1: return loadFnc1(path, out, err);
        default: err = path + " is not a chart this game counts"; return false;
    }
}

bool writeFnc1(const std::string& path, const Chart& chart, std::string& err) {
    // Cells by time, then lane (as the converter sorts them).
    struct Cell {
        int32_t time;
        uint8_t lane;
        uint64_t count;
    };
    std::vector<Cell> cells;
    uint64_t total = 0;
    for (const Note& n : chart.notes) {
        if (n.kind != kNormal) continue;
        const Count c = chart.countOf(n);
        if (!c.fits64() || total + c.w[0] < total) { err = "an iutk chart holds up to 2^64 - 1 notes"; return false; }
        if (n.time < -2147483648.0 || n.time > 2147483647.0) { err = "an iutk chart holds times of 32 bits"; return false; }
        total += c.w[0];
        cells.push_back({int32_t(n.time), uint8_t((n.lane & 3) + (n.side ? 4 : 0)), c.w[0]});
    }
    std::stable_sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return a.time != b.time ? a.time < b.time : a.lane < b.lane; });
    // The same time and lane twice: one cell.
    std::vector<Cell> merged;
    for (const Cell& c : cells) {
        if (!merged.empty() && merged.back().time == c.time && merged.back().lane == c.lane) merged.back().count += c.count;
        else merged.push_back(c);
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { err = "cannot write " + path; return false; }
    std::string out = "FNC1";
    out += char(3);
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out += char(v >> (8 * i)); };
    put32(uint32_t(merged.size()));
    put32(uint32_t(total));
    put32(uint32_t(total >> 32));
    for (const Cell& c : merged) put32(uint32_t(c.time));
    for (const Cell& c : merged) out += char(c.lane);
    for (const Cell& c : merged) {
        put32(uint32_t(c.count));
        put32(uint32_t(c.count >> 32));
    }
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    if (std::fclose(f) != 0 || !ok) { err = "cannot write " + path; return false; }
    return true;
}

}  // namespace gh
