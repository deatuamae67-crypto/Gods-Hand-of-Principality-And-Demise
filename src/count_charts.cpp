// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "count_charts.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

#include "chart_binary.hpp"
#include "chart_stream.hpp"
#include "common.hpp"
#include "json.hpp"
#include "keys.hpp"
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

// ------------------------------------------------------------- shared ----

// A cursor over bytes in memory (the sections and chunks, once read).
struct Cur {
    const unsigned char* p = nullptr;
    const unsigned char* end = nullptr;
    bool ok = true;
    uint8_t u8() {
        if (p >= end) { ok = false; return 0; }
        return *p++;
    }
    uint32_t u32() {
        if (end - p < 4) { ok = false; p = end; return 0; }
        const uint32_t v = le32(p);
        p += 4;
        return v;
    }
    uint64_t u64() {
        const uint64_t lo = u32();
        return lo | uint64_t(u32()) << 32;
    }
    uint64_t var() {
        uint64_t v = 0;
        for (int s = 0;; s += 7) {
            if (p >= end || s > 63) { ok = false; return 0; }
            const unsigned char b = *p++;
            v |= uint64_t(b & 0x7F) << s;
            if (!(b & 0x80)) return v;
        }
    }
    int64_t zig() {
        const uint64_t v = var();
        return int64_t(v >> 1) ^ -int64_t(v & 1);
    }
};

void putVar(std::string& o, uint64_t v) {
    while (v >= 0x80) {
        o += char(v | 0x80);
        v >>= 7;
    }
    o += char(v);
}
void putZig(std::string& o, int64_t v) { putVar(o, (uint64_t(v) << 1) ^ uint64_t(v >> 63)); }
void put32(std::string& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o += char(v >> (8 * i));
}
void put64(std::string& o, uint64_t v) {
    put32(o, uint32_t(v));
    put32(o, uint32_t(v >> 32));
}
uint32_t crc32Of(const void* p, size_t n) { return uint32_t(mz_crc32(MZ_CRC32_INIT, static_cast<const unsigned char*>(p), n)); }

bool inflateTo(const unsigned char* src, size_t n, size_t rawSize, std::vector<unsigned char>& out) {
    out.resize(rawSize);
    mz_ulong m = mz_ulong(rawSize);
    return mz_uncompress(out.data(), &m, src, mz_ulong(n)) == MZ_OK && size_t(m) == rawSize;
}
std::string deflateOf(const std::string& raw, int level) {
    mz_ulong n = mz_compressBound(mz_ulong(raw.size()));
    std::string out(n, '\0');
    if (mz_compress2(reinterpret_cast<unsigned char*>(out.data()), &n, reinterpret_cast<const unsigned char*>(raw.data()),
                     mz_ulong(raw.size()), level) != MZ_OK)
        return std::string();
    out.resize(n);
    return out;
}

// Notes of the same lane within a millisecond become one note of all of them
// (Chart::finalize), so a chart expanding to more than this is folded as it
// goes: memory stays with the chart's cells, never with its note count.
constexpr size_t kFoldEvery = size_t(8) << 20;
void foldIfLarge(Chart& c, size_t& foldedAt) {
    if (c.notes.size() < kFoldEvery || c.notes.size() < 2 * foldedAt) return;
    const bool hadFocus = !c.focus.empty();  // the camera follows the whole chart, worked out at the end
    c.finalize(c.tempo.empty() ? 120 : c.tempo.front().bpm);
    if (!hadFocus) c.focus.clear();
    foldedAt = c.notes.size();
}

void metaChart(const json::Document& d, const json::Value& r, Chart& out) {
    out.title = std::string(d.strOr(d.get(r, "title"), ""));
    out.difficulty = std::string(d.strOr(d.get(r, "difficulty"), ""));
    out.opponent = std::string(d.strOr(d.get(r, "opponent"), ""));
    out.player = std::string(d.strOr(d.get(r, "player"), ""));
    out.gf = std::string(d.strOr(d.get(r, "gf"), ""));
    out.stage = std::string(d.strOr(d.get(r, "stage"), ""));
    out.keys = std::clamp(int(json::Document::num(d.get(r, "keys"), 4)), 1, kMaxKeys);
    out.speed = json::Document::num(d.get(r, "speed"), 2.0);
    out.noteTypes.clear();
    if (const json::Value* nt = d.get(r, "noteTypes"))
        for (size_t i = 0; i < d.size(*nt); ++i) out.noteTypes.push_back(std::string(d.str(d.at(*nt, i))));
    if (out.noteTypes.empty()) out.noteTypes.push_back("");
    if (const json::Value* tp = d.get(r, "tempo"))
        for (size_t i = 0; i < d.size(*tp); ++i) {
            const json::Value& t = d.at(*tp, i);
            out.tempo.push_back({json::Document::num(d.get(t, "timeMs"), 0), json::Document::num(d.get(t, "bpm"), 120),
                                 json::Document::num(d.get(t, "beat"), 0)});
        }
    if (const json::Value* fo = d.get(r, "focus"))
        for (size_t i = 0; i < d.size(*fo); ++i) {
            const json::Value& t = d.at(*fo, i);
            out.focus.push_back({json::Document::num(d.get(t, "timeMs"), 0), uint8_t(json::Document::num(d.get(t, "side"), 0))});
        }
    if (const json::Value* se = d.get(r, "sections"))
        for (size_t i = 0; i < d.size(*se); ++i) {
            const json::Value& s = d.at(*se, i);
            out.sections.push_back({json::Document::num(d.get(s, "timeMs"), 0), json::Document::boolean(d.get(s, "mustHit"), true),
                                    json::Document::boolean(d.get(s, "gf"), false), json::Document::boolean(d.get(s, "alt"), false),
                                    json::Document::num(d.get(s, "beats"), 4), json::Document::num(d.get(s, "bpm"), 0)});
        }
}

