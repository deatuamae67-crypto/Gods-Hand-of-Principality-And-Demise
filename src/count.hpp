// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Note counts of any size the engine plays (issue #36): 256-bit unsigned
// integers, exact up to 2^256 - 1 (about 1.16e77 notes). Charts store their
// counts with no limit at all (variable-length numbers, see appendVarint);
// the game plays whatever fits in 256 bits, and says so when a chart holds
// more. Sums never wrap around: past the top they stay at the top.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace gh {

struct Count {
    std::array<uint64_t, 4> w{};  // little-endian 64-bit words

    constexpr Count() = default;
    constexpr Count(uint64_t v) : w{v, 0, 0, 0} {}  // NOLINT: counts mix with plain integers
    static Count max();

    bool isZero() const { return (w[0] | w[1] | w[2] | w[3]) == 0; }
    bool fits64() const { return (w[1] | w[2] | w[3]) == 0; }
    bool isMax() const;
    uint64_t sat64() const { return fits64() ? w[0] : UINT64_MAX; }
    long long satLL() const;  // for places that keep 64-bit numbers (scores saved, Lua integers)
    double toDouble() const;
    static Count fromDouble(double v);  // scripts setting a count (below 0: 0)

    Count& operator+=(const Count& o);  // saturating
    Count& operator-=(const Count& o);  // floors at 0
    Count& operator++() { return *this += Count(1); }
    friend Count operator+(Count a, const Count& b) { return a += b; }
    friend Count operator-(Count a, const Count& b) { return a -= b; }
    Count times(uint64_t m) const;  // saturating

    friend bool operator==(const Count& a, const Count& b) { return a.w == b.w; }
    friend bool operator!=(const Count& a, const Count& b) { return !(a == b); }
    friend bool operator<(const Count& a, const Count& b);
    friend bool operator>(const Count& a, const Count& b) { return b < a; }
    friend bool operator<=(const Count& a, const Count& b) { return !(b < a); }
    friend bool operator>=(const Count& a, const Count& b) { return !(a < b); }

    std::string str() const;                 // "1234567"
    std::string grouped(char sep = ',') const;  // "1,234,567"
    // Decimal digits (and nothing else). False when empty, not a number or above max().
    static bool parse(std::string_view s, Count& out);

    // Unsigned LEB128 of any length: 7 bits a byte, low first. read() takes
    // numbers of any size and says when one is too large for 256 bits.
    void appendVarint(std::string& out) const;
    enum class Read { Ok, Truncated, TooLarge };
    static Read readVarint(const unsigned char*& p, const unsigned char* end, Count& out);
};

// A score: it goes up with hits and down with misses (Psych takes 10 a miss),
// so it can be below zero. Exact at any size, as counts are.
struct ScoreCount {
    Count gained, lost;
    void add(const Count& c) { gained += c; }
    void sub(const Count& c) { lost += c; }
    bool negative() const { return lost > gained; }
    Count magnitude() const { return negative() ? lost - gained : gained - lost; }
    double toDouble() const { return gained.toDouble() - lost.toDouble(); }
    long long satLL() const;
    void set(double v);  // scripts setting songScore
    std::string str() const { return (negative() ? "-" : "") + magnitude().str(); }
    friend bool operator==(const ScoreCount& a, const ScoreCount& b) {
        return a.negative() == b.negative() && a.magnitude() == b.magnitude();
    }
    std::string grouped(char sep = ',') const { return (negative() ? "-" : "") + magnitude().grouped(sep); }
};

}  // namespace gh
