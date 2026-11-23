// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// SHA-256 (FIPS 180-4): what GitHub gives as each release file's "digest",
// checked before an update or a mod pack is installed.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gh {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t bytes);
    std::array<uint8_t, 32> finish();  // once; the object is spent after it
    std::string finishHex();           // lowercase hex, 64 characters

private:
    void block(const uint8_t* p);
    std::array<uint32_t, 8> h_;
    uint8_t buf_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

// Lowercase hex of a file's SHA-256; false (and why) if it cannot be read.
bool sha256File(const std::string& path, std::string& hex, std::string* err = nullptr);

}  // namespace gh
