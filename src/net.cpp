// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "net.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include "platform.hpp"
#include "process.hpp"

#if defined(_WIN32) && !defined(__ANDROID__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <urlmon.h>
#include <wininet.h>
#endif

namespace gh::net {

namespace fs = std::filesystem;

namespace {
fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool finishPart(const std::string& dest, std::string& err) {
    std::error_code ec;
    fs::remove(toPath(dest), ec);
    fs::rename(toPath(dest + ".part"), toPath(dest), ec);
    if (ec) {
        err = "cannot write " + dest;
        fs::remove(toPath(dest + ".part"), ec);
        return false;
    }
    return true;
}

#if defined(_WIN32) && !defined(__ANDROID__)
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Windows' own downloader (Vista and later): what Internet Explorer uses,
// TLS 1.2 included on an updated Windows 7.
bool windowsGet(const std::string& url, const std::string& part, std::string& err) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const std::wstring wu = wide(url);
    DeleteUrlCacheEntryW(wu.c_str());  // the newest copy, not one kept from before
    const HRESULT hr = URLDownloadToFileW(nullptr, wu.c_str(), toPath(part).wstring().c_str(), 0, nullptr);
    if (SUCCEEDED(co)) CoUninitialize();
    if (FAILED(hr)) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "download failed (0x%08lX)", static_cast<unsigned long>(hr));
        err = buf;
        return false;
    }
    return true;
}
#endif
}  // namespace

uint64_t rangeBytes(const std::string& range) {
    if (range.empty()) return 0;
    const size_t dash = range.find('-');
    if (dash == std::string::npos) return 0;
    if (dash == 0) return std::strtoull(range.c_str() + 1, nullptr, 10);
    const uint64_t a = std::strtoull(range.c_str(), nullptr, 10);
    const uint64_t b = std::strtoull(range.c_str() + dash + 1, nullptr, 10);
    return b >= a ? b - a + 1 : 0;
}

Http::Http(const std::string& baseDir) {
    if (!platform::kAndroid) curl_ = findProgram(baseDir, "curl", "--version");
}

bool Http::available() const {
#if defined(_WIN32)
    return true;  // curl, or Windows' own downloader
#else
    return platform::kAndroid || !curl_.empty();
#endif
}

bool Http::canRange() const { return platform::kAndroid || !curl_.empty(); }

bool Http::get(const std::string& url, const std::string& dest, std::string& err, const std::string& range) const {
    std::error_code ec;
    fs::create_directories(toPath(dest).parent_path(), ec);
    const std::string part = dest + ".part";
    fs::remove(toPath(part), ec);
    bool ok = false;
    if (platform::kAndroid) {
        for (int attempt = 0; attempt < 3 && !ok; ++attempt) ok = platform::download(url, part, err, range);
    } else if (!curl_.empty()) {
        std::vector<std::string> args = {"-L", "--fail", "--silent", "--show-error", "--retry", "3", "--connect-timeout", "20",
                                         "-A", "GodsHand", "-o", part};
        if (!range.empty()) {
            args.push_back("-r");
            args.push_back(range);
            // A server that sends the whole file instead stops at the range's size.
            args.push_back("--max-filesize");
            args.push_back(std::to_string(rangeBytes(range) + 65536));
        }
        args.push_back(url);
        const int code = runProcess(curl_, args);
        ok = code == 0;
        if (!ok) err = code == 22 ? "the server refused (HTTP error)" : code == 6 || code == 7 ? "no internet connection"
                     : code < 0 ? "curl did not start" : "curl failed (" + std::to_string(code) + ")";
    } else {
#if defined(_WIN32) && !defined(__ANDROID__)
        if (!range.empty()) {
            err = "byte ranges need curl";
            return false;
        }
        ok = windowsGet(url, part, err);
#else
        err = "curl was not found";
#endif
    }
    if (!ok) {
        fs::remove(toPath(part), ec);
        return false;
    }
    return finishPart(dest, err);
}

bool Http::getText(const std::string& url, const std::string& tmp, std::string& out, std::string& err,
                   const std::string& range) const {
    if (!get(url, tmp, err, range)) return false;
    std::ifstream in(toPath(tmp), std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    const bool ok = !in.bad();
    in.close();
    std::error_code ec;
    fs::remove(toPath(tmp), ec);
    if (!ok) err = "cannot read " + tmp;
    return ok;
}

}  // namespace gh::net
