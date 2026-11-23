// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// God's Hand bigit charts (.ghb); see bigit_chart.hpp.
//
//   "GHBG" u32 version
//   u64 notes, u64 rawCount, i32 maxData, u32 laneBits, u32 blockNotes, u32 blocks, u32 types
//   types: u32 length + bytes, each
//   u8 how the JSON part is kept (0 deflate, 1 order-1 bigits, 2 mixed bigits), u64 its bytes, u64 kept bytes, kept bytes
//   u64 values, u64 ranges, u64 kept bytes, kept bytes: the counts of each noteData value, then
//       the sections' note ranges (bigits)
//   blocks: u64 kept bytes, u8 how (0 the notes' model, 1 the columns' mixed models), u64 the columns' bytes
//       (1 only), each; then the blocks (bigits)
//
// The coder is LZMA's binary range coder: 11-bit chances that follow what
// they see (a 32nd of the way each time), and a 64-bit low end whose carries
// ripple into the bigits already written.
#include "bigit_chart.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "miniz.h"

namespace gh {

namespace bigit {

namespace {
constexpr int kBits = 11, kMove = 5;
constexpr uint32_t kOne = 1u << kBits, kTop = 1u << 24;
}  // namespace

void Encoder::bit(uint16_t& p, int b) {
    const uint32_t bound = (range_ >> kBits) * p;
    if (!b) {
        range_ = bound;
        p = uint16_t(p + ((kOne - p) >> kMove));
    } else {
        low_ += bound;
        range_ -= bound;
        p = uint16_t(p - (p >> kMove));
    }
    while (range_ < kTop) {
        range_ <<= 8;
        shiftLow();
    }
}

void Encoder::bitP(int p1, int b) {
    p1 = std::clamp(p1, 1, 4095);
    const uint32_t bound = (range_ >> 12) * uint32_t(4096 - p1);
    if (!b) {
        range_ = bound;
    } else {
        low_ += bound;
        range_ -= bound;
    }
    while (range_ < kTop) {
        range_ <<= 8;
        shiftLow();
    }
}

void Encoder::direct(uint64_t v, int n) {
    while (n-- > 0) {
        range_ >>= 1;
        if ((v >> n) & 1) low_ += range_;
        while (range_ < kTop) {
            range_ <<= 8;
            shiftLow();
        }
    }
}

void Encoder::shiftLow() {
    if (uint32_t(low_) < 0xFF000000u || (low_ >> 32) != 0) {
        const uint8_t carry = uint8_t(low_ >> 32);
        uint8_t temp = cache_;
        do {
            out_ += char(uint8_t(temp + carry));
            temp = 0xFF;
        } while (--cacheSize_ != 0);
        cache_ = uint8_t(uint32_t(low_) >> 24);
    }
    ++cacheSize_;
    low_ = (low_ & 0x00FFFFFFu) << 8;
}

void Encoder::finish() {
    for (int i = 0; i < 5; ++i) shiftLow();
}

Decoder::Decoder(const uint8_t* p, const uint8_t* end) : p_(p), end_(end) {
    for (int i = 0; i < 5; ++i) code_ = (code_ << 8) | next();
}

uint8_t Decoder::next() {
    if (p_ < end_) return *p_++;
    overrun_ = true;
    return 0;
}

int Decoder::bit(uint16_t& p) {
    const uint32_t bound = (range_ >> kBits) * p;
    int b;
    if (code_ < bound) {
        range_ = bound;
        p = uint16_t(p + ((kOne - p) >> kMove));
        b = 0;
    } else {
        code_ -= bound;
        range_ -= bound;
        p = uint16_t(p - (p >> kMove));
        b = 1;
    }
    while (range_ < kTop) {
        range_ <<= 8;
        code_ = (code_ << 8) | next();
    }
    return b;
}

int Decoder::bitP(int p1) {
    p1 = std::clamp(p1, 1, 4095);
    const uint32_t bound = (range_ >> 12) * uint32_t(4096 - p1);
    int b;
    if (code_ < bound) {
        range_ = bound;
        b = 0;
    } else {
        code_ -= bound;
        range_ -= bound;
        b = 1;
    }
    while (range_ < kTop) {
        range_ <<= 8;
        code_ = (code_ << 8) | next();
    }
    return b;
}

uint64_t Decoder::direct(int n) {
    uint64_t v = 0;
    while (n-- > 0) {
        range_ >>= 1;
        int b = 0;
        if (code_ >= range_) {
            code_ -= range_;
            b = 1;
        }
        v = (v << 1) | uint64_t(b);
        while (range_ < kTop) {
            range_ <<= 8;
            code_ = (code_ << 8) | next();
        }
    }
    return v;
}

}  // namespace bigit

namespace {

using bigit::Decoder;
using bigit::Encoder;
using bigit::kHalf;

constexpr uint32_t kMagic = 0x47424847u;  // "GHBG"
constexpr uint32_t kVersion = 1;
constexpr uint32_t kBlockNotes = 1u << 19;  // with its model: about 14 MB a block at work

uint64_t zig(int64_t v) { return (uint64_t(v) << 1) ^ uint64_t(v >> 63); }
int64_t unzig(uint64_t v) { return int64_t(v >> 1) ^ -int64_t(v & 1); }

// n-bit values, one tree of chances for each context.
struct Tree {
    int bits = 0;
    std::vector<uint16_t> p;
    void init(int nbits, size_t contexts) {
        bits = nbits;
        p.assign(contexts << nbits, kHalf);
    }
    void put(Encoder& e, size_t ctx, uint32_t v) {
        uint16_t* q = p.data() + (ctx << bits);
        uint32_t m = 1;
        for (int i = bits - 1; i >= 0; --i) {
            const int b = int((v >> i) & 1);
            e.bit(q[m], b);
            m = (m << 1) | uint32_t(b);
        }
    }
    uint32_t get(Decoder& d, size_t ctx) {
        uint16_t* q = p.data() + (ctx << bits);
        uint32_t m = 1;
        for (int i = 0; i < bits; ++i) m = (m << 1) | uint32_t(d.bit(q[m]));
        return m - (1u << bits);
    }
};

// Any 64-bit number: its length in bits (one chance each, learnt), the two
// bits under its leading one (learnt for each length), the rest plain.
struct Num {
    std::vector<uint16_t> len, top;
    void init(size_t contexts) {
        len.assign(contexts * 65, kHalf);
        top.assign(contexts * 65 * 4, kHalf);
    }
    void put(Encoder& e, size_t ctx, uint64_t v) {
        const int n = v ? 64 - std::countl_zero(v) : 0;
        uint16_t* l = len.data() + ctx * 65;
        for (int i = 0; i < n; ++i) e.bit(l[i], 1);
        if (n < 64) e.bit(l[n], 0);
        if (n <= 1) return;
        int rest = n - 1;
        const int k = std::min(rest, 2);
        uint16_t* t = top.data() + (ctx * 65 + size_t(n)) * 4;
        uint32_t m = 1;
        for (int i = 0; i < k; ++i) {
            const int b = int((v >> (rest - 1 - i)) & 1);
            e.bit(t[m], b);
            m = (m << 1) | uint32_t(b);
        }
        rest -= k;
        if (rest > 0) e.direct(v & ((uint64_t(1) << rest) - 1), rest);
    }
    uint64_t get(Decoder& d, size_t ctx) {
        uint16_t* l = len.data() + ctx * 65;
        int n = 0;
        while (n < 64 && d.bit(l[n])) ++n;
        if (n <= 1) return uint64_t(n);
        uint64_t v = 1;
        int rest = n - 1;
        const int k = std::min(rest, 2);
        uint16_t* t = top.data() + (ctx * 65 + size_t(n)) * 4;
        uint32_t m = 1;
        for (int i = 0; i < k; ++i) {
            const int b = d.bit(t[m]);
            m = (m << 1) | uint32_t(b);
            v = (v << 1) | uint64_t(b);
        }
        rest -= k;
        if (rest > 0) v = (v << rest) | d.direct(rest);
        return v;
    }
};

// A note as the models see it: its gap to the note before (ns), sustain
// (us), noteData, type and stack.
struct Tok {
    int64_t gap = 0, len = 0;
    uint16_t data = 0, ty = 0;
    uint8_t stack = 1;
    bool operator==(const Tok& o) const {
        return gap == o.gap && len == o.len && data == o.data && ty == o.ty && stack == o.stack;
    }
};

Tok tokOf(const StreamedNote& n, int64_t& prevT) {
    Tok t;
    const double tm = std::isfinite(n.time) ? n.time : 0.0;
    const int64_t ns = std::llround(tm * 1e6);  // as .ghc keeps them
    t.gap = ns - prevT;
    prevT = ns;
    t.len = std::llround(double(std::isfinite(n.length) ? n.length : 0.0f) * 1000.0);
    t.data = uint16_t(n.noteData());
    t.ty = uint16_t(n.type & (StreamedNote::kAltBit | StreamedNote::kTypeMask));
    t.stack = n.stack;
    return t;
}

void noteOf(const Tok& t, int64_t& prevT, StreamedNote& n) {
    prevT += t.gap;
    n.time = double(prevT) / 1e6;
    n.length = float(double(t.len) / 1000.0);
    n.stack = t.stack;
    n.type = t.ty;
    n.setNoteData(t.data);
}

// The notes' model: what each part of a note is likely to be, from the notes
// before it. The encoder and the decoder keep the same one, step by step.
class NoteModel {
public:
    explicit NoteModel(int laneBits) : laneBits_(std::max(1, laneBits)) {
        gapClass_.init(3, 7 * 7);
        gapNew_.init(1);
        lane_.init(laneBits_, laneBits_ <= 3 ? 64 * 3 : laneBits_ <= 6 ? (size_t(1) << laneBits_) * 3 : 3);
        lenNew_.init(1);
        stack_.init(8, 1);
        typeNew_.init(1);
        lenZero_.fill(kHalf);
        lenSame_.fill(kHalf);
        stackOne_.fill(kHalf);
        typeSame_.fill(kHalf);
    }

