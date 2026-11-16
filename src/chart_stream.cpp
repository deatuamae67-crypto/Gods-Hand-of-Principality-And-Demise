// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Loading chart files of any size. Small charts go through the JSON DOM.
// Large legacy/Psych charts (spam charts reach 1.5 GB and tens of millions of
// notes) are streamed: every "sectionNotes" array is read straight into a
// compact note buffer, and only the rest of the file (a "skeleton" with a
// marker where each note array was) goes through the DOM. Notes stacked in
// the same lane within 1 ms are merged while streaming, as finalize() would,
// so memory stays proportional to the playable notes.
#include <algorithm>
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
#elif defined(__aarch64__) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>
#endif
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>

#include "chart.hpp"
#include "chart_stream.hpp"

#include "chart_binary.hpp"
#include "common.hpp"
#include "flp_import.hpp"

namespace gh {

namespace {

constexpr size_t kStreamThreshold = 8u << 20;  // bytes: from here streaming is faster than the DOM

// Reads the file on a thread of its own, a few blocks ahead of the parser, so
// the disk (or a decompressor writing into a pipe) and the parsing overlap.
class ReadAhead {
public:
    explicit ReadAhead(FILE* f) : f_(f), th_([this] { run(); }) {}
    ~ReadAhead() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        th_.join();
    }
    // As fread: up to n bytes, fewer only at the end of the file.
    size_t read(char* dst, size_t n) {
        size_t done = 0;
        std::unique_lock<std::mutex> lk(m_);
        while (done < n) {
            cv_.wait(lk, [&] { return !q_.empty() || end_; });
            if (q_.empty()) break;  // the end of the file
            std::vector<char>& b = q_.front();
            const size_t take = std::min(n - done, b.size() - pos_);
            std::memcpy(dst + done, b.data() + pos_, take);
            done += take;
            pos_ += take;
            if (pos_ == b.size()) {
                q_.pop_front();
                pos_ = 0;
                cv_.notify_all();  // room for the reader thread
            }
        }
        return done;
    }

private:
    static constexpr size_t kBlock = 4u << 20, kAhead = 4;
    void run() {
        for (;;) {
            std::vector<char> b(kBlock);
            b.resize(std::fread(b.data(), 1, kBlock, f_));
            std::unique_lock<std::mutex> lk(m_);
            if (b.empty()) {
                end_ = true;
                cv_.notify_all();
                return;
            }
            cv_.wait(lk, [&] { return q_.size() < kAhead || stop_; });
            if (stop_) return;
            q_.push_back(std::move(b));
            cv_.notify_all();
        }
    }
    FILE* f_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::vector<char>> q_;
    size_t pos_ = 0;
    bool end_ = false, stop_ = false;
    std::thread th_;  // last: starts once the rest is ready
};

class Reader {
public:
    explicit Reader(FILE* f) : f_(f), buf_(4u << 20) {
        if (f_) ra_ = std::make_unique<ReadAhead>(f_);
        refill();
    }
    // Over text already in memory (a section's notes, parsed by a worker).
    Reader(const char* data, size_t n) : f_(nullptr), p_(data), end_(data + n) {}
    // The bytes readable now without reading more of the file; fill(): reads
    // more when none are left (false at the end of the file).
    size_t avail() const { return size_t(end_ - p_); }
    bool fill() { return p_ < end_ || refill(); }
    bool eof() { return p_ == end_ && !refill(); }
    int peek() { return (p_ < end_ || refill()) ? (unsigned char)*p_ : -1; }
    int get() { return (p_ < end_ || refill()) ? (unsigned char)*p_++ : -1; }
    void skipWs() {
        for (;;) {
            while (p_ < end_) {
                const char c = *p_;
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { ++p_; continue; }
                if (c == '/' ) { skipComment(); continue; }
                return;
            }
            if (!refill()) return;
        }
    }
    uint64_t consumed() const { return total_ - uint64_t(end_ - p_); }
    // Makes at least n bytes readable from data() when the file has them.
    size_t ensure(size_t n) {
        size_t have = size_t(end_ - p_);
        if (have >= n || !f_) return have;
        std::memmove(buf_.data(), p_, have);
        p_ = buf_.data();
        end_ = p_ + have;
        const size_t got = ra_->read(buf_.data() + have, buf_.size() - have);
        end_ += got;
        total_ += got;
        return size_t(end_ - p_);
    }
    const char* data() const { return p_; }
    void advance(size_t n) { p_ += n; }

private:
    void skipComment() {
        ++p_;
        const int c = get();
        if (c == '/') {
            for (int ch = get(); ch != -1 && ch != '\n'; ch = get()) {}
        } else if (c == '*') {
            int prev = 0;
            for (int ch = get(); ch != -1; prev = ch, ch = get())
                if (prev == '*' && ch == '/') break;
        }
    }
    bool refill() {
        if (!f_) return false;
        if (p_ < end_) return true;
        const size_t n = ra_->read(buf_.data(), buf_.size());
        p_ = buf_.data();
        end_ = p_ + n;
        total_ += n;
        return n > 0;
    }
    FILE* f_;
    std::unique_ptr<ReadAhead> ra_;
    std::vector<char> buf_;
    const char* p_ = nullptr;
    const char* end_ = nullptr;
    uint64_t total_ = 0;
};

const double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                         1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

// ---- finding JSON's structural bytes, 16 at a time (SSE2 / NEON) ----

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#define GH_SCAN_SSE2 1
#elif defined(__aarch64__) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
#define GH_SCAN_NEON 1
#endif

inline unsigned lowestBit(uint64_t m) {
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long i;
    _BitScanForward64(&i, m);
    return unsigned(i);
#else
    return unsigned(__builtin_ctzll(m));
#endif
}

// Where JSON's structural bytes are in 64 bytes at once: bit i is set when
// p[i] is one of [ ] { } " \\ / (and , when asked), as simdjson finds them.
class StructScan {
public:
    StructScan() {
#if GH_SCAN_SSE2
        const char set[8] = {'[', ']', '{', '}', '"', '\\', '/', ','};
        for (int k = 0; k < 8; ++k) b_[k] = _mm_set1_epi8(set[k]);
#elif GH_SCAN_NEON
        const char set[8] = {'[', ']', '{', '}', '"', '\\', '/', ','};
        for (int k = 0; k < 8; ++k) b_[k] = vdupq_n_u8(uint8_t(set[k]));
        static const uint8_t bits[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
        bits_ = vld1q_u8(bits);
#endif
    }
    uint64_t mask(const char* p, bool commas) const {
#if GH_SCAN_SSE2
        uint64_t m = 0;
        for (int q = 0; q < 4; ++q) {
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16 * q));
            __m128i e = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, b_[0]), _mm_cmpeq_epi8(v, b_[1])),
                                     _mm_or_si128(_mm_cmpeq_epi8(v, b_[2]), _mm_cmpeq_epi8(v, b_[3])));
            e = _mm_or_si128(e, _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, b_[4]), _mm_cmpeq_epi8(v, b_[5])),
                                             _mm_cmpeq_epi8(v, b_[6])));
            if (commas) e = _mm_or_si128(e, _mm_cmpeq_epi8(v, b_[7]));
            m |= uint64_t(unsigned(_mm_movemask_epi8(e))) << (16 * q);
        }
        return m;
