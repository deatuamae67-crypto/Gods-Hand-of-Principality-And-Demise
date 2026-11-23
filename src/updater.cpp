// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "updater.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "json.hpp"
#include "miniz.h"
#include "platform.hpp"
#include "sha256.hpp"

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace gh::update {

namespace fs = std::filesystem;

namespace {

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool readAll(const std::string& path, std::string& out) {
    std::ifstream in(toPath(path), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool writeAll(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(toPath(path).parent_path(), ec);
    {
        std::ofstream o(toPath(path + ".tmp"), std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o << text;
        if (!o) return false;
    }
    fs::remove(toPath(path), ec);
    fs::rename(toPath(path + ".tmp"), toPath(path), ec);
    return !ec;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> v;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        v.push_back(std::move(l));
        p = e + 1;
    }
    return v;
}

// A path inside the folder: no "..", no root, no drive letter.
bool safeRelative(const std::string& name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return false;
    size_t p = 0;
    while (p <= name.size()) {
        size_t s = name.find_first_of("/\\", p);
        if (s == std::string::npos) s = name.size();
        if (s - p == 2 && name.compare(p, 2, "..") == 0) return false;
        p = s + 1;
    }
    return true;
}

FILE* openRead(const fs::path& p) {
#if defined(_WIN32)
    return _wfopen(p.wstring().c_str(), L"rb");
#else
    return std::fopen(p.c_str(), "rb");
#endif
}

size_t writeOut(void* opaque, mz_uint64, const void* buf, size_t n) {
    static_cast<std::ofstream*>(opaque)->write(static_cast<const char*>(buf), std::streamsize(n));
    return n;
}

}  // namespace

std::string assetName() {
#if defined(__ANDROID__)
    return "GodsHand-android.apk";
#elif defined(_WIN32) && defined(GH_WINDOWS_LEGACY)
    return "GodsHand-windows-vista-7-x86.zip";
#elif defined(_WIN64)
    return "GodsHand-windows-x64.zip";
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return "GodsHand-macos-arm64.zip";
#elif defined(__linux__) && defined(__x86_64__)
    return "GodsHand-linux-x64.zip";
#else
    return "";
#endif
}

std::string exeName() {
#if defined(_WIN32)
    return "godshand.exe";
#else
    return "godshand";
#endif
}

Version parseVersion(std::string_view s) {
    Version v;
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == 'v' || s[i] == 'V')) ++i;
    for (int k = 0; k < 3; ++k) {
        if (i >= s.size() || s[i] < '0' || s[i] > '9') break;
        long n = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && n < 1000000) n = n * 10 + (s[i++] - '0');
        v.part[k] = int(n);
        v.ok = true;
        if (i < s.size() && s[i] == '.') ++i;
        else break;
    }
    return v;
}

int compareVersions(const Version& a, const Version& b) {
    for (int k = 0; k < 3; ++k)
        if (a.part[k] != b.part[k]) return a.part[k] < b.part[k] ? -1 : 1;
    return 0;
}

const Asset* Release::find(std::string_view assetName) const {
    for (const Asset& a : assets)
        if (a.name == assetName) return &a;
    return nullptr;
}

bool parseRelease(std::string_view text, Release& out, std::string* err) {
    json::Document d;
    if (!d.parse(text, err)) return false;
    const json::Value& r = d.root();
    if (r.type != json::Type::Object) {
        if (err) *err = "not a release";
        return false;
    }
    out = Release{};
    out.tag = std::string(d.strOr(d.get(r, "tag_name"), ""));
    out.name = std::string(d.strOr(d.get(r, "name"), ""));
    out.body = std::string(d.strOr(d.get(r, "body"), ""));
    out.draft = json::Document::boolean(d.get(r, "draft"), false);
    out.prerelease = json::Document::boolean(d.get(r, "prerelease"), false);
    if (const json::Value* as = d.get(r, "assets"); as && as->type == json::Type::Array) {
        for (size_t i = 0; i < d.size(*as); ++i) {
            const json::Value& e = d.at(*as, i);
            if (e.type != json::Type::Object) continue;
            Asset a;
            a.name = std::string(d.strOr(d.get(e, "name"), ""));
            a.url = std::string(d.strOr(d.get(e, "browser_download_url"), ""));
            a.size = uint64_t(std::max(0.0, json::Document::num(d.get(e, "size"), 0)));
            const std::string_view dig = d.strOr(d.get(e, "digest"), "");
            if (dig.size() == 71 && dig.substr(0, 7) == "sha256:") {
                std::string h(dig.substr(7));
                for (char& c : h) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (h.find_first_not_of("0123456789abcdef") == std::string::npos) a.sha256 = h;
            }
            if (!a.name.empty() && !a.url.empty()) out.assets.push_back(std::move(a));
        }
    }
    if (out.tag.empty()) {
        if (err) *err = "the release has no tag";
        return false;
    }
    return true;
}

