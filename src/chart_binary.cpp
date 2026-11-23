// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// God's Hand binary charts (.ghc); see chart_binary.hpp.
//
//   "GHCB" u32 version
//   u64 notes, u64 rawCount, i32 maxData, u32 blockNotes, u32 blocks, u32 types
//   version 3: u32 values, u64 dataCount[values] (up to 2048: 1024 keys a side);
//   version 2: u64 dataCount[256]; version 1: [64]
//   types: u32 length + bytes, each
//   u64 skeleton bytes, u64 packed, packed bytes           (deflate)
//   u64 ranges, u64 bytes, u64 packed, packed bytes        (varint gaps)
//   version 4: u64 big, u64 bytes, u64 packed, packed bytes (the notes whose
//       stack is 0 - a count chart's cells - each as the varint gap from the
//       last one's index, then its count as a varint of any length)
//   blocks: u32 bytes, u32 packed, each; then the packed blocks
//
// A block holds five columns, their lengths first (u32 each): times (the
// change of the gap from the note before, in ns, zigzag varints), lengths
// (us, zigzag varints), noteData (version 3: varints, 0..2047; before: bytes),
// stack bytes (0: the count is in the big table), types (varints; bit 15 the
// alt flag). Version 1 kept the alt flag in bit 7 of noteData (values 0..63
// only). Version 3 is written when no note needs the big table; 1 to 3 are
// still read.
#include "chart_binary.hpp"

#include "bigit_chart.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "miniz.h"