#elif GH_SCAN_NEON
        uint8x16_t t[4];
        for (int q = 0; q < 4; ++q) {
            const uint8x16_t v = vld1q_u8(reinterpret_cast<const uint8_t*>(p + 16 * q));
            uint8x16_t e = vorrq_u8(vorrq_u8(vceqq_u8(v, b_[0]), vceqq_u8(v, b_[1])), vorrq_u8(vceqq_u8(v, b_[2]), vceqq_u8(v, b_[3])));
            e = vorrq_u8(e, vorrq_u8(vorrq_u8(vceqq_u8(v, b_[4]), vceqq_u8(v, b_[5])), vceqq_u8(v, b_[6])));
            if (commas) e = vorrq_u8(e, vceqq_u8(v, b_[7]));
            t[q] = vandq_u8(e, bits_);
        }
        uint8x16_t sum = vpaddq_u8(vpaddq_u8(t[0], t[1]), vpaddq_u8(t[2], t[3]));
        sum = vpaddq_u8(sum, sum);
        return vgetq_lane_u64(vreinterpretq_u64_u8(sum), 0);
#else
        uint64_t m = 0;
        for (int i = 0; i < 64; ++i) {
            const char c = p[i];
            if (c == '[' || c == ']' || c == '{' || c == '}' || c == '"' || c == '\\' || c == '/' || (commas && c == ','))
                m |= uint64_t(1) << i;
        }
        return m;
#endif
    }

private:
#if GH_SCAN_SSE2
    __m128i b_[8];
#elif GH_SCAN_NEON
    uint8x16_t b_[8], bits_;
#endif
};

// Worker threads for the notes of whole sections.
class SectionPool {
public:
    explicit SectionPool(int n) {
        for (int i = 0; i < n; ++i) threads_.emplace_back([this] { run(); });
    }
    ~SectionPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (std::thread& t : threads_) t.join();
    }
    int size() const { return int(threads_.size()); }
    void post(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lk(m_);
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
    }

private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stop_ || !jobs_.empty(); });
                if (jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
        }
    }
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    bool stop_ = false;
};

// The notes of one section, parsed by a worker: merged into the chart's in order.
struct SectionResult {
    StreamedNotes notes;  // types numbered within the section
    std::string kept;     // its elements that are not plain notes
    bool keptAny = false, ok = true;
    std::string err;
};

class Streamer {
public:
    Streamer(Reader& r, StreamedNotes& out) : r_(r), out_(out) {}
    // Parallel: sections are copied out (at most `cap` bytes each) and parsed
    // on the pool's threads, then merged in order; bigger sections are parsed
    // here, piece by piece. The result is exactly the sequential one.
    void setPool(SectionPool* pool, size_t cap) {
        pool_ = pool;
        cap_ = std::max<size_t>(cap, 64);
    }

    bool run(std::string& skeleton, std::string& err) {
        r_.skipWs();
        bool ok = value(skeleton, std::string_view(), 0);
        ok = drainAll() && ok;
        if (!ok) {
            err = err_.empty() ? "invalid JSON" : err_;
            return false;
        }
        if (!keptText_.empty()) finishSkeleton(skeleton);
        return true;
    }

    // The elements of a section held in memory: "e1, e2, ..." (with or
    // without a trailing comma), no brackets around them.
    bool chunkElements(std::vector<double>& lastT, std::string& kept, bool& keptAny) {
        for (;;) {
            r_.skipWs();
            if (r_.eof()) return true;
            if (!noteElement(kept, keptAny, lastT)) return false;
            r_.skipWs();
            if (r_.eof()) return true;
            if (r_.get() != ',') return fail("expected , in sectionNotes");
        }
    }
    const std::string& error() const { return err_; }

private:
    bool fail(const char* m) {
        if (err_.empty()) err_ = std::string(m) + " near byte " + std::to_string(r_.consumed());
        return false;
    }

    // Copies a string token (with its quotes) to out; also returns its text.
    bool string(std::string& out, std::string* text) {
        if (r_.get() != '"') return fail("expected a string");
        out += '"';
        for (;;) {
            const int c = r_.get();
            if (c == -1) return fail("unterminated string");
            out += char(c);
            if (c == '"') return true;
            if (c == '\\') {
                const int e = r_.get();
                if (e == -1) return fail("unterminated string");
                out += char(e);
                if (text) text->push_back(e == 'n' ? '\n' : e == 't' ? '\t' : char(e));
                continue;
            }
            if (text) text->push_back(char(c));
        }
    }