// --------------------------------------------------------------- RAID ----
//
// R-Slice's .raid, read from what its files hold (raid.hpp tells the
// layout; the meaning of each op is worked out here):
//   header (128 bytes): "RAID", u32 version, u32 flags, u32 x2 reserved,
//     u64 events, u64 chunks, u64 patterns, u64 index offset, u64 index
//     size, u64 meta offset, u64 meta packed (0: as it is), u64 meta size,
//     u64 patterns offset/packed/size, u32 CRC-32 (of the header with these
//     four bytes 0) at 108.
//   index, 56 bytes a chunk: u64 first event, i64 first and last time (us),
//     u64 offset, u32 packed, u32 size, u32 events, u32 ops, u32 CRC-32 of
//     the raw payload, u32 flags (1: deflated).
//   meta: JSON + u32 CRC-32.  patterns: u32 count, per pattern u64 span
//     (us), u8 has-pitch, u32 length + that many bytes of ops; u32 CRC-32.
//   a chunk: u32 op count, the ops, u64 events.
//   ops: 0 notes at their own times (u32 n; zigzag first time, varint
//     steps; varint lengths; zigzag pitch + 1; varint lanes; varint side +
//     kind * 2 + flags * 4; varint types; varint stacks), 1 the same inside
//     a pattern, its times, pitches and lanes from where the pattern is put,
//     2 a pattern repeated (u32 id, u32 times, zigzag time, pitch and lane
//     moves) a span apart, 3 several patterns at once, each clipped to a
//     time range, 4 a pattern repeated a given stride apart.
namespace raidfmt {
constexpr size_t kHeader = 128, kIndexEntry = 56;
constexpr int kMaxDepth = 8;
constexpr uint64_t kMaxChunkEvents = uint64_t(1) << 22;
constexpr uint64_t kMaxEvents = uint64_t(400) << 20;  // what fits in memory as notes
constexpr int64_t kBound = int64_t(1) << 40;

struct Pattern {
    uint64_t spanUs = 0;
    bool hasPitch = false;
    size_t off = 0, len = 0;
};
struct Ctx {
    int64_t time = 0, pitch = 0, lane = 0;
    int64_t lo = INT64_MIN, hi = INT64_MAX;  // events outside are dropped (op 3)
};
struct Run {
    const std::vector<unsigned char>* blob = nullptr;
    const std::vector<Pattern>* pats = nullptr;
    Chart* out = nullptr;
    uint64_t produced = 0, limit = 0;
    int keys = 4;
};

bool ops(Cur& c, Run& run, const Pattern* pat, const Ctx& ctx, int depth, std::string& err);

bool literal(Cur& c, Run& run, bool relative, const Pattern* pat, const Ctx& ctx, std::string& err) {
    const uint32_t n = c.u32();
    if (!c.ok || n == 0 || n > (1u << 24)) { err = "bad literal"; return false; }
    std::vector<int64_t> t(n);
    int64_t at = relative ? ctx.time + int64_t(c.var()) : c.zig();
    t[0] = at;
    for (uint32_t k = 1; k < n; ++k) {
        const uint64_t d = c.var();
        if (d > uint64_t(kBound) * 1024) { err = "time out of range"; return false; }
        t[k] = (at += int64_t(d));
    }
    if (!c.ok || t[0] < 0) { err = "bad event time"; return false; }
    std::vector<uint32_t> len(n);
    for (auto& v : len) {
        const uint64_t x = c.var();
        if (x > 0xFFFFFFFFu) { err = "bad length"; return false; }
        v = uint32_t(x);
    }
    for (uint32_t k = 0; k < n; ++k) c.zig();  // pitch: notes have none in this game
    std::vector<int64_t> lane(n);
    for (auto& v : lane) {
        v = relative ? ctx.lane + c.zig() : int64_t(c.var());
        if (v < 0 || v > 255) { err = "lane out of range"; return false; }
    }
    std::vector<uint32_t> attr(n), type(n), stack(n);
    for (auto& v : attr) {
        const uint64_t x = c.var();
        if (x > 0x3FF) { err = "bad note attributes"; return false; }
        v = uint32_t(x);
    }
    for (auto& v : type) {
        const uint64_t x = c.var();
        if (x > 0xFFFF) { err = "bad note type"; return false; }
        v = uint32_t(x);
    }
    for (auto& v : stack) {
        const uint64_t x = c.var();
        if (x > 0xFFFF) { err = "bad stack"; return false; }
        v = uint32_t(x ? x : 1);
    }
    if (!c.ok) { err = "cut short"; return false; }
    (void)pat;
    for (uint32_t k = 0; k < n; ++k) {
        if (t[k] < ctx.lo || t[k] >= ctx.hi) continue;
        if (++run.produced > run.limit) { err = "a chunk holds more events than it says"; return false; }
        Note note;
        note.time = double(t[k]) / 1000.0;
        note.length = float(double(len[k]) / 1000.0);
        note.lane = uint16_t(lane[k]);
        note.side = uint8_t(attr[k] & 1);
        note.kind = (attr[k] >> 1 & 1) ? kMine : kNormal;
        note.flags = uint8_t((attr[k] >> 2) & 0xFF & ~kFlagBigCount);
        note.type = uint16_t(type[k]);
        note.setCount32(stack[k]);
        run.keys = std::max(run.keys, int(lane[k]) + 1);
        run.out->notes.push_back(note);
    }
    return true;
}

bool instances(Cur& c, Run& run, const Ctx& ctx, int depth, bool burst, std::string& err) {
    const uint32_t id = c.u32(), reps = c.u32();
    const int64_t anchor = c.zig();
    c.zig();  // pitch move
    const int64_t laneMove = c.zig();
    const int64_t stride = burst ? c.zig() : 0;
    if (!c.ok || id >= run.pats->size()) { err = "bad pattern"; return false; }
    const Pattern& p = (*run.pats)[id];
    const int64_t unit = burst ? stride : int64_t(p.spanUs);
    if (unit < 0 || unit > kBound || anchor < -(int64_t(1) << 50) || anchor > (int64_t(1) << 50) || laneMove < -kBound ||
        laneMove > kBound || (unit && uint64_t(reps) > (uint64_t(1) << 50) / uint64_t(unit)) || reps > run.limit) {
        err = "pattern moved out of range";
        return false;
    }
    for (uint32_t r = 0; r < reps; ++r) {
        Ctx sub{ctx.time + anchor + int64_t(r) * unit, 0, ctx.lane + laneMove, ctx.lo, ctx.hi};
        Cur pc{run.blob->data() + p.off, run.blob->data() + p.off + p.len};
        if (!ops(pc, run, &p, sub, depth + 1, err)) return false;
    }
    return true;
}

bool ops(Cur& c, Run& run, const Pattern* pat, const Ctx& ctx, int depth, std::string& err) {
    if (depth > kMaxDepth) { err = "patterns nested too deep"; return false; }
    const uint32_t count = c.u32();
    if (!c.ok || count > (1u << 22)) { err = "bad op count"; return false; }
    for (uint32_t o = 0; o < count; ++o) {
        const uint8_t kind = c.u8();
        if (!c.ok) { err = "cut short"; return false; }
        bool ok = true;
        if (kind == 0 || kind == 1) {
            if (kind == 1 && !pat) { err = "a pattern's notes outside a pattern"; return false; }
            ok = literal(c, run, kind == 1, pat, ctx, err);
        } else if (kind == 2 || kind == 4) {
            ok = instances(c, run, ctx, depth, kind == 4, err);
        } else if (kind == 3) {
            const uint32_t n = c.u32();
            if (!c.ok || n == 0 || n > 4096) { err = "bad pattern group"; return false; }
            for (uint32_t i = 0; i < n && ok; ++i) {
                const uint32_t id = c.u32(), reps = c.u32();
                const int64_t anchor = c.zig();
                c.zig();
                const int64_t laneMove = c.zig(), lo = c.zig(), hi = c.zig();
                if (!c.ok || id >= run.pats->size() || lo > hi || laneMove < -kBound || laneMove > kBound) {
                    err = "bad pattern group";
                    return false;
                }
                const Pattern& p = (*run.pats)[id];
                if (p.spanUs > (uint64_t(1) << 50) || (p.spanUs && uint64_t(reps) > (uint64_t(1) << 50) / p.spanUs) ||
                    reps > run.limit) {
                    err = "pattern moved out of range";
                    return false;
                }
                for (uint32_t r = 0; r < reps && ok; ++r) {
                    Ctx sub{ctx.time + anchor + int64_t(r) * int64_t(p.spanUs), 0, ctx.lane + laneMove, lo, hi};
                    Cur pc{run.blob->data() + p.off, run.blob->data() + p.off + p.len};
                    ok = ops(pc, run, &p, sub, depth + 1, err);
                }
            }
        } else {
            err = "unknown op " + std::to_string(kind);
            return false;
        }
        if (!ok) return false;
    }
    return c.ok;
}

bool readSection(InFile& f, uint64_t off, uint64_t packed, uint64_t raw, std::vector<unsigned char>& out, std::string& err) {
    if (raw > (uint64_t(512) << 20) || packed > (uint64_t(512) << 20) || off > f.size() || (packed ? packed : raw) > f.size() - off) {
        err = "a section is out of the file";
        return false;
    }
    std::vector<unsigned char> stored(size_t(packed ? packed : raw));
    if (!f.seek(off) || !f.read(stored.data(), stored.size())) { err = "cut short"; return false; }
    if (!packed) {
        out.swap(stored);
        return true;
    }
    if (!inflateTo(stored.data(), stored.size(), size_t(raw), out)) { err = "a section does not unpack"; return false; }
    return true;
}
}  // namespace raidfmt

