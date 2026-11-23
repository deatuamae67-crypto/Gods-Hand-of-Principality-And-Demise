// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// God's Hand bigit charts (.ghb, issue #36: "try bigits"). A bigit is a
// digit of a number too big for any machine integer, as bignum libraries
// call them. Here the whole chart's notes become one such number: an
// arithmetic coder narrows an interval note by note, each note taking a
// share of it as large as the chance the model gave it, and the file keeps
// the digits (base 256) of a number inside the final interval. A note the
// model expected (the same gap as the one before, the next lane of a
// pattern, no sustain...) costs a fraction of a bit; a surprise costs more.
// The file is about as small as what the chart says that could not be
// foreseen - the limit no packer can beat with the same model.
//
// The model, learnt as it goes (nothing is stored but the bigits): charts
// repeat themselves, so after the same six notes as before the note that
// followed them then is expected again (one bit says it was: a small
// fraction of a bit along a copied section); otherwise the gap to the note
// before (the same again, a chord, one of the last four gaps, or a new one),
// the lane from the lanes before it, the sustain, the stack and the note
// type, each in the light of what came just before. The JSON part (song
// data, sections, events) is kept deflated, or with an order-1 model when
// that is smaller. Notes come in blocks of 2^19, each its own number, so
// loading uses every core.
//
// What .ghb trades: it is the smallest of the game's formats, and it loads
// slower than .ghc (deflate unpacks faster than a model predicts).
#pragma once

#include <cstdint>
#include <string>

#include "chart_binary.hpp"
#include "chart_stream.hpp"

namespace gh {

constexpr const char* kBigitChartExt = ".ghb";

bool isBigitChart(const std::string& path);
bool writeBigitChart(const std::string& path, const std::string& skeleton, const StreamedNotes& sn, std::string& err);
// What streamChartSkeleton gives for the chart it was made from.
bool readBigitChart(const std::string& path, std::string& skeleton, StreamedNotes& sn, std::string& err, int threads = -1);
bool readBigitSkeleton(const std::string& path, std::string& skeleton);
// Any chart the game reads as JSON or .ghc to .ghb.
bool convertChartToBigit(const std::string& src, const std::string& dst, std::string& err, ConvertStats* stats = nullptr);

// The arithmetic coder alone (tests): bits with adaptive chances, and the
// bigits they become.
namespace bigit {
class Encoder {
public:
    explicit Encoder(std::string& out) : out_(out) {}
    void bit(uint16_t& p, int b);
    void bitP(int p1, int b);        // a chance a model worked out: p1 of 4096 that it is 1
    void direct(uint64_t v, int n);  // n plain bits, high first
    void finish();

private:
    void shiftLow();
    std::string& out_;
    uint64_t low_ = 0;
    uint32_t range_ = 0xFFFFFFFFu;
    uint8_t cache_ = 0;
    uint64_t cacheSize_ = 1;
};
class Decoder {
public:
    Decoder(const uint8_t* p, const uint8_t* end);
    int bit(uint16_t& p);
    int bitP(int p1);
    uint64_t direct(int n);
    bool overrun() const { return overrun_; }

private:
    uint8_t next();
    const uint8_t *p_, *end_;
    uint32_t code_ = 0, range_ = 0xFFFFFFFFu;
    int extra_ = 0;  // bytes read past the end (a damaged file)
    bool overrun_ = false;
};
constexpr uint16_t kHalf = 1 << 10;  // a new bit's chance: 1/2 (of 2048)
}  // namespace bigit

}  // namespace gh