    bool number(std::string& out) {
        for (int c = r_.peek(); c != -1; c = r_.peek()) {
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
                out += char(r_.get());
            } else {
                break;
            }
        }
        return true;
    }

    // Fast number parse for note arrays; good to well under a microsecond of
    // error, which is all chart times need.
    bool fastNumber(double& v) {
        bool neg = false;
        int c = r_.peek();
        if (c == '-') { neg = true; r_.get(); c = r_.peek(); }
        else if (c == '+') { r_.get(); c = r_.peek(); }
        uint64_t mant = 0;
        int digits = 0, exp10 = 0;
        bool any = false;
        while (c >= '0' && c <= '9') {
            if (digits < 19) { mant = mant * 10 + uint64_t(c - '0'); ++digits; }
            else ++exp10;
            any = true;
            r_.get();
            c = r_.peek();
        }
        if (c == '.') {
            r_.get();
            c = r_.peek();
            while (c >= '0' && c <= '9') {
                if (digits < 19) { mant = mant * 10 + uint64_t(c - '0'); ++digits; --exp10; }
                any = true;
                r_.get();
                c = r_.peek();
            }
        }
        if (c == 'e' || c == 'E') {
            r_.get();
            c = r_.peek();
            bool eneg = false;
            if (c == '-' || c == '+') { eneg = c == '-'; r_.get(); c = r_.peek(); }
            int e = 0;
            while (c >= '0' && c <= '9') { e = std::min(e * 10 + (c - '0'), 1000); r_.get(); c = r_.peek(); }
            exp10 += eneg ? -e : e;
        }
        if (!any) {
            // null / true / false / NaN in a note array: read the word.
            std::string w;
            while (c != -1 && std::isalpha(c)) { w += char(r_.get()); c = r_.peek(); }
            if (w == "true") { v = 1; return true; }
            if (w == "false" || w == "null") { v = 0; return true; }
            v = std::nan("");
            return !w.empty() || fail("bad number");
        }
        double d = double(mant);
        if (exp10 > 0) d *= exp10 <= 22 ? kPow10[exp10] : std::pow(10.0, exp10);
        else if (exp10 < 0) d /= -exp10 <= 22 ? kPow10[-exp10] : std::pow(10.0, -exp10);
        v = neg ? -d : d;
        return true;
    }

    bool word(std::string& out) {
        for (int c = r_.peek(); c != -1 && std::isalpha(c); c = r_.peek()) out += char(r_.get());
        return true;
    }

    static bool isWs(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

    static bool numAt(const char*& p, const char* e, double& v) {
        bool neg = false;
        if (p < e && *p == '-') { neg = true; ++p; }
        uint64_t mant = 0;
        int digits = 0, exp10 = 0;
        bool any = false;
        while (p < e && *p >= '0' && *p <= '9') {
            if (digits < 19) { mant = mant * 10 + uint64_t(*p - '0'); ++digits; } else ++exp10;
            any = true;
            ++p;
        }
        if (p < e && *p == '.') {
            ++p;
            while (p < e && *p >= '0' && *p <= '9') {
                if (digits < 19) { mant = mant * 10 + uint64_t(*p - '0'); ++digits; --exp10; }
                any = true;
                ++p;
            }
        }
        if (!any) return false;
        if (p < e && (*p == 'e' || *p == 'E')) {
            ++p;
            bool eneg = false;
            if (p < e && (*p == '-' || *p == '+')) { eneg = *p == '-'; ++p; }
            int x = 0;
            bool anyE = false;
            while (p < e && *p >= '0' && *p <= '9') { x = std::min(x * 10 + (*p - '0'), 1000); ++p; anyE = true; }
            if (!anyE) return false;
            exp10 += eneg ? -x : x;
        }
        double d = double(mant);
        if (exp10 > 0) d *= exp10 <= 22 ? kPow10[exp10] : std::pow(10.0, exp10);
        else if (exp10 < 0) d /= -exp10 <= 22 ? kPow10[-exp10] : std::pow(10.0, -exp10);
        v = neg ? -d : d;
        return true;
    }

    // Plain [t, d] / [t, d, l] notes straight from the buffer. Returns false
    // (consuming nothing) for anything else.
    bool fastNote(std::vector<double>& lastT) {
        const size_t have = r_.ensure(4096);
        const char* p = r_.data();
        const char* e = p + have;
        if (p >= e || *p != '[') return false;
        ++p;
        double v[3] = {0, 0, 0};
        int n = 0;
        for (;;) {
            while (p < e && isWs(*p)) ++p;
            if (n == 3 || !numAt(p, e, v[n])) return false;
            ++n;
            while (p < e && isWs(*p)) ++p;
            if (p >= e) return false;
            if (*p == ']') { ++p; break; }
            if (*p != ',') return false;
            ++p;
        }
        if (n < 2 || !std::isfinite(v[0]) || !(v[1] >= 0) || v[1] >= kNoteDataValues) return false;
        r_.advance(size_t(p - r_.data()));
        addStreamed(v[0], v[1], n > 2 ? v[2] : 0.0, false, std::string(), false, lastT);
        return true;
    }

    void addStreamed(double t, double d, double l, bool hasType, const std::string& typeText, bool altBool,
                     std::vector<double>& lastT) {
        StreamedNote n;
        n.time = t;
        n.length = std::isfinite(l) && l > 0 ? float(l) : 0.0f;
        const int data = int(d);
        n.data = uint8_t(data);
        if (hasType && !typeText.empty()) n.type = out_.typeIndex(typeText);
        if (altBool) n.type |= StreamedNote::kAltBit;
        out_.maxData = std::max(out_.maxData, data);
        ++out_.dataCount[size_t(data)];
        ++out_.rawCount;
        double& last = lastT[size_t(data)];
        if (std::fabs(n.time - last) < 1.0 && !out_.notes.empty()) {
            // Stacked note: counted on the kept one (search the recent tail).
            for (size_t i = out_.notes.size(); i-- > out_.rangeStart.back();) {
                StreamedNote& k = out_.notes[i];
                if (k.noteData() == data && std::fabs(k.time - n.time) < 1.0 && k.stack < 255) {
                    k.length = std::max(k.length, n.length);
                    if (n.typeIdx() && !k.typeIdx()) k.type = uint16_t((k.type & StreamedNote::kAltBit) | n.typeIdx());
                    ++k.stack;
                    return;
                }
                if (out_.notes.size() - i > 64) break;
            }
        }
        last = n.time;
        out_.notes.push_back(n);
    }

    // A note array element. Plain notes ([time, lane, length, type?]) go to the
    // buffer; anything else (legacy events, odd shapes) is copied to `kept`.
    bool noteElement(std::string& kept, bool& keptAny, std::vector<double>& lastT) {
        if (fastNote(lastT)) return true;
        if (r_.peek() != '[') {
            std::string tmp;
            if (!value(tmp, std::string_view(), 64)) return false;
            if (keptAny) kept += ',';
            kept += tmp;
            keptAny = true;
            return true;
        }
        r_.get();
        double nums[3] = {0, -1, 0};
        int count = 0;
        bool plain = true;
        std::string raw = "[";
        std::string typeText;
        bool hasType = false, altBool = false;
        r_.skipWs();
        if (r_.peek() == ']') { r_.get(); return true; }
        for (;;) {
            r_.skipWs();
            const int c = r_.peek();
            if (count > 0) raw += ',';
            if (c == '"') {
                std::string text;
                if (!string(raw, &text)) return false;
                if (count == 3) { typeText = std::move(text); hasType = true; }
                else if (count < 3) plain = false;
            } else if (c == '[' || c == '{') {
                std::string tmp;
                if (!value(tmp, std::string_view(), 64)) return false;
                raw += tmp;
                plain = false;
            } else if (c == 't' || c == 'f' || c == 'n') {
                std::string w;
                word(w);
                raw += w;
                if (count == 3 && w == "true") altBool = true;
                if (count < 3 && count != 2) plain = false;
            } else {
                double v = 0;
                const size_t before = raw.size();
                (void)before;
                if (!fastNumber(v)) return false;
                char tmp[40];
                std::snprintf(tmp, sizeof tmp, "%.17g", v);
                raw += tmp;
                if (count < 3) nums[count] = v;
            }
            ++count;
            r_.skipWs();
            const int sep = r_.get();
            if (sep == ']') break;
            if (sep != ',') return fail("expected , or ] in a note");
            r_.skipWs();
            if (r_.peek() == ']') { r_.get(); break; }  // trailing comma
        }
        raw += ']';
        if (!plain || count < 2 || !std::isfinite(nums[0]) || !(nums[1] >= 0) || nums[1] >= kNoteDataValues) {
            if (keptAny) kept += ',';
            kept += raw;
            keptAny = true;
            return true;
        }
        addStreamed(nums[0], nums[1], nums[2], hasType, typeText, altBool, lastT);
        return true;
    }

    bool value(std::string& out, std::string_view key, int depth) {
        if (depth > 200) return fail("JSON nested too deeply");
        r_.skipWs();
        const int c = r_.peek();
        if (c == '{') {
            r_.get();
            out += '{';
            bool first = true;
            for (;;) {
                r_.skipWs();
                if (r_.peek() == '}') { r_.get(); break; }
                if (!first) out += ',';
                first = false;
                std::string k;
                if (!string(out, &k)) return false;
                r_.skipWs();
                if (r_.get() != ':') return fail("expected :");
                out += ':';
                if (!value(out, k, depth + 1)) return false;
                r_.skipWs();
                const int sep = r_.get();
                if (sep == '}') break;
                if (sep != ',') return fail("expected , or }");
            }
            out += '}';
            return true;
        }
        if (c == '[') {
            r_.get();
            if (pulseForge_ && depth == 1 && key == "notes") {
                if (!drainAll()) return false;  // earlier sections first: the ranges stay in order
                const size_t k = sections_++;
                out_.rangeStart.push_back(uint32_t(out_.notes.size()));
                std::vector<double> lastT(kNoteDataValues, -1e300);
                for (;;) {
                    r_.skipWs();
                    if (r_.peek() == ']') { r_.get(); break; }
                    if (!pulseForgeNote(lastT)) return false;
                    r_.skipWs();
                    const int sep = r_.get();
                    if (sep == ']') break;
                    if (sep != ',') return fail("expected , or ] in notes");
                }
                out += "[{\"__gh_stream\":" + std::to_string(k) + "}]";
                return true;
            }
            if (key == "sectionNotes" && pool_) return parallelSection(out);
            if (key == "sectionNotes") {
                // Marker object first, then any kept (non-note) elements.
                const size_t k = sections_++;
                out_.rangeStart.push_back(uint32_t(out_.notes.size()));
                std::vector<double> lastT(kNoteDataValues, -1e300);
                std::string kept;
                bool keptAny = false;
                for (;;) {
                    r_.skipWs();
                    if (r_.peek() == ']') { r_.get(); break; }
                    if (!noteElement(kept, keptAny, lastT)) return false;
                    r_.skipWs();
                    const int sep = r_.get();
                    if (sep == ']') break;
                    if (sep != ',') return fail("expected , or ] in sectionNotes");
                }
                out += "[{\"__gh_stream\":" + std::to_string(k) + "}";
                if (keptAny) out += "," + kept;
                out += ']';
                return true;
            }
            out += '[';
            bool first = true;
            for (;;) {
                r_.skipWs();
                if (r_.peek() == ']') { r_.get(); break; }
                if (!first) out += ',';
                first = false;
                if (!value(out, std::string_view(), depth + 1)) return false;
                r_.skipWs();
                const int sep = r_.get();
                if (sep == ']') break;
                if (sep != ',') return fail("expected , or ]");
            }
            out += ']';
            return true;
        }
        if (c == '"') {
            if (depth != 1 || key != "format") return string(out, nullptr);
            std::string text;
            if (!string(out, &text)) return false;
            pulseForge_ = text == "pulseforge-chart" || text == "pulseforge-editor-chart";
            return true;
        }
        if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')) return number(out);
        if (std::isalpha(c)) {
            std::string w;
            word(w);
            if (w == "true" || w == "false" || w == "null") out += w;
            else out += "null";  // NaN, Infinity
            return true;
        }
        return fail("unexpected character");
    }

    // {"time": ms, "duration": ms, "lane": n, "owner": "player", "kind": "normal", ...}
    bool pulseForgeNote(std::vector<double>& lastT) {
        if (r_.get() != '{') return fail("expected a note object");
        double time = std::nan(""), duration = 0, lane = -1;
        std::string owner, kind;
        r_.skipWs();
        if (r_.peek() == '}') { r_.get(); return true; }
        for (;;) {
            r_.skipWs();
            std::string raw, k;
            if (!string(raw, &k)) return false;
            r_.skipWs();
            if (r_.get() != ':') return fail("expected :");
            r_.skipWs();
            if (k == "time" || k == "duration" || k == "lane") {
                double v = 0;
                if (!fastNumber(v)) return false;
                (k == "time" ? time : k == "duration" ? duration : lane) = v;
            } else if ((k == "owner" || k == "kind") && r_.peek() == '"') {
                std::string text;
                raw.clear();
                if (!string(raw, &text)) return false;
                (k == "owner" ? owner : kind) = std::move(text);
            } else {
                std::string skip;
                if (!value(skip, k, 64)) return false;
            }
            r_.skipWs();
            const int sep = r_.get();
            if (sep == '}') break;
            if (sep != ',') return fail("expected , or } in a note");
        }
        if (!std::isfinite(time) || !(lane >= 0) || lane >= 20) return true;
        const int who = owner == "opponent" || owner == "dad" || owner == "enemy" ? 1
                        : owner == "girlfriend" || owner == "gf"                 ? 2
                                                                                 : 0;
        const bool typed = !kind.empty() && kind != "normal";
        addStreamed(time, double(int(lane) + 20 * who), duration, typed, kind, false, lastT);
        return true;
    }

    // ---- parallel sections ----

    // Copies the elements of the array whose '[' was just read into `chunk`
    // (as chunkElements reads them), up to about `cap_` bytes, cutting only
    // after a top-level ','. done: its closing ']' was reached (and read).
    // Comments are copied whole (chunkElements skips them) and their brackets
    // count for nothing; one may be longer than the read buffer, so where a
    // piece ends inside one is kept for the next piece.
    bool capture(std::string& chunk, bool& done) {
        done = false;
        int depth = 0;
        bool inStr = false, esc = false;
        int comment = 0;     // in a comment: 1 for //, 2 for /* */
        bool slash = false;  // the last piece ended with a '/' outside strings and comments
        bool star = false;   // the last piece ended inside a /* */ comment, with a '*'
        // In a comment from p[i]: the index just after its end, or n when it
        // goes on in the next piece.
        auto commentEnd = [&](const char* p, size_t i, size_t n) -> size_t {
            if (comment == 1) {
                const void* nl = i < n ? std::memchr(p + i, '\n', n - i) : nullptr;
                if (!nl) return n;
                comment = 0;
                return size_t(static_cast<const char*>(nl) - p) + 1;
            }
            if (star && i < n) {
                star = false;
                if (p[i] == '/') {
                    comment = 0;
                    return i + 1;
                }
            }
            while (i < n) {
                const void* s = std::memchr(p + i, '*', n - i);
                if (!s) return n;
                i = size_t(static_cast<const char*>(s) - p) + 1;  // just after the '*'
                if (i == n) {
                    star = true;
                    return n;
                }
                if (p[i] == '/') {
                    comment = 0;
                    return i + 1;
                }
            }
            return n;
        };
        for (;;) {
            if (!r_.fill()) return fail("unterminated sectionNotes");
            const char* p = r_.data();
            const size_t n = r_.avail();
            // Commas matter only where this piece may be cut.
            const bool commas = chunk.size() + n >= cap_;
            size_t i = 0;
            if (esc) {  // a backslash ended the last piece: this byte is escaped
                esc = false;
                i = 1;
            } else if (slash) {  // a '/' ended it: this byte says whether a comment starts
                slash = false;
                if (p[0] == '/' || p[0] == '*') {
                    comment = p[0] == '/' ? 1 : 2;
                    i = 1;
                }
            }
            if (comment) i = commentEnd(p, i, n);
            // One byte, by the rules of JSON (in a string or not); returns
            // 0 to go on, 1 at the section's end or where it is cut, 3 after
            // a comment's start, going on at `next`.
            size_t skipAt = size_t(-1), next = 0;
            auto step = [&](size_t j) -> int {
                if (j == skipAt) return 0;  // escaped in a string
                const char c = p[j];
                if (inStr) {
                    if (c == '\\') {
                        if (j + 1 >= n) esc = true;
                        else skipAt = j + 1;
                    } else if (c == '"') {
                        inStr = false;
                    }
                    return 0;
                }
                if (c == '"') {
                    inStr = true;
                } else if (c == '[' || c == '{') {
                    ++depth;
                } else if (c == ']' || c == '}') {
                    if (depth == 0) {  // the end of the section
                        chunk.append(p, j);
                        r_.advance(j + 1);
                        done = true;
                        return 1;
                    }
                    --depth;
                } else if (c == '/') {
                    if (j + 1 >= n) {  // the next piece tells
                        slash = true;
                        return 0;
                    }
                    if (p[j + 1] != '/' && p[j + 1] != '*') return 0;
                    comment = p[j + 1] == '/' ? 1 : 2;
                    next = commentEnd(p, j + 2, n);
                    return 3;
                } else if (c == ',' && depth == 0 && chunk.size() + j >= cap_) {
                    chunk.append(p, j + 1);
                    r_.advance(j + 1);
                    return 1;  // the rest of the section follows
                }
                return 0;
            };
            while (i < n) {
                int st = 0;
                if (i + 64 <= n) {
                    for (uint64_t m = scan_.mask(p + i, commas); m; m &= m - 1) {
                        st = step(i + lowestBit(m));
                        if (st) break;
                    }
                    if (st == 0) { i += 64; continue; }
                } else {
                    for (; i < n; ++i) {
                        const char c = p[i];
                        if (c == '[' || c == ']' || c == '{' || c == '}' || c == '"' || c == '\\' || c == '/' || c == ',') {
                            st = step(i);
                            if (st) break;
                        }
                    }
                    if (st == 0) break;
                }
                if (st == 1) return true;
                i = next;  // after the comment (n: it goes on in the next piece)
            }
            chunk.append(p, n);
            r_.advance(n);
        }
    }

    bool parallelSection(std::string& out) {
        const size_t k = sections_++;
        std::string chunk;
        bool done = false;
        if (!capture(chunk, done)) return false;
        if (done) {
            // The whole section: a worker parses it.
            auto task = std::make_shared<std::packaged_task<SectionResult()>>([text = std::move(chunk)] {
                SectionResult r;
                r.notes.rangeStart.push_back(0);
                Reader mr(text.data(), text.size());
                Streamer s(mr, r.notes);
                std::vector<double> lastT(kNoteDataValues, -1e300);
                r.ok = s.chunkElements(lastT, r.kept, r.keptAny);
                if (!r.ok) r.err = s.error();
                return r;
            });
            pending_.push_back({k, task->get_future()});
            pool_->post([task] { (*task)(); });
            out += "[{\"__gh_stream\":" + std::to_string(k) + "}\x01" + std::to_string(k) + "\x02]";
            while (pending_.size() > size_t(pool_->size()) + 2)
                if (!mergeFront()) return false;
            return true;
        }
        // A section bigger than cap_: parsed here, piece after piece, as the
        // sequential reader would.
        if (!drainAll()) return false;
        out_.rangeStart.push_back(uint32_t(out_.notes.size()));
        std::vector<double> lastT(kNoteDataValues, -1e300);
        std::string kept;
        bool keptAny = false;
        for (;;) {
            Reader mr(chunk.data(), chunk.size());
            Streamer s(mr, out_);
            if (!s.chunkElements(lastT, kept, keptAny)) {
                if (err_.empty()) err_ = s.error();
                return false;
            }
            if (done) break;
            chunk.clear();
            if (!capture(chunk, done)) return false;
        }
        out += "[{\"__gh_stream\":" + std::to_string(k) + "}";
        if (keptAny) out += "," + kept;
        out += ']';
        return true;
    }

    // The oldest parsed section joins the chart's notes.
    bool mergeFront() {
        Pending p = std::move(pending_.front());
        pending_.pop_front();
        SectionResult r = p.result.get();
        if (!r.ok) {
            if (err_.empty()) err_ = r.err;
            return false;
        }
        out_.rangeStart.push_back(uint32_t(out_.notes.size()));
        std::vector<uint16_t> types(r.notes.types.size() + 1, 0);
        for (size_t i = 0; i < r.notes.types.size(); ++i) types[i + 1] = out_.typeIndex(r.notes.types[i]);
        const size_t at = out_.notes.size();
        out_.notes.insert(out_.notes.end(), r.notes.notes.begin(), r.notes.notes.end());
        if (!r.notes.types.empty())
            for (size_t i = at; i < out_.notes.size(); ++i) {
                StreamedNote& q = out_.notes[i];
                q.type = uint16_t((q.type & StreamedNote::kAltBit) | types[q.typeIdx()]);
            }
        out_.maxData = std::max(out_.maxData, r.notes.maxData);
        for (size_t d = 0; d < out_.dataCount.size(); ++d) out_.dataCount[d] += r.notes.dataCount[d];
        out_.rawCount += r.notes.rawCount;
        keptText_[p.k] = r.keptAny ? "," + r.kept : std::string();
        return true;
    }

    bool drainAll() {
        bool ok = true;
        while (!pending_.empty()) ok = mergeFront() && ok;
        return ok;
    }

    // The kept elements of sections parsed by workers go where their markers are.
    void finishSkeleton(std::string& s) const {
        std::string out;
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size()) {
            const size_t a = s.find('\x01', i);
            if (a == std::string::npos) break;
            const size_t b = s.find('\x02', a);
            out.append(s, i, a - i);
            const auto it = keptText_.find(std::stoull(s.substr(a + 1, b - a - 1)));
            if (it != keptText_.end()) out += it->second;
            i = b + 1;
        }
        out.append(s, i, std::string::npos);
        s.swap(out);
    }

    struct Pending {
        size_t k;
        std::future<SectionResult> result;
    };

    Reader& r_;
    StreamedNotes& out_;
    std::string err_;
    bool pulseForge_ = false;
    SectionPool* pool_ = nullptr;
    size_t cap_ = 8u << 20;
    size_t sections_ = 0;
    std::deque<Pending> pending_;
    std::map<size_t, std::string> keptText_;
    StructScan scan_;
};

}  // namespace