namespace gh {

namespace {

constexpr uint32_t kMagic = 0x42434847u;  // "GHCB"
constexpr uint32_t kVersion = 4;  // 1 to 3 are still read
constexpr uint32_t kBlockNotes = 1u << 18;  // 262144 notes: enough blocks for every core

void putU32(std::string& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o += char((v >> (8 * i)) & 0xFF);
}
void putU64(std::string& o, uint64_t v) {
    for (int i = 0; i < 8; ++i) o += char((v >> (8 * i)) & 0xFF);
}
void putVar(std::string& o, uint64_t v) {
    while (v >= 0x80) {
        o += char(uint8_t(v) | 0x80);
        v >>= 7;
    }
    o += char(uint8_t(v));
}
uint64_t zig(int64_t v) { return (uint64_t(v) << 1) ^ uint64_t(v >> 63); }
int64_t unzig(uint64_t v) { return int64_t(v >> 1) ^ -int64_t(v & 1); }
bool getVar(const uint8_t*& p, const uint8_t* e, uint64_t& v) {
    v = 0;
    for (int sh = 0; p < e && sh < 64; sh += 7) {
        const uint8_t b = *p++;
        v |= uint64_t(b & 0x7F) << sh;
        if (!(b & 0x80)) return true;
    }
    return false;
}
uint32_t getU32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// Exact: a whole file read or written, or false.
bool readAll(FILE* f, void* dst, size_t n) { return std::fread(dst, 1, n, f) == n; }
bool readU32(FILE* f, uint32_t& v) {
    uint8_t b[4];
    if (!readAll(f, b, 4)) return false;
    v = getU32(b);
    return true;
}
bool readU64(FILE* f, uint64_t& v) {
    uint8_t b[8];
    if (!readAll(f, b, 8)) return false;
    v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | b[i];
    return true;
}

bool pack(const std::string& raw, std::string& out, int level) {
    mz_ulong n = mz_compressBound(mz_ulong(raw.size()));
    out.resize(size_t(n));
    if (mz_compress2(reinterpret_cast<unsigned char*>(out.data()), &n, reinterpret_cast<const unsigned char*>(raw.data()),
                     mz_ulong(raw.size()), level) != MZ_OK)
        return false;
    out.resize(size_t(n));
    return true;
}
bool unpack(const uint8_t* src, size_t n, std::string& out, size_t rawLen) {
    out.resize(rawLen);
    if (rawLen == 0) return true;
    mz_ulong m = mz_ulong(rawLen);
    return mz_uncompress(reinterpret_cast<unsigned char*>(out.data()), &m, src, mz_ulong(n)) == MZ_OK && size_t(m) == rawLen;
}

// The columns of notes [0, count).
std::string encodeBlock(const StreamedNote* n, size_t count) {
    std::string t, l, d, s, y;
    t.reserve(count * 2);
    l.reserve(count);
    d.reserve(count);
    s.reserve(count);
    y.reserve(count);
    int64_t prevT = 0, prevGap = 0;
    for (size_t i = 0; i < count; ++i) {
        const double tm = std::isfinite(n[i].time) ? n[i].time : 0.0;
        const int64_t ns = std::llround(tm * 1e6);
        const int64_t gap = ns - prevT;
        putVar(t, zig(gap - prevGap));  // steady notes: zeros, which deflate makes almost nothing of
        prevGap = gap;
        prevT = ns;
        putVar(l, zig(std::llround(double(std::isfinite(n[i].length) ? n[i].length : 0.0f) * 1000.0)));
        putVar(d, uint64_t(n[i].noteData()));
        s += char(n[i].stack);
        putVar(y, n[i].type & (StreamedNote::kAltBit | StreamedNote::kTypeMask));
    }
    std::string raw;
    raw.reserve(20 + t.size() + l.size() + d.size() + s.size() + y.size());
    for (const std::string* c : {&t, &l, &d, &s, &y}) putU32(raw, uint32_t(c->size()));
    for (const std::string* c : {&t, &l, &d, &s, &y}) raw += *c;
    return raw;
}

bool decodeBlock(const std::string& raw, StreamedNote* out, size_t count, uint32_t version) {
    if (raw.size() < 20) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(raw.data());
    uint64_t lens[5], total = 20;
    for (int i = 0; i < 5; ++i) total += (lens[i] = getU32(p + 4 * i));
    if (total != raw.size() || (version < 3 && lens[2] != count) || lens[3] != count) return false;
    const uint8_t* t = p + 20;
    const uint8_t* l = t + lens[0];
    const uint8_t* d = l + lens[1];
    const uint8_t* s = d + lens[2];
    const uint8_t* y = s + lens[3];
    const uint8_t* end = y + lens[4];
    int64_t prevT = 0, prevGap = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t v;
        if (!getVar(t, l, v)) return false;
        prevGap += unzig(v);
        prevT += prevGap;
        out[i].time = double(prevT) / 1e6;
        if (!getVar(l, d, v)) return false;
        out[i].length = float(double(unzig(v)) / 1000.0);
        out[i].stack = s[i];
        if (!getVar(y, end, v) || v > 65535) return false;
        out[i].type = uint16_t(v & (StreamedNote::kAltBit | StreamedNote::kTypeMask));
        if (version >= 3) {
            uint64_t nd;
            if (!getVar(d, s, nd) || nd >= uint64_t(kNoteDataValues)) return false;
            out[i].setNoteData(int(nd));
        } else if (version == 2) {
            out[i].setNoteData(d[i]);
        } else {  // the alt flag was bit 7 of noteData
            out[i].setNoteData(d[i] & 63);
            if (d[i] & 128) out[i].type |= StreamedNote::kAltBit;
        }
    }
    return t == p + 20 + lens[0] && y == end && (version < 3 || d == s);
}

int workerCount(int threads) {
    if (threads > 0) return threads;
    return std::max(1, int(std::thread::hardware_concurrency()));
}

}  // namespace

std::string ghcColumns(const StreamedNote* notes, size_t count) { return encodeBlock(notes, count); }
bool ghcFromColumns(const std::string& raw, StreamedNote* out, size_t count) { return decodeBlock(raw, out, count, kVersion); }

bool isBinaryChart(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t m = 0;
    const bool ok = readU32(f, m) && m == kMagic;
    std::fclose(f);
    return ok;
}