    void put(Encoder& e, const Tok& t) {
        // When: the gap to the note before, as one of a few likely kinds.
        const int c = classify(t.gap);
        gapClass_.put(e, size_t(prevClass_ * 7 + prevClass2_), uint32_t(c));
        if (c == 6) gapNew_.put(e, 0, zig(t.gap - lastGap_));
        // Where: the step from the lane before.
        const uint32_t delta = (uint32_t(t.data) - prevData_) & laneMask();
        lane_.put(e, laneContext(c), delta);
        // How long: no sustain, the same as the last one, or another.
        e.bit(lenZero_[lenContext(c)], t.len == 0);
        if (t.len != 0) {
            e.bit(lenSame_[prevLenSame_], t.len == lastLen_);
            if (t.len != lastLen_) lenNew_.put(e, 0, zig(t.len - lastLen_));
        }
        // How many notes of the file it stands for, and its type.
        e.bit(stackOne_[prevStack_ == 1], t.stack == 1);
        if (t.stack != 1) stack_.put(e, 0, t.stack);
        e.bit(typeSame_[prevTypeSame_], t.ty == prevType_);
        if (t.ty != prevType_) typeNew_.put(e, 0, t.ty);
        commit(t, c, delta);
    }

    bool get(Decoder& d, Tok& t) {
        const int c = int(gapClass_.get(d, size_t(prevClass_ * 7 + prevClass2_)));
        if (c == 0) t.gap = prevGap_;
        else if (c == 1) t.gap = 0;
        else if (c <= 5) {
            if (c - 2 >= mruN_) return false;
            t.gap = mru_[c - 2];
        } else if (c == 6) t.gap = lastGap_ + unzig(gapNew_.get(d, 0));
        else return false;
        const uint32_t delta = lane_.get(d, laneContext(c));
        const uint32_t data = (prevData_ + delta) & laneMask();
        if (data >= uint32_t(kNoteDataValues)) return false;
        t.data = uint16_t(data);
        t.len = 0;
        if (!d.bit(lenZero_[lenContext(c)])) {
            t.len = d.bit(lenSame_[prevLenSame_]) ? lastLen_ : lastLen_ + unzig(lenNew_.get(d, 0));
            if (t.len == 0) return false;  // "not zero" said otherwise
        }
        t.stack = 1;
        if (!d.bit(stackOne_[prevStack_ == 1])) t.stack = uint8_t(stack_.get(d, 0));
        t.ty = prevType_;
        if (!d.bit(typeSame_[prevTypeSame_])) {
            const uint64_t ty = typeNew_.get(d, 0);
            if (ty > 0xFFFF) return false;
            t.ty = uint16_t(ty & (StreamedNote::kAltBit | StreamedNote::kTypeMask));
        }
        commit(t, c, delta);
        return true;
    }