bool loadRaid(const std::string& path, Chart& out, std::string& err) {
    using namespace raidfmt;
    InFile f(path);
    unsigned char h[kHeader];
    if (!f.ok() || f.size() < kHeader || !f.read(h, kHeader)) { err = "cannot read " + path; return false; }
    Cur c{h + 4, h + kHeader};
    const uint32_t version = c.u32();
    c.u32();
    c.u32();
    c.u32();
    const uint64_t events = c.u64(), chunks = c.u64();
    c.u64();  // patterns: the section says
    const uint64_t indexOff = c.u64(), indexSize = c.u64();
    const uint64_t metaOff = c.u64(), metaPacked = c.u64(), metaRaw = c.u64();
    const uint64_t patOff = c.u64(), patPacked = c.u64(), patRaw = c.u64();
    const uint32_t crc = c.u32();
    unsigned char hz[kHeader];
    std::memcpy(hz, h, kHeader);
    std::memset(hz + 108, 0, 4);
    if (version < 1 || version > 2) { err = ".raid version " + std::to_string(version) + " is not known"; return false; }
    if (crc32Of(hz, kHeader) != crc) { err = ".raid: damaged header"; return false; }
    if (events > kMaxEvents) {
        err = ".raid: " + Count(events).grouped() + " events, more than this game holds as notes (" + Count(kMaxEvents).grouped() + ")";
        return false;
    }
    if (chunks > (uint64_t(1) << 32) || indexSize != chunks * kIndexEntry || indexOff > f.size() || indexSize > f.size() - indexOff) {
        err = ".raid: bad chunk index";
        return false;
    }
    out = Chart();
    // Meta: the chart's fields.
    std::vector<unsigned char> meta;
    if (metaRaw) {
        if (!readSection(f, metaOff, metaPacked, metaRaw, meta, err)) return false;
        if (meta.size() < 4 || crc32Of(meta.data(), meta.size() - 4) != le32(&meta[meta.size() - 4])) {
            err = ".raid: damaged meta";
            return false;
        }
        json::Document d;
        if (d.parse(std::string_view(reinterpret_cast<const char*>(meta.data()), meta.size() - 4))) metaChart(d, d.root(), out);
    }
    // Patterns.
    std::vector<unsigned char> blob;
    std::vector<Pattern> pats;
    if (patRaw) {
        if (!readSection(f, patOff, patPacked, patRaw, blob, err)) return false;
        if (blob.size() < 8 || crc32Of(blob.data(), blob.size() - 4) != le32(&blob[blob.size() - 4])) {
            err = ".raid: damaged patterns";
            return false;
        }
        Cur pc{blob.data(), blob.data() + blob.size() - 4};
        const uint32_t n = pc.u32();
        for (uint32_t i = 0; i < n && pc.ok; ++i) {
            Pattern p;
            p.spanUs = pc.u64();
            p.hasPitch = pc.u8() & 1;
            const uint32_t len = pc.u32();
            if (!pc.ok || len < 4 || len > size_t(pc.end - pc.p)) { err = ".raid: bad pattern"; return false; }
            p.off = size_t(pc.p - blob.data());
            p.len = len;
            pc.p += len;
            pats.push_back(p);
        }
        if (!pc.ok || pc.p != pc.end) { err = ".raid: bad patterns"; return false; }
    }
    // Chunks.
    std::vector<unsigned char> index;
    index.resize(size_t(indexSize));
    if (!f.seek(indexOff) || !f.read(index.data(), index.size())) { err = ".raid: cut short"; return false; }
    Run run;
    run.blob = &blob;
    run.pats = &pats;
    run.out = &out;
    run.keys = out.keys;
    uint64_t produced = 0;
    size_t foldedAt = 0;
    for (uint64_t i = 0; i < chunks; ++i) {
        Cur ic{&index[size_t(i * kIndexEntry)], &index[size_t(i * kIndexEntry)] + kIndexEntry};
        ic.u64();
        ic.u64();
        ic.u64();
        const uint64_t off = ic.u64();
        const uint32_t packed = ic.u32(), raw = ic.u32(), expanded = ic.u32();
        ic.u32();
        const uint32_t ccrc = ic.u32(), flags = ic.u32();
        if (expanded == 0 || expanded > kMaxChunkEvents) { err = ".raid: bad chunk"; return false; }
        std::vector<unsigned char> payload;
        if (!readSection(f, off, (flags & 1) ? packed : 0, raw, payload, err)) return false;
        if (payload.size() < 12 || crc32Of(payload.data(), payload.size()) != ccrc) { err = ".raid: damaged chunk"; return false; }
        Cur cc{payload.data(), payload.data() + payload.size()};
        run.produced = 0;
        run.limit = expanded;
        if (!ops(cc, run, nullptr, Ctx{}, 0, err)) { err = ".raid: " + err; return false; }
        if (cc.u64() != run.produced || !cc.ok || cc.p != cc.end || run.produced != expanded) {
            err = ".raid: a chunk's events do not add up";
            return false;
        }
        produced += run.produced;
        if (out.keys < run.keys) out.keys = std::min(run.keys, kMaxKeys);
        foldIfLarge(out, foldedAt);
    }
    if (produced != events) { err = ".raid: its events do not add up"; return false; }
    out.finalize(out.tempo.empty() ? 120 : out.tempo.front().bpm);
    out.rawNoteCount = out.playerNotes + out.opponentNotes;
    return true;
}

