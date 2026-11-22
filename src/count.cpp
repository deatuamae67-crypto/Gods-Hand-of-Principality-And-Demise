// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "count.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace gh {

namespace {

// a * b as a 128-bit (hi, lo) pair, without compiler extensions (MSVC has no __int128).
void mul64(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
    const uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

// x = x * m + a; false when the result does not fit (x is then left as it was).
bool mulAdd(Count& x, uint64_t m, uint64_t a) {
    Count r;
    uint64_t carry = a;
    for (size_t i = 0; i < 4; ++i) {
        uint64_t hi = 0, lo = 0;
        mul64(x.w[i], m, hi, lo);
        lo += carry;
        hi += lo < carry ? 1 : 0;
        r.w[i] = lo;
        carry = hi;
    }
    if (carry) return false;
    x = r;
    return true;
}

// x / d (d < 2^32) in place; returns the remainder.
uint32_t divSmall(Count& x, uint32_t d) {
    uint64_t rem = 0;
    for (size_t i = 4; i-- > 0;) {
        const uint64_t hiPart = (rem << 32) | (x.w[i] >> 32);
        const uint64_t qh = hiPart / d;
        rem = hiPart % d;
        const uint64_t loPart = (rem << 32) | (x.w[i] & 0xFFFFFFFFu);
        const uint64_t ql = loPart / d;
        rem = loPart % d;
        x.w[i] = (qh << 32) | ql;
    }
    return uint32_t(rem);
}

}  // namespace

Count Count::fromDouble(double v) {
    if (!(v >= 1.0)) return Count();
    if (v >= std::ldexp(1.0, 256)) return Count::max();
    Count c;
    for (int i = 3; i >= 0; --i) {
        const double unit = std::ldexp(1.0, 64 * i);
        const double q = std::floor(v / unit);
        if (q >= 1.0) {
            c.w[size_t(i)] = q >= 18446744073709551615.0 ? UINT64_MAX : uint64_t(q);
            v -= double(c.w[size_t(i)]) * unit;
            if (v < 0) v = 0;
        }
    }
    return c;
}

Count Count::max() {
    Count c;
    c.w.fill(UINT64_MAX);
    return c;
}

bool Count::isMax() const { return w[0] == UINT64_MAX && w[1] == UINT64_MAX && w[2] == UINT64_MAX && w[3] == UINT64_MAX; }

long long Count::satLL() const { return fits64() && w[0] <= uint64_t(LLONG_MAX) ? (long long)w[0] : LLONG_MAX; }

double Count::toDouble() const {
    double d = 0;
    for (size_t i = 4; i-- > 0;) d += std::ldexp(double(w[i]), int(64 * i));
    return d;
}

Count& Count::operator+=(const Count& o) {
    uint64_t carry = 0;
    for (size_t i = 0; i < 4; ++i) {
        const uint64_t s = w[i] + o.w[i];
        const uint64_t c1 = s < w[i] ? 1 : 0;
        w[i] = s + carry;
        carry = c1 | (w[i] < s ? 1 : 0);
    }
    if (carry) *this = max();
    return *this;
}

Count& Count::operator-=(const Count& o) {
    if (*this < o) return *this = Count();
    uint64_t borrow = 0;
    for (size_t i = 0; i < 4; ++i) {
        const uint64_t d = w[i] - o.w[i];
        const uint64_t b1 = w[i] < o.w[i] ? 1 : 0;
        const uint64_t r = d - borrow;
        borrow = b1 | (d < borrow ? 1 : 0);
        w[i] = r;
    }
    return *this;
}

Count Count::times(uint64_t m) const {
    Count r = *this;
    if (!mulAdd(r, m, 0)) return max();
    return r;
}

bool operator<(const Count& a, const Count& b) {
    for (size_t i = 4; i-- > 0;)
        if (a.w[i] != b.w[i]) return a.w[i] < b.w[i];
    return false;
}

std::string Count::str() const {
    if (fits64()) return std::to_string(w[0]);
    Count x = *this;
    std::string out;
    while (!x.isZero()) {
        uint32_t chunk = divSmall(x, 1000000000u);  // nine digits at a time
        for (int k = 0; k < 9; ++k) {
            out += char('0' + chunk % 10);
            chunk /= 10;
            if (x.isZero() && chunk == 0) break;
        }
    }
    while (out.size() > 1 && out.back() == '0') out.pop_back();
    std::reverse(out.begin(), out.end());
    return out;
}

std::string Count::grouped(char sep) const {
    const std::string s = str();
    std::string out;
    out.reserve(s.size() + s.size() / 3);
    for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && (s.size() - i) % 3 == 0) out += sep;
        out += s[i];
    }
    return out;
}

bool Count::parse(std::string_view s, Count& out) {
    if (s.empty()) return false;
    Count x;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        if (!mulAdd(x, 10, uint64_t(c - '0'))) return false;
    }
    out = x;
    return true;
}

void Count::appendVarint(std::string& out) const {
    Count x = *this;
    for (;;) {
        const unsigned char low = uint8_t(x.w[0] & 0x7F);
        // x >>= 7
        for (size_t i = 0; i < 4; ++i) x.w[i] = (x.w[i] >> 7) | (i + 1 < 4 ? x.w[i + 1] << 57 : 0);
        if (x.isZero()) {
            out += char(low);
            return;
        }
        out += char(low | 0x80);
    }
}

Count::Read Count::readVarint(const unsigned char*& p, const unsigned char* end, Count& out) {
    Count x;
    bool tooLarge = false;
    for (unsigned shift = 0;; shift += 7) {
        if (p >= end) return Read::Truncated;
        const unsigned char b = *p++;
        const uint64_t bits = b & 0x7F;
        if (bits) {
            if (shift >= 256 || (shift > 249 && (bits >> (256 - shift)) != 0)) {
                tooLarge = true;
            } else {
                const size_t word = shift / 64, off = shift % 64;
                x.w[word] |= bits << off;
                if (off > 57 && word + 1 < 4) x.w[word + 1] |= bits >> (64 - off);
            }
        }
        if (!(b & 0x80)) break;
    }
    if (tooLarge) return Read::TooLarge;
    out = x;
    return Read::Ok;
}

long long ScoreCount::satLL() const {
    const long long m = magnitude().satLL();
    return negative() ? -m : m;
}

void ScoreCount::set(double v) {
    gained = v > 0 ? Count::fromDouble(v) : Count();
    lost = v < 0 ? Count::fromDouble(-v) : Count();
}

}  // namespace gh