    // A note the match model gave: what the models remember moves on as if
    // it had been coded.
    void observe(const Tok& t) { commit(t, classify(t.gap), (uint32_t(t.data) - prevData_) & laneMask()); }

private:
    uint32_t laneMask() const { return (1u << laneBits_) - 1; }
    // 0 the gap before again, 1 none (a chord), 2-5 one of the last four
    // different gaps, 6 a new one (told as its difference from the last).
    int classify(int64_t gap) const {
        if (gap == prevGap_) return 0;
        if (gap == 0) return 1;
        for (int k = 0; k < mruN_; ++k)
            if (mru_[k] == gap) return 2 + k;
        return 6;
    }
    void commit(const Tok& t, int c, uint32_t delta) {
        if (t.gap != 0) {
            int at = -1;
            for (int k = 0; k < mruN_; ++k)
                if (mru_[k] == t.gap) at = k;
            if (at < 0) {
                at = std::min(mruN_, 3);
                if (mruN_ < 4) ++mruN_;
            }
            for (int k = at; k > 0; --k) mru_[k] = mru_[k - 1];
            mru_[0] = t.gap;
            lastGap_ = t.gap;
        }
        prevGap_ = t.gap;
        prevClass2_ = prevClass_;
        prevClass_ = c;
        prevDelta2_ = laneBits_ <= 3 ? prevDelta_ : 0;
        prevDelta_ = laneBits_ <= 6 ? delta : 0;
        prevData_ = t.data;
        if (t.len != 0) {
            prevLenSame_ = t.len == lastLen_;
            lastLen_ = t.len;
        }
        prevLen_ = t.len;
        prevStack_ = t.stack;
        prevTypeSame_ = t.ty == prevType_;
        prevType_ = t.ty;
    }
    static int gapKind(int c) { return c == 1 ? 1 : c == 0 ? 0 : 2; }
    size_t laneContext(int c) const {
        const int k = gapKind(c);
        if (laneBits_ <= 3) return (size_t(prevDelta_) * 8 + prevDelta2_) * 3 + size_t(k);
        if (laneBits_ <= 6) return size_t(prevDelta_) * 3 + size_t(k);
        return size_t(k);
    }
    size_t lenContext(int c) const { return (prevLen_ == 0 ? 2u : 0u) + (gapKind(c) == 1 ? 1u : 0u); }