// ---------------------------------------------------------------- GHN ----
namespace ghnfmt {
constexpr size_t kHeader = 64;
constexpr size_t kBlockNotes = 65536;
constexpr uint64_t kMaxCells = uint64_t(400) << 20;  // notes held at once
constexpr int64_t kNsPerMs = 1000000;

int64_t toNs(double ms) { return int64_t(std::llround(ms * 1e6)); }

std::string esc(const std::string& s) {
    std::string o = "\"";
    for (char ch : s) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') o += '\\', o += ch;
        else if (ch == '\n') o += "\\n";
        else if (u < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", u);
            o += b;
        } else o += ch;
    }
    return o + "\"";
}
std::string num(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.17g", v);
    return b;
}

std::string metaOf(const Chart& c) {
    std::string j = "{\"ghn\":1,\"title\":" + esc(c.title) + ",\"difficulty\":" + esc(c.difficulty) + ",\"opponent\":" + esc(c.opponent) +
                    ",\"player\":" + esc(c.player) + ",\"gf\":" + esc(c.gf) + ",\"stage\":" + esc(c.stage) +
                    ",\"keys\":" + std::to_string(c.keys) + ",\"sideKeys\":[" + std::to_string(c.sideKeys[0]) + "," +
                    std::to_string(c.sideKeys[1]) + "],\"speed\":" + num(c.speed) + ",\"noteTypes\":[";
    for (size_t i = 0; i < c.noteTypes.size(); ++i) j += (i ? "," : "") + esc(c.noteTypes[i]);
    j += "],\"tempo\":[";
    for (size_t i = 0; i < c.tempo.size(); ++i)
        j += std::string(i ? "," : "") + "{\"timeMs\":" + num(c.tempo[i].timeMs) + ",\"bpm\":" + num(c.tempo[i].bpm) + "}";
    j += "],\"focus\":[";
    for (size_t i = 0; i < c.focus.size(); ++i)
        j += std::string(i ? "," : "") + "{\"timeMs\":" + num(c.focus[i].timeMs) + ",\"side\":" + std::to_string(c.focus[i].side) + "}";
    j += "],\"sections\":[";
    for (size_t i = 0; i < c.sections.size(); ++i) {
        const SectionInfo& s = c.sections[i];
        j += std::string(i ? "," : "") + "{\"timeMs\":" + num(s.timeMs) + ",\"mustHit\":" + (s.mustHit ? "true" : "false") +
             ",\"gf\":" + (s.gf ? "true" : "false") + ",\"alt\":" + (s.alt ? "true" : "false") + ",\"beats\":" + num(s.beats) +
             ",\"bpm\":" + num(s.bpm) + "}";
    }
    j += "],\"events\":[";
    for (size_t i = 0; i < c.events.size(); ++i) {
        const psych::ChartEvent& e = c.events[i];
        j += std::string(i ? "," : "") + "[" + num(e.timeMs) + "," + esc(e.name) + "," + esc(e.v1) + "," + esc(e.v2) + "]";
    }
    j += "],\"playerNotes\":\"" + c.playerNotes.str() + "\",\"opponentNotes\":\"" + c.opponentNotes.str() + "\",\"rawNotes\":\"" +
         (c.rawNoteCount.isZero() ? (c.playerNotes + c.opponentNotes).str() : c.rawNoteCount.str()) + "\"}";
    return j;
}