bool stagePackage(const std::string& zipPath, const std::string& staging, std::vector<std::string>& files, std::string& err) {
    files.clear();
    const fs::path zp = toPath(zipPath);
    std::error_code ec;
    const uintmax_t size = fs::file_size(zp, ec);
    FILE* f = ec ? nullptr : openRead(zp);
    mz_zip_archive zip{};
    if (!f || !mz_zip_reader_init_cfile(&zip, f, mz_uint64(size), 0)) {
        if (f) std::fclose(f);
        err = "the download is not a zip archive";
        return false;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    std::vector<std::string> names(n);
    std::string top;
    bool single = n > 0;
    for (mz_uint i = 0; i < n; ++i) {
        char buf[1024];
        mz_zip_reader_get_filename(&zip, i, buf, sizeof buf);
        std::string s = buf;
        std::replace(s.begin(), s.end(), '\\', '/');
        names[i] = s;
        const size_t slash = s.find('/');
        const std::string first = slash == std::string::npos ? "" : s.substr(0, slash);
        if (first.empty() || (!top.empty() && first != top)) single = false;
        else top = first;
    }
    const std::string prefix = single ? top + "/" : "";
    const fs::path root = toPath(staging);
    fs::remove_all(root, ec);
    bool ok = true, haveExe = false;
    for (mz_uint i = 0; i < n && ok; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        std::string rel = names[i].substr(prefix.size());
        const std::string first = rel.substr(0, rel.find('/'));
        // The player's mods and raylib's files for programmers are not the game's.
        if (rel.find('/') != std::string::npos && (first == "mods" || first == "lib" || first == "include" || first == "update")) continue;
        if (!safeRelative(rel)) continue;
        const fs::path out = root / toPath(rel);
        fs::create_directories(out.parent_path(), ec);
        std::ofstream o(out, std::ios::binary | std::ios::trunc);
        const bool w = o && mz_zip_reader_extract_to_callback(&zip, i, writeOut, &o, 0);
        o.close();
        if (!w || !o) {
            err = "cannot write " + rel + " (disk full?)";
            ok = false;
            break;
        }
#if !defined(_WIN32)
        // Programs keep the permission to run them that the zip gives them.
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&zip, i, &st) && (st.m_version_made_by >> 8) == 3) {
            const unsigned mode = (st.m_external_attr >> 16) & 0777;
            if (mode) ::chmod(out.c_str(), mode);
        } else if (rel == exeName()) {
            ::chmod(out.c_str(), 0755);
        }
#endif
        haveExe = haveExe || rel == exeName();
        files.push_back(rel);
    }
    mz_zip_reader_end(&zip);
    std::fclose(f);
    if (ok && !haveExe) {
        err = "the download is not a God's Hand package (no " + exeName() + " in it)";
        ok = false;
    }
    if (!ok) {
        fs::remove_all(root, ec);
        files.clear();
    }
    return ok;
}

bool writeReady(const std::string& base, const std::string& version, const std::vector<std::string>& files, std::string& err) {
    std::string t = "GHUPDATE 1\nversion " + version + "\n";
    for (const std::string& f : files) t += "file " + f + "\n";
    if (!writeAll(base + "update/ready.txt", t)) {
        err = "cannot write " + base + "update/ready.txt";
        return false;
    }
    return true;
}

std::string pendingVersion(const std::string& base) {
    std::string t;
    if (!readAll(base + "update/ready.txt", t)) return "";
    for (const std::string& l : lines(t))
        if (l.rfind("version ", 0) == 0) return l.substr(8);
    return "";
}

Applied applyPending(const std::string& base) {
    Applied r;
    std::string t;
    if (!readAll(base + "update/ready.txt", t)) return r;
    const std::vector<std::string> ls = lines(t);
    if (ls.empty() || ls[0] != "GHUPDATE 1") return r;
    std::vector<std::string> files;
    for (const std::string& l : ls) {
        if (l.rfind("version ", 0) == 0) r.version = l.substr(8);
        else if (l.rfind("file ", 0) == 0 && safeRelative(l.substr(5))) files.push_back(l.substr(5));
    }
    std::error_code ec;
    // Whatever happens next, this update is not tried again at every start.
    fs::remove(toPath(base + "update/ready.txt"), ec);
    r.done = true;
    const fs::path root = toPath(base), staging = toPath(base + "update/staging");
    std::string oldList;
    for (const std::string& rel : files) {
        const fs::path src = staging / toPath(rel), dst = root / toPath(rel);
        if (!fs::is_regular_file(src, ec)) {
            ++r.failed;
            continue;
        }
        fs::create_directories(dst.parent_path(), ec);
        fs::path old = dst;
        old += ".old";
        bool moved = false;
        if (fs::exists(dst, ec)) {
            fs::remove(old, ec);
            fs::rename(dst, old, ec);  // works on a running program too
            if (ec) {
                ++r.failed;
                continue;
            }
            moved = true;
        }
        fs::rename(src, dst, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            if (moved) fs::rename(old, dst, ec);  // the old one back
            ++r.failed;
            continue;
        }
        ++r.files;
        if (rel == exeName()) r.exeChanged = true;
        if (moved) oldList += rel + ".old\n";
    }
    fs::remove_all(staging, ec);
    if (!oldList.empty()) {
        std::string before;
        readAll(base + "update/old-files.txt", before);
        writeAll(base + "update/old-files.txt", before + oldList);
    }
    r.message = r.failed == 0 ? "Updated to God's Hand " + r.version + "."
                              : "Update to " + r.version + ": " + std::to_string(r.failed) + " file(s) could not be replaced.";
    writeAll(base + "update/applied.txt", r.message);
    return r;
}

std::string takeAppliedNote(const std::string& base) {
    std::string t;
    if (!readAll(base + "update/applied.txt", t)) return "";
    std::error_code ec;
    fs::remove(toPath(base + "update/applied.txt"), ec);
    return t;
}

void removeOldFiles(const std::string& base) {
    std::string t;
    if (!readAll(base + "update/old-files.txt", t)) return;
    std::string keep;
    for (const std::string& rel : lines(t)) {
        if (rel.empty() || !safeRelative(rel)) continue;
        std::error_code ec;
        const fs::path p = toPath(base) / toPath(rel);
        fs::remove(p, ec);
        if (ec && fs::exists(p)) keep += rel + "\n";  // still in use (Windows): next time
    }
    std::error_code ec;
    if (keep.empty()) fs::remove(toPath(base + "update/old-files.txt"), ec);
    else writeAll(base + "update/old-files.txt", keep);
}

}  // namespace gh::update
