// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Fetching files over HTTPS: the system's curl on desktop (Windows 10 and
// later, macOS and Linux come with it), Windows' own downloader where there
// is no curl (Vista, 7), java.net on Android. Used by the updater and the mod
// packs screen.
#pragma once

#include <cstdint>
#include <string>

namespace gh::net {

class Http {
public:
    explicit Http(const std::string& baseDir);  // looks for curl (next to the game, then the PATH)
    bool available() const;
    // url into dest, written as dest + ".part" and renamed once complete.
    // range: "first-last" bytes, or "-n" for the last n bytes ("" = all of it;
    // GitHub's file servers take only the first kind); with curl a server
    // that ignores the range is cut off at the range's size.
    bool get(const std::string& url, const std::string& dest, std::string& err, const std::string& range = "") const;
    // A small file into memory (through tmp, removed afterwards).
    bool getText(const std::string& url, const std::string& tmp, std::string& out, std::string& err,
                 const std::string& range = "") const;
    bool canRange() const;  // byte ranges can be asked for (not with Windows' own downloader)
    const std::string& curl() const { return curl_; }

private:
    std::string curl_;
};

// Bytes of a range like "100-199" or "-65536" (0 if it is not one).
uint64_t rangeBytes(const std::string& range);

}  // namespace gh::net