// A note's columns, all but its time: two notes alike in these are the same
// note moved (what a repeat is made of).
struct Shape {
    uint64_t laneSide = 0, kindFlags = 0, type = 0, length = 0;
    Count count;
    bool operator==(const Shape& o) const {
        return laneSide == o.laneSide && kindFlags == o.kindFlags && type == o.type && length == o.length && count == o.count;
    }
};
Shape shapeOf(const Chart& c, const Note& n) {
    Shape s;
    s.laneSide = uint64_t(n.lane) << 1 | n.side;
    s.kindFlags = uint64_t(n.kind) | uint64_t(n.flags & ~kFlagBigCount) << 7;
    s.type = n.type;
    s.length = uint64_t(std::max<int64_t>(0, toNs(double(n.length))));
    s.count = c.countOf(n);
    return s;
}

std::string encodeBlock(const Chart& c, const Note* n, size_t cnt, int64_t first) {
    std::string t, ls, kf, ty, ln, ct;
    int64_t prev = first;
    for (size_t i = 0; i < cnt; ++i) {
        const int64_t at = toNs(n[i].time);
        putZig(t, at - prev);
        prev = at;
        const Shape s = shapeOf(c, n[i]);
        putVar(ls, s.laneSide);
        putVar(kf, s.kindFlags);
        putVar(ty, s.type);
        putVar(ln, s.length);
        (s.count - Count(1)).appendVarint(ct);
    }
    return t + ls + kf + ty + ln + ct;
}

struct Decoded {
    std::vector<Note> notes;
    std::vector<Count> big;  // counts too large for a note, by the notes' indexes
};

bool decodeBlock(const unsigned char* p, const unsigned char* end, size_t cnt, int64_t first, Decoded& out, std::string& err) {
    out.notes.assign(cnt, Note{});
    Cur c{p, end};
    int64_t at = first;
    for (Note& n : out.notes) {
        at += c.zig();
        n.time = double(at) / 1e6;
    }
    for (Note& n : out.notes) {
        const uint64_t v = c.var();
        if (v >= uint64_t(2 * kMaxKeys)) { err = "lane out of range"; return false; }
        n.lane = uint16_t(v >> 1);
        n.side = uint8_t(v & 1);
    }
    for (Note& n : out.notes) {
        const uint64_t v = c.var();
        if (v >= (uint64_t(256) << 7)) { err = "bad note kind"; return false; }
        n.kind = uint8_t(v & 0x7F) == kMine ? kMine : kNormal;
        n.flags = uint8_t((v >> 7) & ~uint64_t(kFlagBigCount));
    }
    for (Note& n : out.notes) {
        const uint64_t v = c.var();
        if (v > 0xFFFF) { err = "bad note type"; return false; }
        n.type = uint16_t(v);
    }
    for (Note& n : out.notes) n.length = float(double(c.var()) / 1e6);
    if (!c.ok) { err = "cut short"; return false; }
    for (Note& n : out.notes) {
        Count k;
        switch (Count::readVarint(c.p, c.end, k)) {
            case Count::Read::Truncated: err = "cut short"; return false;
            case Count::Read::TooLarge: err = "a note counts for more than 2^256 notes, more than this game counts"; return false;
            case Count::Read::Ok: break;
        }
        if (k.isMax()) { err = "a note counts for more than 2^256 notes, more than this game counts"; return false; }
        k += Count(1);
        if (k.fits64() && k.w[0] <= kMaxStack) {
            n.setCount32(uint32_t(k.w[0]));
        } else {
            n.flags = uint8_t(n.flags | kFlagBigCount);
            n.setCount32(uint32_t(out.big.size()));
            out.big.push_back(k);
        }
    }
    if (c.p != end) { err = "a block holds more than it says"; return false; }
    return true;
}

struct Entry {
    uint8_t kind = 0;
    uint64_t notes = 0, rawSize = 0, packed = 0, offset = 0;
    uint32_t crc = 0;
    int64_t first = 0;
    uint64_t source = 0, copies = 0;
    int64_t shift = 0, start = 0;
};

// Copies of a block's notes, start + j * shift for j < copies, added to the
// chart: every copy as a note when they are few; else those falling in the
// same millisecond of a lane become one note counting for all of them.
bool addCopies(Chart& out, const Decoded& src, int64_t srcFirst, const Entry& e, size_t& foldedAt, std::string& err) {
    auto addNote = [&](const Note& n, double timeMs, const Count& count) {
        Note x = n;
        x.time = timeMs;
        x.flags = uint8_t(x.flags & ~kFlagBigCount);
        out.notes.push_back(x);
        out.setCount(out.notes.back(), count);
    };
    auto countOf = [&](const Note& n) {
        if (!(n.flags & kFlagBigCount)) return Count(n.count32());
        return n.count32() < src.big.size() ? src.big[n.count32()] : Count(1);
    };
    const uint64_t perCopy = src.notes.size();
    if (e.copies == 0 || perCopy == 0) return true;
    const bool few = e.copies <= kMaxCells && perCopy * e.copies <= kMaxCells / 4;
    if (e.shift != 0 && (e.copies - 1) > uint64_t(INT64_MAX / std::llabs(e.shift))) { err = "a repeat runs past 292 years"; return false; }
    for (const Note& n : src.notes) {
        const int64_t t0 = toNs(n.time) - srcFirst + e.start;
        const Count k = countOf(n);
        if (few || e.shift == 0) {
            if (e.shift == 0) {
                const Count all = k.times(e.copies);
                if (all.isMax()) { err = "a note counts for more than 2^256 notes, more than this game counts"; return false; }
                addNote(n, double(t0) / 1e6, all);
            } else {
                for (uint64_t j = 0; j < e.copies; ++j) addNote(n, double(t0 + int64_t(j) * e.shift) / 1e6, k);
                foldIfLarge(out, foldedAt);
            }
            continue;
        }
        // Many copies: a millisecond at a time. Copies j with t0 + j*shift in
        // [b, b + 1 ms) for each millisecond b the copies cross.
        const int64_t step = std::llabs(e.shift);
        const int64_t last = t0 + int64_t(e.copies - 1) * e.shift;
        const int64_t lo = std::min(t0, last), hi = std::max(t0, last);
        auto floorDiv = [](int64_t a, int64_t b) { return a / b - ((a % b) != 0 && ((a < 0) != (b < 0))); };
        const int64_t b0 = floorDiv(lo, kNsPerMs), b1 = floorDiv(hi, kNsPerMs);
        if (uint64_t(b1 - b0) + out.notes.size() > kMaxCells) {
            err = "the chart has more notes apart than this game holds at once";
            return false;
        }
        // In the order of time: j counted from the earliest copy.
        const int64_t base = e.shift > 0 ? t0 : last;
        for (int64_t b = b0; b <= b1; ++b) {
            const int64_t from = b * kNsPerMs - base, to = (b + 1) * kNsPerMs - base;  // relative to the earliest copy
            const int64_t j0 = std::max<int64_t>(0, from <= 0 ? 0 : (from + step - 1) / step);
            const int64_t j1 = std::min<int64_t>(int64_t(e.copies), to <= 0 ? 0 : (to + step - 1) / step);
            if (j1 <= j0) continue;
            addNote(n, double(base + j0 * step) / 1e6, k.times(uint64_t(j1 - j0)));
        }
        foldIfLarge(out, foldedAt);
    }
    return true;
}
}  // namespace ghnfmt