StreamedNotesBuilder::StreamedNotesBuilder(StreamedNotes& out) : out_(out), lastT_(kNoteDataValues, -1e300) {}

std::string StreamedNotesBuilder::beginArray() {
    out_.rangeStart.push_back(uint32_t(out_.notes.size()));
    std::fill(lastT_.begin(), lastT_.end(), -1e300);
    return "[{\"__gh_stream\":" + std::to_string(arrays_++) + "}]";
}

// The reader's addStreamed, step for step (a note the reader would not take
// as a note, with a noteData outside 0..255, is left out).
void StreamedNotesBuilder::add(double time, int noteData, double length, const std::string& type) {
    if (!std::isfinite(time) || noteData < 0 || noteData >= kNoteDataValues || out_.rangeStart.empty()) return;
    StreamedNote n;
    n.time = time;
    n.length = std::isfinite(length) && length > 0 ? float(length) : 0.0f;
    const int data = noteData;
    n.data = uint8_t(data);
    if (!type.empty()) n.type = out_.typeIndex(type);
    out_.maxData = std::max(out_.maxData, data);
    ++out_.dataCount[size_t(data)];
    ++out_.rawCount;
    double& last = lastT_[size_t(data)];
    if (std::fabs(n.time - last) < 1.0 && !out_.notes.empty()) {
        for (size_t i = out_.notes.size(); i-- > out_.rangeStart.back();) {
            StreamedNote& k = out_.notes[i];
            if (k.noteData() == data && std::fabs(k.time - n.time) < 1.0 && k.stack < 255) {
                k.length = std::max(k.length, n.length);
                if (n.typeIdx() && !k.typeIdx()) k.type = uint16_t((k.type & StreamedNote::kAltBit) | n.typeIdx());
                ++k.stack;
                return;
            }
            if (out_.notes.size() - i > 64) break;
        }
    }
    last = n.time;
    out_.notes.push_back(n);
}