    int laneBits_;
    Tree gapClass_, lane_, stack_;
    Num gapNew_, lenNew_, typeNew_;
    std::array<uint16_t, 4> lenZero_{};
    std::array<uint16_t, 2> lenSame_{}, stackOne_{}, typeSame_{};
    int64_t prevGap_ = 0, lastGap_ = 0;
    std::array<int64_t, 4> mru_{};
    int mruN_ = 0;
    int prevClass_ = 0, prevClass2_ = 0;
    uint32_t prevData_ = 0, prevDelta_ = 0, prevDelta2_ = 0;
    int64_t prevLen_ = 0, lastLen_ = 0;
    bool prevLenSame_ = true;
    uint8_t prevStack_ = 1;
    uint16_t prevType_ = 0;
    bool prevTypeSame_ = true;
};

// Charts repeat themselves (a pattern, a copied section): after the same
// six notes as before, the note that followed them then is likely again.
// One bit then says "the same" - a small fraction of a bit once a long run
// of them has come true - and the note costs nothing more.
class MatchModel {
public:
    static constexpr int kContext = 6;
    explicit MatchModel(size_t notes) : table_(size_t(1) << kHashBits, 0) { flag_.fill(kHalf); }
    // The note expected next (nullptr: none).
    const Tok* expected(const std::vector<Tok>& toks, size_t i) const {
        return ptr_ >= 0 && size_t(ptr_) < i ? &toks[size_t(ptr_)] : nullptr;
    }
    uint16_t& flag() { return flag_[size_t(std::min(len_, 15))]; }
    // Note i is known: the match goes on or ends, and a new one is looked for.
    void after(bool hit, const std::vector<Tok>& toks, size_t i) {
        if (hit) {
            ++ptr_;
            ++len_;
        } else {
            ptr_ = -1;
            len_ = 0;
        }
        if (i + 1 < size_t(kContext)) return;
        uint64_t h = 0;
        for (int k = 0; k < kContext; ++k) {
            const Tok& t = toks[i - size_t(k)];
            h = (h ^ (uint64_t(t.gap) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(t.len) << 17) ^ (uint64_t(t.data) << 3) ^
                 (uint64_t(t.ty) << 41) ^ (uint64_t(t.stack) << 57)) *
                0xD6E8FEB86659FD93ull;
            h ^= h >> 29;
        }
        uint32_t& slot = table_[size_t(h >> (64 - kHashBits))];
        if (ptr_ < 0 && slot) ptr_ = int64_t(slot) - 1;
        slot = uint32_t(i + 2);  // the note after this one, plus one (0: none)
    }

private:
    static constexpr int kHashBits = 19;
    std::vector<uint32_t> table_;
    std::array<uint16_t, 16> flag_{};
    int64_t ptr_ = -1;
    int len_ = 0;
};

std::string encodeNotes(const StreamedNote* n, size_t count, int laneBits) {
    std::vector<Tok> toks(count);
    int64_t prevT = 0;
    for (size_t i = 0; i < count; ++i) toks[i] = tokOf(n[i], prevT);
    std::string out;
    out.reserve(count / 4 + 16);
    Encoder e(out);
    NoteModel m(laneBits);
    MatchModel mm(count);
    for (size_t i = 0; i < count; ++i) {
        bool hit = false;
        if (const Tok* x = mm.expected(toks, i)) {
            hit = *x == toks[i];
            e.bit(mm.flag(), hit);
        }
        if (hit) m.observe(toks[i]);
        else m.put(e, toks[i]);
        mm.after(hit, toks, i);
    }
    e.finish();
    return out;
}

bool decodeNotes(const std::string& bytes, StreamedNote* out, size_t count, int laneBits) {
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.data());
    Decoder d(p, p + bytes.size());
    NoteModel m(laneBits);
    MatchModel mm(count);
    std::vector<Tok> toks(count);
    int64_t prevT = 0;
    for (size_t i = 0; i < count; ++i) {
        bool hit = false;
        if (const Tok* x = mm.expected(toks, i)) {
            hit = d.bit(mm.flag()) != 0;
            if (hit) {
                toks[i] = *x;
                m.observe(toks[i]);
            }
        }
        if (!hit && !m.get(d, toks[i])) return false;
        if (d.overrun()) return false;
        mm.after(hit, toks, i);
        noteOf(toks[i], prevT, out[i]);
    }
    return !d.overrun();
}

// The second way to code a block: the same five columns .ghc deflates
// (chart_binary.hpp), byte by byte, each bit's chance mixed from several
// models that learn as they go - the bytes before it (one, two and three of
// them, in the light of which column it is), the next byte of the last time
// the six bytes before it were seen, and a learnt bias - by weights that
// learn which of them to trust (as PAQ-style compressors do). Deflate finds
// the same repeats, but pays for each of them in whole bytes.
namespace cm {

int squash(int d) {  // 1 / (1 + e^-x), x = d / 256, as 12 bits
    static const int t[33] = {1,    2,    3,    6,    10,   16,   27,   45,   73,   120,  194,  310,  488,  747,  1101, 1546, 2047,
                              2549, 2994, 3348, 3607, 3785, 3901, 3975, 4024, 4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094};
    if (d > 2047) return 4095;
    if (d < -2047) return 1;
    const int w = d & 127;
    d = (d >> 7) + 16;
    return (t[d] * (128 - w) + t[d + 1] * w + 64) >> 7;
}

struct Stretch {
    std::array<int16_t, 4096> t{};
    Stretch() {
        int pi = 0;
        for (int x = -2047; x <= 2047; ++x) {
            const int v = squash(x);
            for (int j = pi; j <= v; ++j) t[size_t(j)] = int16_t(x);
            pi = v + 1;
        }
        for (int j = pi; j < 4096; ++j) t[size_t(j)] = 2047;
    }
};
inline int stretch(int p) {
    static const Stretch s;
    return s.t[size_t(std::clamp(p, 0, 4095))];
}

inline void learn(uint16_t& p, int y, int rate) {
    if (y) p = uint16_t(p + ((65535 - p) >> rate));
    else p = uint16_t(p - (p >> rate));
}

class Model {
public:
    static constexpr int kInputs = 7;
    // columns: the bytes are a .ghc block (its header says where each column
    // starts); otherwise plain bytes (the JSON part).
    Model(const std::array<uint64_t, 6>& colStart, bool columns) : colStart_(colStart), columns_(columns) {
        t0_.assign(6 * 256, 32768);
        t1_.assign(size_t(6) * 256 * 256, 32768);
        h2_.assign(size_t(1) << (kHashBits + 4), 32768);
        h3_.assign(size_t(1) << (kHashBits + 4), 32768);
        match_.assign(size_t(1) << kMatchBits, 0);
        conf_.fill(32768);
        w_.assign(size_t(kSets) * kInputs, 19661);  // 0.3 each to begin with
        byteStart();
    }
    // The chance that the next bit is 1, of 4096.
    int p() {
        const int k = bit_;
        const size_t nib = k < 4 ? size_t(c0_) : size_t((1u << (k - 4)) | (c0_ & ((1u << (k - 4)) - 1)));
        if (k == 0 || k == 4) {
            b2_ = bucket(h2v_);
            b3_ = bucket(h3v_);
        }
        i0_ = size_t(col_) * 256 + c0_;
        i1_ = (size_t(col_) * 256 + c1_) * 256 + c0_;
        i2_ = b2_ + nib;
        i3_ = b3_ + nib;
        int x[kInputs];
        x[0] = stretch(t0_[i0_] >> 4);
        x[1] = stretch(t1_[i1_] >> 4);
        x[2] = stretch(h2_[i2_] >> 4);
        x[3] = stretch(h3_[i3_] >> 4);
        // The match: the bit of the byte that came after the same bytes last time.
        expected_ = -1;
        if (len_ > 0 && ptr_ < hist_.size()) {
            const int pb = hist_[ptr_];
            if (((pb | 0x100) >> (8 - k)) == int(c0_)) expected_ = (pb >> (7 - k)) & 1;
        }
        ci_ = size_t(std::min(len_, 31));
        x[4] = expected_ < 0 ? 0 : (expected_ ? stretch(conf_[ci_] >> 4) : -stretch(conf_[ci_] >> 4));
        x[5] = 256;  // a bias
        x[6] = stretch(t1_[i1_] >> 4) / 2 + stretch(h2_[i2_] >> 4) / 2;
        set_ = size_t(col_) * 4 + (len_ == 0 ? 0 : len_ < 8 ? 1 : len_ < 24 ? 2 : 3);
        int64_t dot = 0;
        const int32_t* w = w_.data() + set_ * kInputs;
        for (int i = 0; i < kInputs; ++i) {
            x_[i] = x[i];
            dot += int64_t(x[i]) * w[i];
        }
        pr_ = squash(int(std::clamp<int64_t>(dot >> 16, -2047, 2047)));
        return pr_;
    }
    void update(int y) {
        const int err = ((y << 12) - pr_) * 7;
        int32_t* w = w_.data() + set_ * kInputs;
        for (int i = 0; i < kInputs; ++i) w[i] += (x_[i] * err) >> 10;
        learn(t0_[i0_], y, 5);
        learn(t1_[i1_], y, 4);
        learn(h2_[i2_], y, 4);
        learn(h3_[i3_], y, 4);
        if (expected_ >= 0) learn(conf_[ci_], y == expected_, 5);
        c0_ = (c0_ << 1) | uint32_t(y);
        if (++bit_ == 8) {
            byteDone(uint8_t(c0_ & 0xFF));
            byteStart();
        }
    }

private:
    static constexpr int kHashBits = 18;   // buckets of 16 chances: 4 MB a table
    static constexpr int kMatchBits = 18;
    static constexpr int kSets = 6 * 4;
    static uint32_t mix32(uint64_t h) {
        h ^= h >> 31;
        h *= 0x7FB5D329728EA185ull;
        h ^= h >> 27;
        h *= 0x81DADEF4BC2DD44Dull;
        return uint32_t(h >> 32);
    }
    size_t bucket(uint64_t h) const { return size_t(mix32(h + uint64_t(c0_) * 0x9E3779B97F4A7C15ull) >> (32 - kHashBits)) << 4; }
    void byteStart() {
        c0_ = 1;
        bit_ = 0;
        const uint64_t pos = hist_.size();
        while (col_ < 5 && pos >= colStart_[size_t(col_ + 1)]) ++col_;
        h2v_ = (uint64_t(col_) << 16 | c1_ << 8 | c2_) * 0x2545F4914F6CDD1Dull + 2;
        h3v_ = (uint64_t(col_) << 24 | c1_ << 16 | c2_ << 8 | c3_) * 0x9E3779B97F4A7C15ull + 3;
    }
    void byteDone(uint8_t b) {
        const size_t pos = hist_.size();
        hist_.push_back(b);
        if (columns_ && pos == 19) {
            // The header is known: where each column starts.
            uint64_t at = 20;
            for (int c = 0; c < 5; ++c) {
                colStart_[size_t(c + 1)] = at;
                at += uint64_t(hist_[size_t(4 * c)]) | uint64_t(hist_[size_t(4 * c + 1)]) << 8 | uint64_t(hist_[size_t(4 * c + 2)]) << 16 |
                      uint64_t(hist_[size_t(4 * c + 3)]) << 24;
            }
        }
        c3_ = c2_;
        c2_ = c1_;
        c1_ = b;
        // The match goes on, or another is looked for after the last six bytes.
        if (len_ > 0 && ptr_ < pos && hist_[ptr_] == b) {
            ++len_;
            ++ptr_;
        } else {
            len_ = 0;
        }
        if (pos + 1 >= 6) {
            uint64_t h = 0;
            for (size_t i = pos + 1 - 6; i <= pos; ++i) h = (h + hist_[i] + 1) * 0x100000001B3ull;
            uint32_t& slot = match_[mix32(h) >> (32 - kMatchBits)];
            if (len_ == 0 && slot) {
                const size_t cand = slot;  // the byte after an earlier place with the same six
                int l = 0;
                while (l < 32 && cand >= size_t(l) + 1 && hist_[cand - 1 - size_t(l)] == hist_[pos - size_t(l)]) ++l;
                if (l >= 6) {
                    ptr_ = cand;
                    len_ = l;
                }
            }
            slot = uint32_t(pos + 1);
        }
    }