bool loadGhn(const std::string& path, Chart& out, std::string& err) {
    using namespace ghnfmt;
    InFile f(path);
    unsigned char h[kHeader];
    if (!f.ok() || f.size() < kHeader || !f.read(h, kHeader)) { err = "cannot read " + path; return false; }
    if (crc32Of(h, 60) != le32(h + 60)) { err = ".ghn: damaged header"; return false; }
    Cur c{h + 4, h + kHeader};
    const uint32_t version = c.u32();
    c.u32();
    const uint32_t blocks = c.u32();
    const uint64_t metaOff = c.u64();
    const uint32_t metaPacked = c.u32(), metaRaw = c.u32(), metaCrc = c.u32();
    const uint64_t dirOff = c.u64();
    const uint32_t dirSize = c.u32(), dirCrc = c.u32();
    const uint64_t dataOff = c.u64();
    if (version != 1) { err = ".ghn version " + std::to_string(version) + " is newer than this game"; return false; }
    // Meta.
    std::vector<unsigned char> meta;
    if (!raidfmt::readSection(f, metaOff, metaPacked, metaRaw, meta, err)) return false;
    if (crc32Of(meta.data(), meta.size()) != metaCrc) { err = ".ghn: damaged meta"; return false; }
    json::Document d;
    if (!d.parse(std::string_view(reinterpret_cast<const char*>(meta.data()), meta.size()))) { err = ".ghn: bad meta"; return false; }
    const json::Value& r = d.root();
    out = Chart();
    metaChart(d, r, out);
    if (const json::Value* sk = d.get(r, "sideKeys"); sk && d.size(*sk) == 2)
        for (int s = 0; s < 2; ++s) out.sideKeys[s] = std::clamp(int(json::Document::num(&d.at(*sk, size_t(s)), 0)), 0, kMaxKeys);
    if (const json::Value* ev = d.get(r, "events"))
        for (size_t i = 0; i < d.size(*ev); ++i) {
            const json::Value& e = d.at(*ev, i);
            if (d.size(e) < 4) continue;
            out.events.push_back({json::Document::num(&d.at(e, 0), 0), std::string(d.str(d.at(e, 1))), std::string(d.str(d.at(e, 2))),
                                  std::string(d.str(d.at(e, 3)))});
        }
    Count wantP, wantO, wantRaw;
    const bool totals = Count::parse(d.strOr(d.get(r, "playerNotes"), ""), wantP) &&
                        Count::parse(d.strOr(d.get(r, "opponentNotes"), ""), wantO) &&
                        Count::parse(d.strOr(d.get(r, "rawNotes"), ""), wantRaw);
    // Directory.
    std::vector<unsigned char> dir;
    if (!raidfmt::readSection(f, dirOff, 0, dirSize, dir, err)) return false;
    if (crc32Of(dir.data(), dir.size()) != dirCrc) { err = ".ghn: damaged directory"; return false; }
    std::vector<Entry> entries(blocks);
    Cur dc{dir.data(), dir.data() + dir.size()};
    for (uint32_t i = 0; i < blocks; ++i) {
        Entry& e = entries[i];
        e.kind = dc.u8();
        if (e.kind == 0) {
            e.notes = dc.var();
            e.rawSize = dc.var();
            e.packed = dc.var();
            e.crc = dc.u32();
            e.first = dc.zig();
            e.offset = dc.var();
            if (e.notes == 0 || e.notes > (uint64_t(1) << 24) || e.rawSize > (uint64_t(1) << 30) || e.packed > (uint64_t(1) << 30)) {
                err = ".ghn: bad block";
                return false;
            }
        } else if (e.kind == 1) {
            e.source = dc.var();
            e.copies = dc.var();
            e.shift = dc.zig();
            e.start = dc.zig();
            if (e.source >= i || entries[size_t(e.source)].kind != 0) { err = ".ghn: a repeat of no block"; return false; }
        } else {
            err = ".ghn: unknown block kind";
            return false;
        }
        if (!dc.ok) { err = ".ghn: cut short"; return false; }
    }
    // Notes blocks, read in order and decoded on every core.
    std::vector<std::vector<unsigned char>> raw(blocks);
    for (uint32_t i = 0; i < blocks; ++i) {
        const Entry& e = entries[i];
        if (e.kind != 0) continue;
        if (!raidfmt::readSection(f, dataOff + e.offset, e.packed, e.rawSize, raw[i], err)) return false;
        if (crc32Of(raw[i].data(), raw[i].size()) != e.crc) { err = ".ghn: damaged block " + std::to_string(i); return false; }
    }
    std::vector<Decoded> dec(blocks);
    std::vector<std::string> errs(blocks);
    {
        const unsigned workers = std::max(1u, std::min(std::thread::hardware_concurrency(), 16u));
        std::atomic<uint32_t> next{0};
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < workers; ++w)
            pool.emplace_back([&] {
                for (uint32_t i; (i = next++) < blocks;) {
                    const Entry& e = entries[i];
                    if (e.kind != 0) continue;
                    decodeBlock(raw[i].data(), raw[i].data() + raw[i].size(), size_t(e.notes), e.first, dec[i], errs[i]);
                }
            });
        for (std::thread& t : pool) t.join();
    }
    size_t foldedAt = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        if (!errs[i].empty()) { err = ".ghn: block " + std::to_string(i) + ": " + errs[i]; return false; }
        const Entry& e = entries[i];
        if (e.kind == 0) {
            const Decoded& b = dec[i];
            for (Note n : b.notes) {
                Count k = (n.flags & kFlagBigCount) ? b.big[n.count32()] : Count(n.count32());
                n.flags = uint8_t(n.flags & ~kFlagBigCount);
                out.notes.push_back(n);
                out.setCount(out.notes.back(), k);
            }
            foldIfLarge(out, foldedAt);
        } else if (!addCopies(out, dec[size_t(e.source)], entries[size_t(e.source)].first, e, foldedAt, err)) {
            err = ".ghn: " + err;
            return false;
        }
    }
    for (const Note& n : out.notes) out.keys = std::max(out.keys, int(n.lane) + 1);
    out.keys = std::min(out.keys, kMaxKeys);
    out.finalize(out.tempo.empty() ? 120 : out.tempo.front().bpm);
    if (totals) {
        if (out.playerNotes != wantP || out.opponentNotes != wantO) {
            err = ".ghn: its notes do not add up (" + (out.playerNotes + out.opponentNotes).grouped() + " instead of " +
                  (wantP + wantO).grouped() + ")";
            return false;
        }
        out.rawNoteCount = wantRaw;
    }
    return true;
}