void StreamedNotesBuilder::finish() { out_.rangeStart.push_back(uint32_t(out_.notes.size())); }

uint16_t StreamedNotes::typeIndex(const std::string& name) {
    for (size_t i = 0; i < types.size(); ++i)
        if (types[i] == name) return uint16_t(i + 1);
    if (types.size() >= 32000) return 0;  // the top bit of a note's type is its alt flag
    types.push_back(name);
    return uint16_t(types.size());
}

uint64_t fileSize(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
#if defined(_WIN32)
    _fseeki64(f, 0, SEEK_END);
    const long long sz = _ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    const long long sz = ftello(f);
#endif
    std::fclose(f);
    return sz > 0 ? uint64_t(sz) : 0;
}

bool streamChartSkeleton(const std::string& path, std::string& skeleton, StreamedNotes& notes, std::string& err, int threads,
                         size_t sectionCap) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    if (threads < 0) threads = std::clamp(int(std::thread::hardware_concurrency()) - 1, 0, 8);
    bool ok;
    {
        Reader r(f);
        // UTF-8 BOM
        if (r.peek() == 0xEF) { r.get(); r.get(); r.get(); }
        std::unique_ptr<SectionPool> pool;
        if (threads > 0) pool = std::make_unique<SectionPool>(threads);
        Streamer s(r, notes);
        if (pool) s.setPool(pool.get(), sectionCap ? sectionCap : (8u << 20));
        skeleton.clear();
        ok = s.run(skeleton, err);
    }  // the workers and the read-ahead thread stop before the file is closed
    std::fclose(f);
    notes.rangeStart.push_back(uint32_t(notes.notes.size()));
    return ok;
}