    std::array<uint64_t, 6> colStart_;
    bool columns_ = true;
    std::vector<uint16_t> t0_, t1_, h2_, h3_;
    std::vector<uint32_t> match_;
    std::vector<uint8_t> hist_;
    std::array<uint16_t, 32> conf_{};
    std::vector<int32_t> w_;
    int x_[kInputs] = {};
    uint32_t c0_ = 1, c1_ = 0, c2_ = 0, c3_ = 0;
    int bit_ = 0, col_ = 0, len_ = 0, expected_ = -1, pr_ = 2048;
    size_t ptr_ = 0, i0_ = 0, i1_ = 0, i2_ = 0, i3_ = 0, b2_ = 0, b3_ = 0, ci_ = 0, set_ = 0;
    uint64_t h2v_ = 0, h3v_ = 0;
};

std::array<uint64_t, 6> unknownColumns() {
    std::array<uint64_t, 6> c{};
    c[0] = 0;
    for (size_t i = 1; i < 6; ++i) c[i] = UINT64_MAX;  // until the header says
    return c;
}

std::string encode(const std::string& raw, bool columns = true) {
    std::string out;
    out.reserve(raw.size() / 3 + 16);
    Encoder e(out);
    Model m(unknownColumns(), columns);
    for (const char ch : raw) {
        const int c = uint8_t(ch);
        for (int k = 7; k >= 0; --k) {
            const int y = (c >> k) & 1;
            e.bitP(m.p(), y);
            m.update(y);
        }
    }
    e.finish();
    return out;
}

bool decode(const std::string& bytes, size_t rawLen, std::string& raw, bool columns = true) {
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.data());
    Decoder d(p, p + bytes.size());
    Model m(unknownColumns(), columns);
    raw.resize(rawLen);
    for (size_t i = 0; i < rawLen; ++i) {
        int c = 0;
        for (int k = 0; k < 8; ++k) {
            const int y = d.bitP(m.p());
            m.update(y);
            c = (c << 1) | y;
        }
        raw[i] = char(c);
        if (d.overrun()) return false;
    }
    return true;
}

}  // namespace cm