bool writeBinaryChart(const std::string& path, const std::string& skeleton, const StreamedNotes& sn, int level,
                      std::string& err) {
    level = std::clamp(level, 1, 9);
    const size_t n = sn.notes.size();
    const uint32_t blocks = uint32_t((n + kBlockNotes - 1) / kBlockNotes);
    // The blocks, packed on every core.
    std::vector<std::string> packed(blocks);
    std::vector<uint32_t> rawLens(blocks);
    {
        const int workers = workerCount(-1);
        std::vector<std::future<bool>> jobs;
        for (int w = 0; w < workers; ++w)
            jobs.push_back(std::async(std::launch::async, [&, w] {
                for (uint32_t b = uint32_t(w); b < blocks; b += uint32_t(workers)) {
                    const size_t from = size_t(b) * kBlockNotes, cnt = std::min<size_t>(kBlockNotes, n - from);
                    const std::string raw = encodeBlock(sn.notes.data() + from, cnt);
                    rawLens[b] = uint32_t(raw.size());
                    if (!pack(raw, packed[b], level)) return false;
                }
                return true;
            }));
        bool ok = true;
        for (auto& j : jobs) ok = j.get() && ok;
        if (!ok) {
            err = "could not pack the notes";
            return false;
        }
    }
    std::string head;
    const uint32_t version = sn.big.empty() ? 3u : kVersion;  // 3: what older versions of the game read
    putU32(head, kMagic);
    putU32(head, version);
    putU64(head, n);
    putU64(head, sn.rawCount);
    putU32(head, uint32_t(int32_t(sn.maxData)));
    putU32(head, kBlockNotes);
    putU32(head, blocks);
    putU32(head, uint32_t(sn.types.size()));
    // The counts up to the last noteData used: most charts need 8.
    size_t values = sn.dataCount.size();
    while (values > 0 && sn.dataCount[values - 1] == 0) --values;
    putU32(head, uint32_t(values));
    for (size_t i = 0; i < values; ++i) putU64(head, sn.dataCount[i]);
    for (const std::string& t : sn.types) {
        putU32(head, uint32_t(t.size()));
        head += t;
    }
    std::string skel;
    if (!pack(skeleton, skel, level)) {
        err = "could not pack the chart's data";
        return false;
    }
    putU64(head, skeleton.size());
    putU64(head, skel.size());
    head += skel;
    std::string ranges, rangesPacked;
    uint32_t prev = 0;
    for (uint32_t r : sn.rangeStart) {
        putVar(ranges, r - prev);
        prev = r;
    }
    if (!pack(ranges, rangesPacked, level)) {
        err = "could not pack the sections";
        return false;
    }
    putU64(head, sn.rangeStart.size());
    putU64(head, ranges.size());
    putU64(head, rangesPacked.size());
    head += rangesPacked;
    if (version >= 4) {
        std::string big, bigPacked;
        uint64_t last = 0;
        for (const auto& [idx, count] : sn.big) {
            putVar(big, idx - last);
            last = idx;
            count.appendVarint(big);
        }
        if (!pack(big, bigPacked, level)) {
            err = "could not pack the counts";
            return false;
        }
        putU64(head, sn.big.size());
        putU64(head, big.size());
        putU64(head, bigPacked.size());
        head += bigPacked;
    }
    for (uint32_t b = 0; b < blocks; ++b) {
        putU32(head, rawLens[b]);
        putU32(head, uint32_t(packed[b].size()));
    }
    // Written beside it and moved in place: a reader never sees half a file.
    const std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        err = "cannot write " + path;
        return false;
    }
    bool ok = std::fwrite(head.data(), 1, head.size(), f) == head.size();
    for (uint32_t b = 0; ok && b < blocks; ++b) ok = std::fwrite(packed[b].data(), 1, packed[b].size(), f) == packed[b].size();
    ok = (std::fclose(f) == 0) && ok;
    if (ok) {
        std::remove(path.c_str());
        ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    }
    if (!ok) {
        std::remove(tmp.c_str());
        err = "cannot write " + path;
    }
    return ok;
}