namespace ghnfmt {
// The shortest period p (notes) after which the chart repeats itself moved
// in time - a pattern spammed over and over - looked for in its first notes;
// 0 when there is none.
size_t periodOf(const Chart& c) {
    const size_t n = c.notes.size();
    if (n < 4 * kBlockNotes) return 0;
    const size_t look = std::min<size_t>(n, 1 << 20);
    std::vector<Shape> sh(look);
    std::vector<int64_t> dt(look);
    for (size_t i = 0; i < look; ++i) {
        sh[i] = shapeOf(c, c.notes[i]);
        dt[i] = i + 1 < look ? toNs(c.notes[i + 1].time) - toNs(c.notes[i].time) : 0;
    }
    for (size_t p = 1; p <= 4096 && 4 * p < look; ++p) {
        bool same = true;
        for (size_t i = 0; i + p + 1 < look && same; ++i) same = sh[i] == sh[i + p] && dt[i] == dt[i + p];
        if (same) return p;
    }
    return 0;
}
}  // namespace ghnfmt

}  // namespace

bool writeGhn(const std::string& path, const Chart& chart, std::string& err, GhnStats* stats) {
    using namespace ghnfmt;
    const std::string meta = metaOf(chart);
    const size_t n = chart.notes.size();
    // Blocks of notes, a whole number of periods each when the chart repeats.
    size_t blockNotes = kBlockNotes;
    if (const size_t p = periodOf(chart)) blockNotes = p * std::max<size_t>(1, kBlockNotes / p);
    struct Out {
        Entry e;
        std::string payload;
    };
    std::vector<Out> outs;
    size_t lastLiteral = SIZE_MAX;
    auto sameMoved = [&](size_t a, size_t b, size_t cnt, int64_t& shift) {  // block b == block a moved by shift
        shift = toNs(chart.notes[b].time) - toNs(chart.notes[a].time);
        for (size_t i = 0; i < cnt; ++i) {
            if (toNs(chart.notes[b + i].time) - toNs(chart.notes[a + i].time) != shift) return false;
            if (!(shapeOf(chart, chart.notes[a + i]) == shapeOf(chart, chart.notes[b + i]))) return false;
        }
        return true;
    };
    std::vector<size_t> literalStart;  // first note of each literal entry
    uint64_t dataSize = 0;
    for (size_t from = 0; from < n; from += blockNotes) {
        const size_t cnt = std::min(blockNotes, n - from);
        int64_t shift = 0;
        if (lastLiteral != SIZE_MAX && cnt == outs[lastLiteral].e.notes && sameMoved(literalStart[lastLiteral], from, cnt, shift)) {
            Out& prev = outs.back();
            if (prev.e.kind == 1 && prev.e.source == lastLiteral &&
                ((prev.e.copies == 1 && toNs(chart.notes[from].time) - prev.e.start != 0) ||
                 (prev.e.copies > 1 && toNs(chart.notes[from].time) == prev.e.start + int64_t(prev.e.copies) * prev.e.shift))) {
                if (prev.e.copies == 1) prev.e.shift = toNs(chart.notes[from].time) - prev.e.start;
                ++prev.e.copies;
                literalStart.push_back(SIZE_MAX);
                continue;
            }
            Out rep;
            rep.e.kind = 1;
            rep.e.source = lastLiteral;
            rep.e.copies = 1;
            rep.e.start = toNs(chart.notes[from].time);
            outs.push_back(rep);
            literalStart.push_back(SIZE_MAX);
            continue;
        }
        Out lit;
        lit.e.kind = 0;
        lit.e.notes = cnt;
        lit.e.first = toNs(chart.notes[from].time);
        const std::string raw = encodeBlock(chart, chart.notes.data() + from, cnt, lit.e.first);
        lit.e.rawSize = raw.size();
        lit.e.crc = crc32Of(raw.data(), raw.size());
        std::string packed = deflateOf(raw, 6);
        if (!packed.empty() && packed.size() < raw.size()) {
            lit.e.packed = packed.size();
            lit.payload = std::move(packed);
        } else {
            lit.payload = raw;
        }
        lit.e.offset = dataSize;
        dataSize += lit.payload.size();
        lastLiteral = outs.size();
        outs.push_back(std::move(lit));
        literalStart.push_back(from);
    }
    std::string dir;
    for (const Out& o : outs) {
        dir += char(o.e.kind);
        if (o.e.kind == 0) {
            putVar(dir, o.e.notes);
            putVar(dir, o.e.rawSize);
            putVar(dir, o.e.packed);
            put32(dir, o.e.crc);
            putZig(dir, o.e.first);
            putVar(dir, o.e.offset);
        } else {
            putVar(dir, o.e.source);
            putVar(dir, o.e.copies);
            putZig(dir, o.e.shift);
            putZig(dir, o.e.start);
        }
    }
    std::string metaStored = deflateOf(meta, 9);
    const bool metaPacked = !metaStored.empty() && metaStored.size() < meta.size();
    if (!metaPacked) metaStored = meta;
    std::string head = "GHN1";
    put32(head, 1);
    put32(head, 0);
    put32(head, uint32_t(outs.size()));
    const uint64_t metaOff = kHeader, dirOff = metaOff + metaStored.size(), dataOff = dirOff + dir.size();
    put64(head, metaOff);
    put32(head, metaPacked ? uint32_t(metaStored.size()) : 0);
    put32(head, uint32_t(meta.size()));
    put32(head, crc32Of(meta.data(), meta.size()));
    put64(head, dirOff);
    put32(head, uint32_t(dir.size()));
    put32(head, crc32Of(dir.data(), dir.size()));
    put64(head, dataOff);
    put32(head, crc32Of(head.data(), 60));
    const std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { err = "cannot write " + path; return false; }
    bool ok = std::fwrite(head.data(), 1, head.size(), f) == head.size() &&
              std::fwrite(metaStored.data(), 1, metaStored.size(), f) == metaStored.size() &&
              std::fwrite(dir.data(), 1, dir.size(), f) == dir.size();
    for (const Out& o : outs)
        if (ok && !o.payload.empty()) ok = std::fwrite(o.payload.data(), 1, o.payload.size(), f) == o.payload.size();
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    auto u8 = [](const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); };
    if (ok) std::filesystem::rename(u8(tmp), u8(path), ec);
    if (!ok || ec) {
        std::filesystem::remove(u8(tmp), ec);
        err = "cannot write " + path;
        return false;
    }
    if (stats) {
        stats->notes = n;
        stats->blocks = 0;
        stats->repeats = 0;
        for (const Out& o : outs) (o.e.kind == 0 ? stats->blocks : stats->repeats) += 1;
        stats->bytes = dataOff + dataSize;
        stats->total = chart.playerNotes + chart.opponentNotes;
    }
    return true;
}