// ------------------------------------------------------------- cache ----

namespace {

namespace fs = std::filesystem;

std::mutex g_cacheMtx;
std::string g_cacheDir;
constexpr uint32_t kCacheMagic = 0x43434847u;  // "GHCC"
constexpr uint32_t kCacheVersion = 1;
constexpr uint64_t kCacheLimit = 4ull << 30;

struct CacheHeader {
    uint32_t magic = kCacheMagic, version = kCacheVersion, noteSize = uint32_t(sizeof(StreamedNote)), reserved = 0;
    uint64_t sourceSize = 0;
    int64_t sourceTime = 0;
};

fs::path u8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// The chart file's size and time: a cache entry is only used for that same file.
bool sourceStamp(const std::string& path, uint64_t& size, int64_t& time) {
    std::error_code ec;
    const fs::path p = u8(path);
    size = fs::file_size(p, ec);
    if (ec) return false;
    time = int64_t(fs::last_write_time(p, ec).time_since_epoch().count());
    return !ec;
}

std::string cacheFileFor(const std::string& dir, const std::string& path) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a of the path
    for (unsigned char c : path) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char name[40];
    std::snprintf(name, sizeof name, "%016llx.ghcc", (unsigned long long)h);
    return dir + name;
}

template <class T>
bool putAll(FILE* f, const T* p, uint64_t n) {
    return n == 0 || std::fwrite(p, sizeof(T), size_t(n), f) == size_t(n);
}