// Text by an order-1 model (each byte from the one before it).
std::string encodeText(const std::string& s) {
    std::string out;
    Encoder e(out);
    Tree t;
    t.init(8, 256);
    uint8_t prev = 0;
    for (char ch : s) {
        const uint8_t c = uint8_t(ch);
        t.put(e, prev, c);
        prev = c;
    }
    e.finish();
    return out;
}
bool decodeText(const std::string& bytes, size_t len, std::string& s) {
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.data());
    Decoder d(p, p + bytes.size());
    Tree t;
    t.init(8, 256);
    s.resize(len);
    uint8_t prev = 0;
    for (size_t i = 0; i < len; ++i) {
        prev = uint8_t(t.get(d, prev));
        s[i] = char(prev);
    }
    return !d.overrun();
}

// The counts of each noteData value and the sections' ranges.
std::string encodeTables(const std::array<uint64_t, kNoteDataValues>& counts, size_t values, const std::vector<uint32_t>& ranges) {
    std::string out;
    Encoder e(out);
    Num num;
    num.init(2);
    std::array<uint16_t, 2> same{kHalf, kHalf};
    for (size_t i = 0; i < values; ++i) num.put(e, 0, counts[i]);
    uint32_t prev = 0, prevGap = 0;
    bool wasSame = true;
    for (uint32_t r : ranges) {
        const uint32_t gap = r - prev;
        e.bit(same[wasSame], gap == prevGap);
        if (gap != prevGap) num.put(e, 1, gap);
        wasSame = gap == prevGap;
        prevGap = gap;
        prev = r;
    }
    e.finish();
    return out;
}
bool decodeTables(const std::string& bytes, std::array<uint64_t, kNoteDataValues>& counts, size_t values, std::vector<uint32_t>& ranges) {
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.data());
    Decoder d(p, p + bytes.size());
    Num num;
    num.init(2);
    std::array<uint16_t, 2> same{kHalf, kHalf};
    counts.fill(0);
    for (size_t i = 0; i < values; ++i) counts[i] = num.get(d, 0);
    uint64_t at = 0;
    uint32_t prevGap = 0;
    bool wasSame = true;
    for (uint32_t& r : ranges) {
        uint64_t gap = prevGap;
        if (!d.bit(same[wasSame])) gap = num.get(d, 1);
        if (gap > 0xFFFFFFFFu) return false;
        wasSame = gap == prevGap;
        prevGap = uint32_t(gap);
        at += gap;
        if (at > 0xFFFFFFFFu) return false;
        r = uint32_t(at);
    }
    return !d.overrun();
}

void putU32(std::string& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o += char((v >> (8 * i)) & 0xFF);
}
void putU64(std::string& o, uint64_t v) {
    for (int i = 0; i < 8; ++i) o += char((v >> (8 * i)) & 0xFF);
}
bool readAll(FILE* f, void* dst, size_t n) { return n == 0 || std::fread(dst, 1, n, f) == n; }
bool readU32(FILE* f, uint32_t& v) {
    uint8_t b[4];
    if (!readAll(f, b, 4)) return false;
    v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
    return true;
}
bool readU64(FILE* f, uint64_t& v) {
    uint8_t b[8];
    if (!readAll(f, b, 8)) return false;
    v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | b[i];
    return true;
}
bool readBytes(FILE* f, std::string& s, uint64_t n) {
    if (n > (uint64_t(1) << 34)) return false;
    s.assign(size_t(n), '\0');
    return readAll(f, s.data(), s.size());
}

int workerCount(int threads) {
    if (threads > 0) return threads;
    return std::max(1, int(std::thread::hardware_concurrency()));
}

