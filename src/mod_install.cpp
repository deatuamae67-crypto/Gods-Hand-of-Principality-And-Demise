// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "mod_install.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "common.hpp"
#include "mod_sync.hpp"

namespace gh {

namespace fs = std::filesystem;

namespace {
fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathStr(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
}  // namespace

int installModZips(const std::string& modsDirS, std::string& report, const InstallProgress& progress) {
    const fs::path modsDir = toPath(modsDirS);
    std::error_code ec;
    if (!fs::is_directory(modsDir, ec)) return 0;
    std::vector<fs::path> zips;
    for (fs::directory_iterator it(modsDir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && endsWithI(pathStr(it->path().filename()), ".zip")) zips.push_back(it->path());
    }
    int installed = 0;
    for (const fs::path& z : zips) {
        const std::string name = pathStr(z.filename());
        if (progress) progress(name, 0.0);
        // Recorded file by file: a newer copy of the same zip later removes
        // what it no longer has, and the change can be taken back (mod_sync.hpp).
        modsync::InstallResult res;
        const bool ok = modsync::installZips(modsDirS, {pathStr(z)}, modsync::InstallOptions{}, res,
                                             [&](const std::string&, double f) { if (progress) progress(name, f); });
        report += name + ": " + (ok ? "installed, " : "failed, ") + res.message + "\n";
        if (!ok) continue;
        ++installed;
        const fs::path done = modsDir / ".installed";
        fs::create_directories(done, ec);
        fs::rename(z, done / z.filename(), ec);
        if (ec) fs::remove(z, ec);
    }
    return installed;
}

}  // namespace gh