// Reads n items of T, if the file still has that many bytes.
template <class T>
bool getAll(FILE* f, T* p, uint64_t n, uint64_t& left) {
    if (n > left / sizeof(T)) return false;
    left -= n * sizeof(T);
    return n == 0 || std::fread(p, sizeof(T), size_t(n), f) == size_t(n);
}

bool readCache(const std::string& file, uint64_t size, int64_t time, std::string& skeleton, StreamedNotes& sn) {
    std::error_code ec;
    uint64_t left = fs::file_size(u8(file), ec);
    if (ec) return false;
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    CacheHeader h;
    uint64_t n = 0;
    bool ok = getAll(f, &h, 1, left) && h.magic == kCacheMagic && h.version == kCacheVersion &&
              h.noteSize == sizeof(StreamedNote) && h.sourceSize == size && h.sourceTime == time;
    if (ok && (ok = getAll(f, &n, 1, left) && n <= left)) {
        skeleton.resize(size_t(n));
        ok = getAll(f, skeleton.data(), n, left);
    }
    if (ok && (ok = getAll(f, &n, 1, left) && n <= left / sizeof(StreamedNote))) {
        sn.notes.resize(size_t(n));
        ok = getAll(f, sn.notes.data(), n, left);
    }
    if (ok && (ok = getAll(f, &n, 1, left) && n <= left / 4)) {
        sn.rangeStart.resize(size_t(n));
        ok = getAll(f, sn.rangeStart.data(), n, left);
    }
    if (ok && (ok = getAll(f, &n, 1, left) && n <= 65535)) {
        sn.types.resize(size_t(n));
        for (std::string& t : sn.types) {
            uint64_t len = 0;
            if (!(ok = getAll(f, &len, 1, left) && len <= left)) break;
            t.resize(size_t(len));
            if (!(ok = getAll(f, t.data(), len, left))) break;
        }
    }
    int32_t maxData = -1;
    if (ok) ok = getAll(f, &maxData, 1, left) && getAll(f, sn.dataCount.data(), sn.dataCount.size(), left) &&
                 getAll(f, &sn.rawCount, 1, left);
    sn.maxData = maxData;
    std::fclose(f);
    if (!ok) {
        skeleton.clear();
        sn = StreamedNotes();
    }
    return ok;
}

void trimCache(const std::string& dir, const fs::path& keep) {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    uint64_t total = 0;
    for (fs::directory_iterator it(u8(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".ghcc") continue;
        std::error_code e2;
        total += it->file_size(e2);
        files.emplace_back(it->last_write_time(e2), it->path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        if (total <= kCacheLimit) break;
        if (f.second == keep) continue;
        std::error_code e2;
        const uint64_t sz = fs::file_size(f.second, e2);
        if (fs::remove(f.second, e2)) total -= std::min(total, sz);
    }
}

void writeCache(const std::string& dir, const std::string& file, uint64_t size, int64_t time, const std::string& skeleton,
                const StreamedNotes& sn) {
    std::error_code ec;
    fs::create_directories(u8(dir), ec);
    // A name of its own while it is written: another load never reads half a file.
    const std::string tmp = file + "." + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFFFF) + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    CacheHeader h;
    h.sourceSize = size;
    h.sourceTime = time;
    const uint64_t counts[4] = {skeleton.size(), sn.notes.size(), sn.rangeStart.size(), sn.types.size()};
    bool ok = putAll(f, &h, 1) && putAll(f, &counts[0], 1) && putAll(f, skeleton.data(), counts[0]) && putAll(f, &counts[1], 1) &&
              putAll(f, sn.notes.data(), counts[1]) && putAll(f, &counts[2], 1) && putAll(f, sn.rangeStart.data(), counts[2]) &&
              putAll(f, &counts[3], 1);
    for (const std::string& t : sn.types) {
        const uint64_t len = t.size();
        ok = ok && putAll(f, &len, 1) && putAll(f, t.data(), len);
    }
    const int32_t maxData = sn.maxData;
    ok = ok && putAll(f, &maxData, 1) && putAll(f, sn.dataCount.data(), sn.dataCount.size()) && putAll(f, &sn.rawCount, 1);
    ok = (std::fclose(f) == 0) && ok;
    if (ok) fs::rename(u8(tmp), u8(file), ec);
    if (!ok || ec) {
        fs::remove(u8(tmp), ec);
        return;
    }
    trimCache(dir, u8(file));
}

}  // namespace