int laneBitsFor(const StreamedNotes& sn) {
    int hi = 0;
    for (const StreamedNote& n : sn.notes) hi = std::max(hi, n.noteData());
    return std::max(1, int(std::bit_width(unsigned(hi))));
}

struct Head {
    uint64_t notes = 0, rawCount = 0;
    int32_t maxData = -1;
    uint32_t laneBits = 1, blockNotes = 0, blocks = 0, types = 0;
};

bool readHead(FILE* f, Head& h, std::string& skeleton, StreamedNotes* sn, std::string& err) {
    uint32_t magic = 0, version = 0, maxData = 0;
    if (!readU32(f, magic) || magic != kMagic) {
        err = "not a God's Hand bigit chart";
        return false;
    }
    if (!readU32(f, version) || version < 1 || version > kVersion) {
        err = "a bigit chart of a newer God's Hand";
        return false;
    }
    if (!readU64(f, h.notes) || !readU64(f, h.rawCount) || !readU32(f, maxData) || !readU32(f, h.laneBits) ||
        !readU32(f, h.blockNotes) || !readU32(f, h.blocks) || !readU32(f, h.types) || h.blockNotes == 0 || h.laneBits < 1 ||
        h.laneBits > 11 || uint64_t(h.blocks) != (h.notes + h.blockNotes - 1) / h.blockNotes) {
        err = "damaged bigit chart";
        return false;
    }
    h.maxData = int32_t(maxData);
    std::vector<std::string> types(h.types);
    for (std::string& t : types) {
        uint32_t len = 0;
        if (!readU32(f, len) || len > (1u << 20) || !readBytes(f, t, len)) {
            err = "damaged bigit chart";
            return false;
        }
    }
    uint8_t codec = 0;
    uint64_t raw = 0, kept = 0;
    std::string buf;
    if (!readAll(f, &codec, 1) || !readU64(f, raw) || !readU64(f, kept) || raw > (uint64_t(1) << 34) || !readBytes(f, buf, kept)) {
        err = "damaged bigit chart";
        return false;
    }
    bool ok;
    if (codec == 0) {
        skeleton.resize(size_t(raw));
        mz_ulong m = mz_ulong(raw);
        ok = raw == 0 || (mz_uncompress(reinterpret_cast<unsigned char*>(skeleton.data()), &m,
                                        reinterpret_cast<const unsigned char*>(buf.data()), mz_ulong(buf.size())) == MZ_OK &&
                          m == raw);
    } else if (codec == 1) {
        ok = decodeText(buf, size_t(raw), skeleton);
    } else {
        ok = codec == 2 && cm::decode(buf, size_t(raw), skeleton, false);
    }
    if (!ok) {
        err = "damaged bigit chart";
        return false;
    }
    if (!sn) return true;
    uint64_t values = 0, ranges = 0;
    if (!readU64(f, values) || !readU64(f, ranges) || values > uint64_t(kNoteDataValues) || ranges > (uint64_t(1) << 32) ||
        !readU64(f, kept) || !readBytes(f, buf, kept)) {
        err = "damaged bigit chart";
        return false;
    }
    sn->types = std::move(types);
    sn->maxData = h.maxData;
    sn->rawCount = h.rawCount;
    sn->rangeStart.resize(size_t(ranges));
    if (!decodeTables(buf, sn->dataCount, size_t(values), sn->rangeStart)) {
        err = "damaged bigit chart";
        return false;
    }
    return true;
}

}  // namespace

bool isBigitChart(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t m = 0;
    const bool ok = readU32(f, m) && m == kMagic;
    std::fclose(f);
    return ok;
}