namespace {

// Everything before the note blocks.
struct Head {
    uint32_t version = kVersion;
    uint64_t notes = 0, rawCount = 0;
    int32_t maxData = -1;
    uint32_t blockNotes = 0, blocks = 0, types = 0;
};

bool readHead(FILE* f, Head& h, std::string& skeleton, StreamedNotes* sn, std::string& err) {
    uint32_t magic = 0, version = 0, maxData = 0;
    if (!readU32(f, magic) || magic != kMagic) { err = "not a God's Hand binary chart"; return false; }
    if (!readU32(f, version) || version < 1 || version > kVersion) { err = "a binary chart of a newer God's Hand"; return false; }
    h.version = version;
    if (!readU64(f, h.notes) || !readU64(f, h.rawCount) || !readU32(f, maxData) || !readU32(f, h.blockNotes) ||
        !readU32(f, h.blocks) || !readU32(f, h.types) || h.blockNotes == 0 ||
        uint64_t(h.blocks) != (h.notes + h.blockNotes - 1) / h.blockNotes) {
        err = "damaged binary chart";
        return false;
    }
    h.maxData = int32_t(maxData);
    std::array<uint64_t, kNoteDataValues> counts{};
    uint32_t values = version < 2 ? 64 : 256;
    if (version >= 3 && (!readU32(f, values) || values > counts.size())) { err = "damaged binary chart"; return false; }
    for (size_t i = 0; i < values; ++i)
        if (!readU64(f, counts[i])) { err = "damaged binary chart"; return false; }
    std::vector<std::string> types(h.types);
    for (std::string& t : types) {
        uint32_t len = 0;
        if (!readU32(f, len) || len > (1u << 20)) { err = "damaged binary chart"; return false; }
        t.resize(len);
        if (len && !readAll(f, t.data(), len)) { err = "damaged binary chart"; return false; }
    }
    uint64_t raw = 0, packedLen = 0;
    if (!readU64(f, raw) || !readU64(f, packedLen) || raw > (uint64_t(1) << 34) || packedLen > (uint64_t(1) << 34)) {
        err = "damaged binary chart";
        return false;
    }
    std::string buf(size_t(packedLen), '\0');
    if ((packedLen && !readAll(f, buf.data(), buf.size())) ||
        !unpack(reinterpret_cast<const uint8_t*>(buf.data()), buf.size(), skeleton, size_t(raw))) {
        err = "damaged binary chart";
        return false;
    }
    if (!sn) return true;
    sn->types = std::move(types);
    sn->maxData = h.maxData;
    sn->dataCount = counts;
    sn->rawCount = h.rawCount;
    uint64_t ranges = 0, rangesRaw = 0, rangesPacked = 0;
    if (!readU64(f, ranges) || !readU64(f, rangesRaw) || !readU64(f, rangesPacked) || rangesRaw > (uint64_t(1) << 34) ||
        rangesPacked > (uint64_t(1) << 34) || ranges > rangesRaw) {
        err = "damaged binary chart";
        return false;
    }
    buf.assign(size_t(rangesPacked), '\0');
    std::string gaps;
    if ((rangesPacked && !readAll(f, buf.data(), buf.size())) ||
        !unpack(reinterpret_cast<const uint8_t*>(buf.data()), buf.size(), gaps, size_t(rangesRaw))) {
        err = "damaged binary chart";
        return false;
    }
    sn->rangeStart.resize(size_t(ranges));
    const uint8_t* p = reinterpret_cast<const uint8_t*>(gaps.data());
    const uint8_t* e = p + gaps.size();
    uint64_t at = 0;
    for (uint32_t& r : sn->rangeStart) {
        uint64_t v;
        if (!getVar(p, e, v)) { err = "damaged binary chart"; return false; }
        at += v;
        r = uint32_t(at);
    }
    if (version >= 4) {  // the counts of the notes whose stack is 0
        uint64_t bigN = 0, bigRaw = 0, bigPacked = 0;
        if (!readU64(f, bigN) || !readU64(f, bigRaw) || !readU64(f, bigPacked) || bigRaw > (uint64_t(1) << 34) ||
            bigPacked > (uint64_t(1) << 34) || bigN > h.notes) {
            err = "damaged binary chart";
            return false;
        }
        buf.assign(size_t(bigPacked), '\0');
        std::string raw;
        if ((bigPacked && !readAll(f, buf.data(), buf.size())) ||
            !unpack(reinterpret_cast<const uint8_t*>(buf.data()), buf.size(), raw, size_t(bigRaw))) {
            err = "damaged binary chart";
            return false;
        }
        const uint8_t* q = reinterpret_cast<const uint8_t*>(raw.data());
        const uint8_t* qe = q + raw.size();
        sn->big.resize(size_t(bigN));
        uint64_t idx = 0;
        for (auto& [i, count] : sn->big) {
            uint64_t gap;
            if (!getVar(q, qe, gap)) { err = "damaged binary chart"; return false; }
            idx += gap;
            i = idx;
            if (Count::readVarint(q, qe, count) == Count::Read::Truncated) { err = "damaged binary chart"; return false; }
        }
    }
    return true;
}

}  // namespace