void setChartCacheDir(const std::string& dir) {
    std::lock_guard<std::mutex> lk(g_cacheMtx);
    g_cacheDir = dir;
    if (!g_cacheDir.empty() && g_cacheDir.back() != '/' && g_cacheDir.back() != '\\') g_cacheDir += '/';
}

std::string chartCacheDir() {
    std::lock_guard<std::mutex> lk(g_cacheMtx);
    return g_cacheDir;
}

bool loadChartFile(const std::string& path, const std::string& difficulty, Chart& out, std::string& err,
                   std::string_view vsliceMetadata, size_t streamThreshold, bool extraKeys) {
    std::string flpFile;
    int pattern = -1;
    if (flpChartPath(path, flpFile, pattern)) {  // an FL Studio project, read as SNIFF does
        FlpProject p;
        if (!readFlpFile(flpFile, p, err) || !flpToChart(p, pattern, out, err)) return false;
        out.difficulty = difficulty;
        return true;
    }
    if (isBinaryChart(path)) {  // God's Hand's own binary chart (.ghc)
        std::string skeleton;
        StreamedNotes sn;
        if (!readBinaryChart(path, skeleton, sn, err)) return false;
        return parseChartStreamed(skeleton, sn, difficulty, out, err, extraKeys);
    }
    const uint64_t size = fileSize(path);
    if (size == 0 && !fileExistsAt(path)) { err = "cannot read " + path; return false; }
    // A pipe has no size (gh_tests --chart /dev/stdin, fed by a decompressor):
    // whatever it holds is streamed.
    std::error_code ec;
    const auto kind = std::filesystem::status(std::filesystem::path(std::u8string(path.begin(), path.end())), ec).type();
    const bool pipe = !ec && (kind == std::filesystem::file_type::fifo || kind == std::filesystem::file_type::character);
    if (!vsliceMetadata.empty() || (!pipe && size < (streamThreshold ? streamThreshold : kStreamThreshold))) {
        std::string text;
        if (!readTextFile(path, text)) { err = "cannot read " + path; return false; }
        return parseChart(text, difficulty, out, err, vsliceMetadata, extraKeys);
    }
    std::string skeleton;
    StreamedNotes sn;
    // The cache: the notes of this very file, read before.
    const std::string dir = pipe ? std::string() : chartCacheDir();
    uint64_t srcSize = 0;
    int64_t srcTime = 0;
    const bool cacheable = !dir.empty() && sourceStamp(path, srcSize, srcTime);
    const std::string cached = cacheable ? cacheFileFor(dir, path) : std::string();
    if (cacheable && readCache(cached, srcSize, srcTime, skeleton, sn))
        return parseChartStreamed(skeleton, sn, difficulty, out, err, extraKeys);
    if (!streamChartSkeleton(path, skeleton, sn, err)) return false;
    if (cacheable) writeCache(dir, cached, srcSize, srcTime, skeleton, sn);
    return parseChartStreamed(skeleton, sn, difficulty, out, err, extraKeys);
}

bool fileExistsAt(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// ------------------------------------------------------------ peeking ----

namespace {
std::string findStringField(std::string_view text, std::string_view key) {
    const std::string pat = "\"" + std::string(key) + "\"";
    size_t p = 0;
    while ((p = text.find(pat, p)) != std::string_view::npos) {
        size_t q = p + pat.size();
        while (q < text.size() && (text[q] == ' ' || text[q] == '\t' || text[q] == '\r' || text[q] == '\n')) ++q;
        if (q < text.size() && text[q] == ':') {
            ++q;
            while (q < text.size() && (text[q] == ' ' || text[q] == '\t' || text[q] == '\r' || text[q] == '\n')) ++q;
            if (q < text.size() && text[q] == '"') {
                std::string out;
                for (++q; q < text.size() && text[q] != '"'; ++q) {
                    if (text[q] == '\\' && q + 1 < text.size()) ++q;
                    out += text[q];
                }
                return out;
            }
        }
        p += pat.size();
    }
    return "";
}
}  // namespace

ChartPeek peekChart(const std::string& path) {
    ChartPeek pk;
    constexpr size_t kWin = 96 * 1024;
    std::string head, tail;
    if (isBinaryChart(path)) {  // its JSON part holds the song's fields
        if (!readBinarySkeleton(path, head)) return pk;
        if (head.size() > 2 * kWin) {
            tail = head.substr(head.size() - kWin);
            head.resize(kWin);
        }
    }
    FILE* f = head.empty() ? std::fopen(path.c_str(), "rb") : nullptr;
    if (head.empty() && !f) return pk;
    if (f) {
        head.assign(kWin, '\0');
        head.resize(std::fread(head.data(), 1, kWin, f));
    }
    const uint64_t size = f ? fileSize(path) : 0;
    if (f && size > kWin) {
#if defined(_WIN32)
        _fseeki64(f, (long long)(size - std::min<uint64_t>(size - kWin, kWin)), SEEK_SET);
#else
        fseeko(f, off_t(size - std::min<uint64_t>(size - kWin, kWin)), SEEK_SET);
#endif
        tail.resize(kWin);
        tail.resize(std::fread(tail.data(), 1, kWin, f));
    }
    if (f) std::fclose(f);
    // Top-level fields come either before or after the (huge) notes array.
    // A section's "notes" never contain a "song" string, so the first match
    // in the head or the last in the tail is the song's own.
    auto field = [&](std::string_view key) {
        std::string v = findStringField(head, key);
        if (v.empty()) v = findStringField(tail, key);
        return v;
    };
    pk.pulseForge = head.find("\"pulseforge-chart\"") != std::string::npos ||
                    head.find("\"pulseforge-editor-chart\"") != std::string::npos;
    if (pk.pulseForge) {
        pk.song = field("title");
        pk.difficulty = field("difficulty");
        pk.player2 = field("opponent");
        pk.stage = field("stage");
        pk.isChart = true;
        return pk;
    }
    pk.song = field("song");
    if (pk.song.empty()) pk.song = field("songName");
    pk.player2 = field("player2");
    pk.stage = field("stage");
    pk.isChart = head.find("\"notes\"") != std::string::npos || head.find("\"sectionNotes\"") != std::string::npos ||
                 tail.find("\"sectionNotes\"") != std::string::npos || tail.find("\"bpm\"") != std::string::npos;
    return pk;
}

}  // namespace gh