bool writeBigitChart(const std::string& path, const std::string& skeleton, const StreamedNotes& sn, std::string& err) {
    const size_t n = sn.notes.size();
    const uint32_t blocks = uint32_t((n + kBlockNotes - 1) / kBlockNotes);
    const int laneBits = laneBitsFor(sn);
    // Each block both ways - the notes' model, and the columns' mixed
    // models - and the smaller kept.
    std::vector<std::string> coded(blocks);
    std::vector<uint8_t> codec(blocks, 0);
    std::vector<uint64_t> rawLen(blocks, 0);
    {
        const int workers = workerCount(-1);
        std::vector<std::future<void>> jobs;
        for (int w = 0; w < workers; ++w)
            jobs.push_back(std::async(std::launch::async, [&, w] {
                for (uint32_t b = uint32_t(w); b < blocks; b += uint32_t(workers)) {
                    const size_t from = size_t(b) * kBlockNotes, cnt = std::min<size_t>(kBlockNotes, n - from);
                    coded[b] = encodeNotes(sn.notes.data() + from, cnt, laneBits);
                    const std::string raw = ghcColumns(sn.notes.data() + from, cnt);
                    std::string cols = cm::encode(raw);
                    if (cols.size() < coded[b].size()) {
                        coded[b] = std::move(cols);
                        codec[b] = 1;
                        rawLen[b] = raw.size();
                    }
                }
            }));
        for (auto& j : jobs) j.get();
    }
    std::string head;
    putU32(head, kMagic);
    putU32(head, kVersion);
    putU64(head, n);
    putU64(head, sn.rawCount);
    putU32(head, uint32_t(int32_t(sn.maxData)));
    putU32(head, uint32_t(laneBits));
    putU32(head, kBlockNotes);
    putU32(head, blocks);
    putU32(head, uint32_t(sn.types.size()));
    for (const std::string& t : sn.types) {
        putU32(head, uint32_t(t.size()));
        head += t;
    }
    // The JSON part: deflated, or by the order-1 model, whichever is smaller.
    std::string deflated;
    {
        mz_ulong m = mz_compressBound(mz_ulong(skeleton.size()));
        deflated.resize(size_t(m));
        if (mz_compress2(reinterpret_cast<unsigned char*>(deflated.data()), &m, reinterpret_cast<const unsigned char*>(skeleton.data()),
                         mz_ulong(skeleton.size()), 9) != MZ_OK) {
            err = "could not pack the chart's data";
            return false;
        }
        deflated.resize(size_t(m));
    }
    const std::string modelled = encodeText(skeleton), mixed = cm::encode(skeleton, false);
    // 0 deflate, 1 order-1, 2 the mixed models: the smallest.
    const std::string* kept = &deflated;
    char how = 0;
    if (modelled.size() < kept->size()) {
        kept = &modelled;
        how = 1;
    }
    if (mixed.size() < kept->size()) {
        kept = &mixed;
        how = 2;
    }
    head += how;
    putU64(head, skeleton.size());
    putU64(head, kept->size());
    head += *kept;
    size_t values = sn.dataCount.size();
    while (values > 0 && sn.dataCount[values - 1] == 0) --values;
    const std::string tables = encodeTables(sn.dataCount, values, sn.rangeStart);
    putU64(head, values);
    putU64(head, sn.rangeStart.size());
    putU64(head, tables.size());
    head += tables;
    for (uint32_t b = 0; b < blocks; ++b) {
        putU64(head, coded[b].size());
        head += char(codec[b]);
        putU64(head, rawLen[b]);
    }
    if (std::getenv("GH_BIGIT_DEBUG")) {
        size_t notesBytes = 0;
        for (const std::string& c : coded) notesBytes += c.size();
        int byColumns = 0;
        for (uint8_t c : codec) byColumns += c;
        std::fprintf(stderr, "[ghb] notes %zu: %zu bytes (%.3f bits a note, %d of %u blocks by columns); json %zu -> deflate %zu / order-1 %zu / mixed %zu; tables %zu\n",
                     n, notesBytes, n ? 8.0 * double(notesBytes) / double(n) : 0.0, byColumns, blocks, skeleton.size(), deflated.size(),
                     modelled.size(), mixed.size(), tables.size());
    }
    const std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        err = "cannot write " + path;
        return false;
    }
    bool ok = std::fwrite(head.data(), 1, head.size(), f) == head.size();
    for (uint32_t b = 0; ok && b < blocks; ++b) ok = std::fwrite(coded[b].data(), 1, coded[b].size(), f) == coded[b].size();
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

bool readBigitSkeleton(const std::string& path, std::string& skeleton) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    Head h;
    std::string err;
    const bool ok = readHead(f, h, skeleton, nullptr, err);
    std::fclose(f);
    return ok;
}

bool readBigitChart(const std::string& path, std::string& skeleton, StreamedNotes& sn, std::string& err, int threads) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "cannot read " + path;
        return false;
    }
    Head h;
    sn = StreamedNotes{};
    bool ok = readHead(f, h, skeleton, &sn, err);
    std::vector<uint64_t> lens(ok ? h.blocks : 0), rawLens(ok ? h.blocks : 0);
    std::vector<uint8_t> codecs(ok ? h.blocks : 0);
    for (uint32_t b = 0; ok && b < h.blocks; ++b)
        ok = readU64(f, lens[b]) && lens[b] <= (uint64_t(1) << 34) && readAll(f, &codecs[b], 1) && codecs[b] <= 1 &&
             readU64(f, rawLens[b]) && rawLens[b] <= (uint64_t(1) << 34);
    if (ok) {
        sn.notes.resize(size_t(h.notes));
        // Read in order, each block's number taken apart on a core of its own.
        const size_t window = size_t(workerCount(threads));
        std::deque<std::future<bool>> flight;
        const int laneBits = int(h.laneBits);
        for (uint32_t b = 0; ok && b < h.blocks; ++b) {
            auto bytes = std::make_shared<std::string>();
            if (!readBytes(f, *bytes, lens[b])) {
                ok = false;
                break;
            }
            const size_t from = size_t(b) * h.blockNotes, cnt = std::min<size_t>(h.blockNotes, size_t(h.notes) - from);
            StreamedNote* dst = sn.notes.data() + from;
            const uint8_t codec = codecs[b];
            const uint64_t rawLen = rawLens[b];
            flight.push_back(std::async(std::launch::async, [bytes, dst, cnt, laneBits, codec, rawLen] {
                if (codec == 0) return decodeNotes(*bytes, dst, cnt, laneBits);
                std::string raw;
                return cm::decode(*bytes, size_t(rawLen), raw) && ghcFromColumns(raw, dst, cnt);
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
        if (!ok && err.empty()) err = "damaged bigit chart";
    }
    std::fclose(f);
    if (!ok) sn = StreamedNotes{};
    return ok;
}

bool convertChartToBigit(const std::string& src, const std::string& dst, std::string& err, ConvertStats* stats) {
    const auto t0 = std::chrono::steady_clock::now();
    std::string skeleton;
    StreamedNotes sn;
    if (isBigitChart(src)) {
        if (!readBigitChart(src, skeleton, sn, err)) return false;
    } else if (isBinaryChart(src)) {
        if (!readBinaryChart(src, skeleton, sn, err)) return false;
    } else if (!streamChartSkeleton(src, skeleton, sn, err)) {
        return false;
    }
    if (!writeBigitChart(dst, skeleton, sn, err)) return false;
    if (stats) {
        stats->inBytes = fileSize(src);
        stats->outBytes = fileSize(dst);
        stats->notes = sn.rawCount ? sn.rawCount : sn.notes.size();
        stats->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    return true;
}

}  // namespace gh