bool readBinarySkeleton(const std::string& path, std::string& skeleton) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    Head h;
    std::string err;
    const bool ok = readHead(f, h, skeleton, nullptr, err);
    std::fclose(f);
    return ok;
}

bool readBinaryChart(const std::string& path, std::string& skeleton, StreamedNotes& sn, std::string& err, int threads) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "cannot read " + path;
        return false;
    }
    Head h;
    sn = StreamedNotes{};
    bool ok = readHead(f, h, skeleton, &sn, err);
    std::vector<uint32_t> rawLens(ok ? h.blocks : 0), packedLens(ok ? h.blocks : 0);
    for (uint32_t b = 0; ok && b < h.blocks; ++b) ok = readU32(f, rawLens[b]) && readU32(f, packedLens[b]);
    if (ok) {
        sn.notes.resize(size_t(h.notes));
        // Read in order, unpacked on every core: a few blocks in flight at once.
        const size_t window = size_t(workerCount(threads)) * 2;
        std::deque<std::future<bool>> flight;
        for (uint32_t b = 0; ok && b < h.blocks; ++b) {
            auto packed = std::make_shared<std::string>(packedLens[b], '\0');
            if (!packed->empty() && !readAll(f, packed->data(), packed->size())) {
                ok = false;
                break;
            }
            const size_t from = size_t(b) * h.blockNotes, cnt = std::min<size_t>(h.blockNotes, size_t(h.notes) - from);
            StreamedNote* dst = sn.notes.data() + from;
            const uint32_t rawLen = rawLens[b];
            const uint32_t version = h.version;
            flight.push_back(std::async(std::launch::async, [packed, dst, cnt, rawLen, version] {
                std::string raw;
                return unpack(reinterpret_cast<const uint8_t*>(packed->data()), packed->size(), raw, rawLen) &&
                       decodeBlock(raw, dst, cnt, version);
            }));
            while (flight.size() >= window) {
                ok = flight.front().get() && ok;
                flight.pop_front();
            }
        }
        while (!flight.empty()) {
            ok = flight.front().get() && ok;
            flight.pop_front();
        }
        if (!ok && err.empty()) err = "damaged binary chart";
    }
    std::fclose(f);
    if (!ok) sn = StreamedNotes{};
    return ok;
}

bool convertChartToBinary(const std::string& src, const std::string& dst, std::string& err, ConvertStats* stats, int level) {
    const auto t0 = std::chrono::steady_clock::now();
    std::string skeleton;
    StreamedNotes sn;
    if (isBinaryChart(src)) {
        if (!readBinaryChart(src, skeleton, sn, err)) return false;
    } else if (isBigitChart(src)) {
        if (!readBigitChart(src, skeleton, sn, err)) return false;
    } else if (!streamChartSkeleton(src, skeleton, sn, err)) {
        return false;
    }
    if (!writeBinaryChart(dst, skeleton, sn, level, err)) return false;
    if (stats) {
        stats->inBytes = fileSize(src);
        stats->outBytes = fileSize(dst);
        stats->notes = sn.rawCount ? sn.rawCount : sn.notes.size();
        stats->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    return true;
}

}  // namespace gh