namespace {
}  // namespace

CountChartKind countChartKind(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return CountChartKind::None;
    unsigned char m[5] = {};
    const size_t n = std::fread(m, 1, 5, f);
    std::fclose(f);
    if (n == 5 && std::memcmp(m, "FNC1", 4) == 0 && m[4] >= 2 && m[4] <= 4) return CountChartKind::Fnc1;
    if (n >= 4 && std::memcmp(m, "RAID", 4) == 0) return CountChartKind::Raid;
    if (n >= 4 && std::memcmp(m, "GHN1", 4) == 0) return CountChartKind::Ghn;
    return CountChartKind::None;
}

bool loadCountChart(const std::string& path, Chart& out, std::string& err) {
    switch (countChartKind(path)) {
        case CountChartKind::Fnc1: return loadFnc1(path, out, err);
        case CountChartKind::Raid: return loadRaid(path, out, err);
        case CountChartKind::Ghn: return loadGhn(path, out, err);
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

int convertChartFile(const std::string& in, const std::string& out) {
    const auto t0 = std::chrono::steady_clock::now();
    auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    std::string err;
    if (endsWithI(out, kBinaryChartExt)) {
        ConvertStats st;
        if (!convertChartToBinary(in, out, err, &st)) {
            std::fprintf(stderr, "%s: %s\n", in.c_str(), err.c_str());
            return 1;
        }
        std::printf("%s -> %s: %s notes, %s bytes, %.2f s\n", in.c_str(), out.c_str(), Count(st.notes).grouped().c_str(),
                    Count(st.outBytes).grouped().c_str(), seconds());
        return 0;
    }
    Chart c;
    if (!loadChartFile(in, "Normal", c, err)) {
        std::fprintf(stderr, "%s: %s\n", in.c_str(), err.c_str());
        return 1;
    }
    if (endsWithI(out, kCountChartExt)) {
        GhnStats st;
        if (!writeGhn(out, c, err, &st)) {
            std::fprintf(stderr, "%s: %s\n", out.c_str(), err.c_str());
            return 1;
        }
        std::printf("%s -> %s: %s notes in %s (%u blocks, %u repeated), %s bytes, %.2f s\n", in.c_str(), out.c_str(),
                    st.total.grouped().c_str(), Count(st.notes).grouped().c_str(), st.blocks, st.repeats, Count(st.bytes).grouped().c_str(),
                    seconds());
        return 0;
    }
    if (endsWithI(out, ".bin")) {
        if (!writeFnc1(out, c, err)) {
            std::fprintf(stderr, "%s: %s\n", out.c_str(), err.c_str());
            return 1;
        }
        std::printf("%s -> %s (iutk FNC1): %s notes, %.2f s\n", in.c_str(), out.c_str(), (c.playerNotes + c.opponentNotes).grouped().c_str(),
                    seconds());
        return 0;
    }
    std::fprintf(stderr, "%s: write .ghn, .ghc or .bin\n", out.c_str());
    return 1;
}

}  // namespace gh
