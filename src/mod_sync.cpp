// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "mod_sync.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include "common.hpp"
#include "miniz.h"
#include "updater.hpp"

namespace gh::modsync {

namespace fs = std::filesystem;

namespace {

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathStr(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string withSlash(std::string s) {
    if (!s.empty() && s.back() != '/' && s.back() != '\\') s += '/';
    return s;
}

bool readAll(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool writeAll(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o << text;
        if (!o) return false;
    }
    fs::remove(p, ec);
    fs::rename(tmp, p, ec);
    return !ec;
}

std::vector<std::string> splitChar(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t p = 0;
    for (;;) {
        const size_t e = s.find(sep, p);
        v.push_back(s.substr(p, e == std::string::npos ? std::string::npos : e - p));
        if (e == std::string::npos) break;
        p = e + 1;
    }
    return v;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> v;
    for (std::string& l : splitChar(text, '\n')) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) v.push_back(std::move(l));
    }
    return v;
}

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

uint16_t rd16(const unsigned char* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t rd64(const unsigned char* p) { return uint64_t(rd32(p)) | uint64_t(rd32(p + 4)) << 32; }

std::string hex8(uint32_t v) {
    char b[9];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}

std::string stemOf(const std::string& name) {
    std::string s = name;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const size_t dot = s.find_last_of('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

// CRC-32 of a file on disk (to know if an unrecorded file is the zip's).
bool crcOfFile(const fs::path& p, uint32_t& crc) {
    FILE* f = openRead(p);
    if (!f) return false;
    mz_ulong c = MZ_CRC32_INIT;
    std::vector<unsigned char> buf(1 << 20);
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) c = mz_crc32(c, buf.data(), n);
    std::fclose(f);
    crc = uint32_t(c);
    return true;
}

size_t writeOut(void* opaque, mz_uint64, const void* buf, size_t n) {
    static_cast<std::ofstream*>(opaque)->write(static_cast<const char*>(buf), std::streamsize(n));
    return n;
}

fs::path undoDir(const std::string& modsDir, const std::string& folder) {
    return toPath(withSlash(modsDir) + ".installed/undo/" + folder);
}
fs::path recordPath(const std::string& modsDir, const std::string& folder) { return toPath(recordsDir(modsDir) + folder + ".pack"); }

// Removes the empty folders from `from` up to (not including) `root`.
void pruneEmpty(const fs::path& root, fs::path from) {
    std::error_code ec;
    while (!from.empty() && from != root && pathStr(from).size() > pathStr(root).size()) {
        if (!fs::is_directory(from, ec) || !fs::is_empty(from, ec)) break;
        fs::remove(from, ec);
        if (ec) break;
        from = from.parent_path();
    }
}

bool moveFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::remove(to, ec);
    fs::rename(from, to, ec);
    if (!ec) return true;
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::remove(from, ec);
    return true;
}

// The song a file belongs to: songs/<x>/..., data/<x>/... (Psych), data/songs/<x>/....
std::string songOf(const std::string& path) {
    const std::vector<std::string> p = splitChar(path, '/');
    if (p.size() >= 4 && equalsI(p[0], "data") && equalsI(p[1], "songs")) return toLower(p[2]);
    if (p.size() >= 3 && (equalsI(p[0], "songs") || equalsI(p[0], "data"))) return toLower(p[1]);
    return "";
}

}  // namespace

// --------------------------------------------------------------- records --

std::string recordsDir(const std::string& modsDir) { return withSlash(modsDir) + ".installed/records/"; }

bool safeName(const std::string& folder) {
    return !folder.empty() && folder != "." && folder != ".." && folder.find_first_of("/\\:") == std::string::npos &&
           folder[0] != '.';
}

bool loadRecord(const std::string& modsDir, const std::string& folder, Record& r) {
    if (!safeName(folder)) return false;
    std::string text;
    if (!readAll(recordPath(modsDir, folder), text)) return false;
    const std::vector<std::string> ls = lines(text);
    if (ls.empty() || ls[0] != "GHMOD 1") return false;
    r = Record{};
    r.folder = folder;
    for (size_t i = 1; i < ls.size(); ++i) {
        const std::vector<std::string> f = splitChar(ls[i], '\t');
        if (f[0] == "source" && f.size() >= 2) r.source = f[1];
        else if (f[0] == "zip" && f.size() >= 4) r.zips.push_back({f[1], f[2] == "-" ? "" : f[2], std::strtoull(f[3].c_str(), nullptr, 10)});
        else if (f[0] == "f" && f.size() >= 5 && safeRelative(f[4])) {
            FileInfo fi;
            fi.crc = uint32_t(std::strtoul(f[1].c_str(), nullptr, 16));
            fi.size = std::strtoull(f[2].c_str(), nullptr, 10);
            fi.zip = std::atoi(f[3].c_str());
            r.files[f[4]] = fi;
        }
    }
    return true;
}

bool saveRecord(const std::string& modsDir, const Record& r) {
    if (!safeName(r.folder)) return false;
    std::string t = "GHMOD 1\nfolder\t" + r.folder + "\nsource\t" + r.source + "\n";
    for (const ZipInfo& z : r.zips) t += "zip\t" + z.name + "\t" + (z.sha256.empty() ? "-" : z.sha256) + "\t" + std::to_string(z.size) + "\n";
    for (const auto& [path, f] : r.files)
        t += "f\t" + hex8(f.crc) + "\t" + std::to_string(f.size) + "\t" + std::to_string(f.zip) + "\t" + path + "\n";
    return writeAll(recordPath(modsDir, r.folder), t);
}

std::vector<std::string> recordedFolders(const std::string& modsDir) {
    std::vector<std::string> v;
    std::error_code ec;
    for (fs::directory_iterator it(toPath(recordsDir(modsDir)), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = pathStr(it->path().filename());
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".pack") == 0) v.push_back(n.substr(0, n.size() - 5));
    }
    std::sort(v.begin(), v.end());
    return v;
}

// ------------------------------------------------------------------ zips --

Cd parseCentralDirectory(const std::string& tail, uint64_t fileSize, std::vector<ZipEntry>& out, uint64_t& cdOffset,
                         uint64_t& cdSize) {
    out.clear();
    const auto* p = reinterpret_cast<const unsigned char*>(tail.data());
    const size_t n = tail.size();
    if (n < 22 || fileSize < n) return Cd::Bad;
    const uint64_t tailStart = fileSize - n;
    size_t pos = std::string::npos;
    for (size_t i = n - 22 + 1; i-- > 0;) {
        if (rd32(p + i) == 0x06054b50 && i + 22 + rd16(p + i + 20) <= n) {
            pos = i;
            break;
        }
    }
    if (pos == std::string::npos) return Cd::Bad;
    uint64_t entries = rd16(p + pos + 10);
    cdSize = rd32(p + pos + 12);
    cdOffset = rd32(p + pos + 16);
    if (entries == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
        // Zip64: a locator just before, pointing to the 64-bit end record.
        if (pos < 20 || rd32(p + pos - 20) != 0x07064b50) return Cd::Bad;
        const uint64_t at = rd64(p + pos - 20 + 8);
        if (at < tailStart || at - tailStart + 56 > n) return Cd::Bad;
        const unsigned char* e = p + (at - tailStart);
        if (rd32(e) != 0x06064b50) return Cd::Bad;
        entries = rd64(e + 32);
        cdSize = rd64(e + 40);
        cdOffset = rd64(e + 48);
    }
    if (cdOffset > fileSize || cdSize > fileSize - cdOffset) return Cd::Bad;
    if (cdOffset < tailStart) return Cd::NeedMore;
    return parseCentralDirectoryAt(tail.substr(size_t(cdOffset - tailStart), size_t(cdSize)), out) ? Cd::Ok : Cd::Bad;
}

bool parseCentralDirectoryAt(const std::string& cd, std::vector<ZipEntry>& out) {
    out.clear();
    const auto* p = reinterpret_cast<const unsigned char*>(cd.data());
    const size_t n = cd.size();
    size_t i = 0;
    while (i + 46 <= n && rd32(p + i) == 0x02014b50) {
        uint64_t unc = rd32(p + i + 24);
        const uint64_t comp = rd32(p + i + 20);
        const size_t nl = rd16(p + i + 28), xl = rd16(p + i + 30), cl = rd16(p + i + 32);
        if (i + 46 + nl + xl + cl > n) return false;
        ZipEntry e;
        e.crc = rd32(p + i + 16);
        e.name.assign(reinterpret_cast<const char*>(p + i + 46), nl);
        if (unc == 0xFFFFFFFFu) {
            // Zip64 extra field: the 64-bit sizes, in this order, when they were too big.
            size_t x = i + 46 + nl;
            const size_t xe = x + xl;
            while (x + 4 <= xe) {
                const size_t id = rd16(p + x), sz = rd16(p + x + 2);
                if (x + 4 + sz > xe) break;
                if (id == 1 && sz >= 8) {
                    unc = rd64(p + x + 4);
                    break;
                }
                x += 4 + sz;
            }
        }
        (void)comp;
        e.size = unc;
        std::replace(e.name.begin(), e.name.end(), '\\', '/');
        e.dir = !e.name.empty() && e.name.back() == '/';
        out.push_back(std::move(e));
        i += 46 + nl + xl + cl;
    }
    return i == n || (i + 4 <= n && (rd32(p + i) == 0x06064b50 || rd32(p + i) == 0x06054b50));
}

bool listZip(const std::string& zipPath, std::vector<ZipEntry>& out) {
    out.clear();
    const fs::path zp = toPath(zipPath);
    std::error_code ec;
    const uintmax_t size = fs::file_size(zp, ec);
    FILE* f = ec ? nullptr : openRead(zp);
    mz_zip_archive zip{};
    if (!f || !mz_zip_reader_init_cfile(&zip, f, mz_uint64(size), 0)) {
        if (f) std::fclose(f);
        return false;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        ZipEntry e;
        e.name = st.m_filename;
        std::replace(e.name.begin(), e.name.end(), '\\', '/');
        e.crc = st.m_crc32;
        e.size = st.m_uncomp_size;
        e.dir = st.m_is_directory || (!e.name.empty() && e.name.back() == '/');
        out.push_back(std::move(e));
    }
    mz_zip_reader_end(&zip);
    std::fclose(f);
    return true;
}

Layout layoutOf(const std::vector<std::string>& names, const std::string& zipStem) {
    std::string strip;
    bool allMods = !names.empty();
    for (const std::string& s : names) allMods = allMods && s.rfind("mods/", 0) == 0;
    if (allMods) strip = "mods/";
    std::string top;
    bool single = true;
    for (const std::string& s0 : names) {
        const std::string s = s0.substr(strip.size());
        if (s.empty()) continue;
        const size_t slash = s.find('/');
        const std::string first = slash == std::string::npos ? "" : s.substr(0, slash);
        if (first.empty() || (!top.empty() && first != top)) {
            single = false;
            break;
        }
        top = first;
    }
    // A zip whose single folder is "data", "images"... holds a mod's contents, not a mod.
    static const char* contentDirs[] = {"data", "images", "songs", "characters", "stages", "scripts", "weeks", "music", "sounds"};
    for (const char* c : contentDirs)
        if (single && equalsI(top, c)) single = false;
    if (top.empty()) single = false;
    Layout l;
    l.folder = single ? top : zipStem;
    l.prefix = strip + (single ? top + "/" : "");
    return l;
}

FileList filesOf(const std::vector<ZipEntry>& entries, const Layout& lay, int zipIndex) {
    FileList v;
    for (const ZipEntry& e : entries) {
        if (e.dir || e.name.rfind(lay.prefix, 0) != 0) continue;
        const std::string rel = e.name.substr(lay.prefix.size());
        if (!safeRelative(rel) || rel.back() == '/') continue;
        v[rel] = FileInfo{e.crc, e.size, zipIndex};
    }
    return v;
}

// ----------------------------------------------------------------- diffs --

std::string Diff::summary() const {
    std::string s;
    auto part = [&](size_t n, const char* one, const char* many) {
        if (n == 0) return;
        if (!s.empty()) s += ", ";
        s += std::to_string(n) + " " + (n == 1 ? one : many);
    };
    part(songsAdded.size(), "song added", "songs added");
    part(songsRemoved.size(), "song removed", "songs removed");
    part(added.size(), "new file", "new files");
    part(changed.size(), "file changed", "files changed");
    part(removed.size(), "file removed", "files removed");
    return s.empty() ? "no changes" : s;
}

Diff diffFiles(const FileList& installed, const FileList& next, const std::vector<std::string>* scope) {
    Diff d;
    for (const auto& [path, f] : next) {
        const auto it = installed.find(path);
        if (it == installed.end()) d.added.push_back(path);
        else if (it->second.crc != f.crc || it->second.size != f.size) d.changed.push_back(path);
    }
    std::set<std::string> scoped;
    if (scope) scoped.insert(scope->begin(), scope->end());
    for (const auto& [path, f] : installed) {
        if (scope && !scoped.count(path)) continue;
        if (!next.count(path)) d.removed.push_back(path);
    }
    std::set<std::string> before, after;
    for (const auto& [path, f] : installed)
        if (const std::string s = songOf(path); !s.empty()) before.insert(s);
    for (const auto& [path, f] : next)
        if (const std::string s = songOf(path); !s.empty()) after.insert(s);
    for (const std::string& s : after)
        if (!before.count(s)) d.songsAdded.push_back(s);
    // A song is gone when the files that went took all of it with them.
    std::set<std::string> goneFrom;
    for (const std::string& path : d.removed)
        if (const std::string s = songOf(path); !s.empty()) goneFrom.insert(s);
    for (const std::string& s : goneFrom)
        if (!after.count(s)) d.songsRemoved.push_back(s);
    return d;
}

// --------------------------------------------------------------- install --

bool installZips(const std::string& modsDir, const std::vector<std::string>& zipPaths, const InstallOptions& opt, InstallResult& res,
                 const Progress& progress) {
    res = InstallResult{};
    struct Z {
        std::string path, name;
        std::vector<ZipEntry> entries;
        Layout lay;
        int index = 0;
    };
    std::vector<Z> zs;
    for (const std::string& p : zipPaths) {
        Z z;
        z.path = p;
        z.name = pathStr(toPath(p).filename());
        if (!listZip(p, z.entries)) {
            res.message = z.name + ": not a zip archive";
            return false;
        }
        std::vector<std::string> names;
        for (const ZipEntry& e : z.entries) names.push_back(e.name);
        z.lay = layoutOf(names, stemOf(z.name));
        zs.push_back(std::move(z));
    }
    if (zs.empty()) {
        res.message = "nothing to install";
        return false;
    }
    const std::string folder = zs[0].lay.folder;
    for (const Z& z : zs)
        if (z.lay.folder != folder) {
            res.message = "the zips are of different mods (" + folder + ", " + z.lay.folder + ")";
            return false;
        }
    if (!safeName(folder)) {
        res.message = "the zip's mod name cannot be a folder: " + folder;
        return false;
    }
    res.folder = folder;
    const fs::path root = toPath(withSlash(modsDir) + folder);

    Record old;
    const bool hadRecord = loadRecord(modsDir, folder, old);
    std::error_code ec0;
    const bool existed = fs::is_directory(root, ec0);  // installed before (with or without a record)
    Record rec;
    if (!opt.wholePack && hadRecord) rec = old;
    rec.folder = folder;
    if (opt.wholePack || !hadRecord) rec.source = opt.source;
    if (opt.wholePack) rec.zips.clear();
    std::set<int> replacedZips;  // the old record's zips these stand for
    for (Z& z : zs) {
        ZipInfo info{z.name, "", 0};
        std::error_code ec;
        info.size = uint64_t(fs::file_size(toPath(z.path), ec));
        for (const ZipInfo& i : opt.zipInfo)
            if (i.name == z.name) info = i;
        int idx = -1;
        for (size_t i = 0; i < rec.zips.size(); ++i)
            if (rec.zips[i].name == z.name) idx = int(i);
        if (idx < 0) {
            idx = int(rec.zips.size());
            rec.zips.push_back(info);
        } else {
            rec.zips[size_t(idx)] = info;
        }
        z.index = idx;
        if (hadRecord)
            for (size_t i = 0; i < old.zips.size(); ++i)
                if (old.zips[i].name == z.name) replacedZips.insert(int(i));
    }
    FileList next;
    for (const Z& z : zs) {
        for (auto& [path, f] : filesOf(z.entries, z.lay, z.index)) next[path] = f;
        // Entries outside the mod's folder, or that would leave it, are not installed.
        for (const ZipEntry& e : z.entries)
            if (!e.dir && (e.name.rfind(z.lay.prefix, 0) != 0 || !safeRelative(e.name.substr(z.lay.prefix.size())))) ++res.skipped;
    }

    // What the mod has now: the record, or - installed before records were
    // kept, or made by hand - the files on disk with the zip's names.
    FileList before;
    std::vector<std::string> scope;
    if (hadRecord) {
        before = old.files;
        for (const auto& [path, f] : old.files)
            if (opt.wholePack || replacedZips.count(f.zip)) scope.push_back(path);
    } else {
        for (const auto& [path, f] : next) {
            std::error_code ec;
            const fs::path fp = root / toPath(path);
            if (!fs::is_regular_file(fp, ec)) continue;
            FileInfo fi;
            fi.size = uint64_t(fs::file_size(fp, ec));
            if (fi.size == f.size && !crcOfFile(fp, fi.crc)) fi.crc = ~f.crc;
            before[path] = fi;
        }
    }
    res.diff = diffFiles(before, next, &scope);

    // The plan: what to write (new, changed, or missing / different on disk), what to remove.
    std::set<std::string> toWrite(res.diff.added.begin(), res.diff.added.end());
    toWrite.insert(res.diff.changed.begin(), res.diff.changed.end());
    for (const auto& [path, f] : next) {
        if (toWrite.count(path)) continue;
        std::error_code ec;
        const fs::path fp = root / toPath(path);
        if (!fs::is_regular_file(fp, ec) || uint64_t(fs::file_size(fp, ec)) != f.size) toWrite.insert(path);
    }
    std::vector<std::string> toRemove;
    for (const std::string& path : res.diff.removed) {
        std::error_code ec;
        if (fs::exists(root / toPath(path), ec)) toRemove.push_back(path);
    }
    if (opt.wholePack) rec.files = next;
    else {
        for (const std::string& path : scope) rec.files.erase(path);
        for (const auto& [path, f] : next) rec.files[path] = f;
    }
    if (toWrite.empty() && toRemove.empty()) {
        saveRecord(modsDir, rec);
        res.kept = int(next.size());
        res.message = folder + ": already up to date";
        return true;
    }

    // Only the last change is kept to be removed again.
    const fs::path undo = undoDir(modsDir, folder);
    std::error_code ec;
    fs::remove_all(undo, ec);
    fs::create_directories(undo / "files", ec);
    if (hadRecord) fs::copy_file(recordPath(modsDir, folder), undo / "record.pack", fs::copy_options::overwrite_existing, ec);
    std::string log = "GHUNDO 1\nwhen\t" +
                      std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()) +
                      "\nsummary\t" + res.diff.summary() + "\n";
    std::set<std::string> backedUp, logged;
    std::set<std::string> failed;  // to write or remove, and left as they were

    size_t done = 0;
    const size_t total = std::max<size_t>(1, toWrite.size() + toRemove.size());
    for (const Z& z : zs) {
        FILE* f = openRead(toPath(z.path));
        mz_zip_archive zip{};
        const uintmax_t zsize = fs::file_size(toPath(z.path), ec);
        if (!f || !mz_zip_reader_init_cfile(&zip, f, mz_uint64(zsize), 0)) {
            if (f) std::fclose(f);
            ++res.skipped;
            for (const auto& [path, fi] : next)
                if (fi.zip == z.index && toWrite.count(path)) failed.insert(path);
            continue;
        }
        const mz_uint n = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < n; ++i) {
            if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
            char buf[1024];
            mz_zip_reader_get_filename(&zip, i, buf, sizeof buf);
            std::string name = buf;
            std::replace(name.begin(), name.end(), '\\', '/');
            if (name.rfind(z.lay.prefix, 0) != 0) continue;
            const std::string rel = name.substr(z.lay.prefix.size());
            const auto it = next.find(rel);
            if (it == next.end() || it->second.zip != z.index || !toWrite.count(rel)) continue;
            const fs::path dst = root / toPath(rel);
            const fs::path bak = undo / "files" / toPath(rel);
            bool hadOld = false;
            if (fs::exists(dst, ec)) {
                if (!backedUp.count(rel) && moveFile(dst, bak)) {
                    backedUp.insert(rel);
                    hadOld = true;
                } else if (!backedUp.count(rel)) {
                    ++res.skipped;  // in use or read-only: left as it is
                    failed.insert(rel);
                    continue;
                }
            }
            fs::create_directories(dst.parent_path(), ec);
            std::ofstream o(dst, std::ios::binary | std::ios::trunc);
            const bool ok = o && mz_zip_reader_extract_to_callback(&zip, i, writeOut, &o, 0);
            o.close();
            if (!ok || !o) {
                fs::remove(dst, ec);
                if (hadOld) moveFile(bak, dst);
                backedUp.erase(rel);
                ++res.skipped;
                failed.insert(rel);
                continue;
            }
            failed.erase(rel);
            if (!logged.count(rel)) {
                log += std::string(backedUp.count(rel) ? "restore\t" : "delete\t") + rel + "\n";
                logged.insert(rel);
            }
            ++res.written;
            if (progress && (++done % 32) == 0) progress(z.name, double(done) / double(total));
        }
        mz_zip_reader_end(&zip);
        std::fclose(f);
    }
    for (const std::string& rel : toRemove) {
        const fs::path src = root / toPath(rel);
        if (moveFile(src, undo / "files" / toPath(rel))) {
            log += "restore\t" + rel + "\n";
            pruneEmpty(root, src.parent_path());
        } else {
            ++res.skipped;
            failed.insert(rel);
        }
        if (progress && (++done % 32) == 0) progress(folder, double(done) / double(total));
    }
    writeAll(undo / "undo.txt", log);
    // Files left as they were stay in the record as they were (or out of it),
    // and the zips' digests are forgotten: the next check compares the files
    // themselves, finds these, and offers the change again.
    if (!failed.empty()) {
        for (const std::string& rel : failed) {
            const auto b = before.find(rel);
            if (b != before.end() && fs::exists(root / toPath(rel), ec)) {
                FileInfo fi = b->second;
                if (opt.wholePack || fi.zip < 0 || size_t(fi.zip) >= rec.zips.size()) fi.zip = 0;
                rec.files[rel] = fi;
            } else {
                rec.files.erase(rel);
            }
        }
        for (const Z& z : zs)
            if (z.index >= 0 && size_t(z.index) < rec.zips.size()) rec.zips[size_t(z.index)].sha256.clear();
    }
    saveRecord(modsDir, rec);
    res.kept = int(next.size()) - res.written;
    res.message = (existed ? folder + ": " + res.diff.summary() : std::to_string(res.written) + " files into " + folder) +
                  (res.skipped ? " (" + std::to_string(res.skipped) + " skipped)" : "");
    return res.written > 0 || !toRemove.empty() || res.skipped == 0;
}

UndoInfo undoInfo(const std::string& modsDir, const std::string& folder) {
    UndoInfo u;
    if (!safeName(folder)) return u;
    std::string text;
    if (!readAll(undoDir(modsDir, folder) / "undo.txt", text)) return u;
    const std::vector<std::string> ls = lines(text);
    if (ls.empty() || ls[0] != "GHUNDO 1") return u;
    u.exists = true;
    for (const std::string& l : ls) {
        const std::vector<std::string> f = splitChar(l, '\t');
        if (f.size() >= 2 && f[0] == "summary") u.summary = f[1];
        else if (f.size() >= 2 && f[0] == "when") u.when = std::atoll(f[1].c_str());
    }
    return u;
}

bool undoChanges(const std::string& modsDir, const std::string& folder, std::string& message) {
    if (!safeName(folder)) return false;
    const fs::path undo = undoDir(modsDir, folder), root = toPath(withSlash(modsDir) + folder);
    std::string text;
    if (!readAll(undo / "undo.txt", text)) {
        message = folder + ": there is no change to remove";
        return false;
    }
    int restored = 0, removed = 0, failed = 0;
    std::string left;  // what could not be done (a file in use), kept to try again
    for (const std::string& l : lines(text)) {
        const std::vector<std::string> f = splitChar(l, '\t');
        const bool op = f.size() >= 2 && (f[0] == "delete" || f[0] == "restore");
        if (!op) {  // the header, when and summary: kept as they are
            if (!l.empty()) left += l + "\n";
            continue;
        }
        if (!safeRelative(f[1])) continue;  // never done
        const fs::path dst = root / toPath(f[1]);
        std::error_code ec;
        if (f[0] == "delete") {
            fs::remove(dst, ec);
            if (ec && fs::exists(dst)) {
                ++failed;
                left += l + "\n";
            } else {
                ++removed;
            }
            pruneEmpty(root, dst.parent_path());
        } else if (f[0] == "restore") {
            const fs::path bak = undo / "files" / toPath(f[1]);
            if (moveFile(bak, dst)) {
                ++restored;
            } else {
                std::error_code e2;
                if (fs::exists(bak, e2)) {  // its copy is still there: try again later
                    ++failed;
                    left += l + "\n";
                }
            }
        }
    }
    std::error_code ec;
    if (failed) {
        // The copies of the files not put back, and the record they belong
        // with, stay until REMOVE CHANGES can finish.
        writeAll(undo / "undo.txt", left);
    } else {
        if (fs::exists(undo / "record.pack", ec)) {
            fs::create_directories(toPath(recordsDir(modsDir)), ec);
            fs::copy_file(undo / "record.pack", recordPath(modsDir, folder), fs::copy_options::overwrite_existing, ec);
        } else {
            fs::remove(recordPath(modsDir, folder), ec);
        }
        fs::remove_all(undo, ec);
        pruneEmpty(toPath(withSlash(modsDir)), root);
    }
    message = folder + (failed ? ": part of the last change was removed (" : ": the last change was removed (") + std::to_string(restored) +
              (restored == 1 ? " file" : " files") + " put back, " + std::to_string(removed) + " removed" +
              (failed ? ", " + std::to_string(failed) + " in use: try again" : "") + ")";
    return failed == 0;
}

bool removeMod(const std::string& modsDir, const std::string& folder, std::string& message) {
    if (!safeName(folder)) return false;
    const fs::path root = toPath(withSlash(modsDir) + folder);
    Record r;
    std::error_code ec;
    if (!loadRecord(modsDir, folder, r)) {
        message = folder + ": no record of what it installed (remove the folder by hand)";
        return false;
    }
    int removed = 0, failed = 0;
    for (const auto& [path, f] : r.files) {
        const fs::path p = root / toPath(path);
        if (!fs::exists(p, ec)) continue;
        fs::remove(p, ec);
        if (ec) ++failed;
        else ++removed;
        pruneEmpty(root, p.parent_path());
    }
    pruneEmpty(toPath(withSlash(modsDir)), root);
    fs::remove(recordPath(modsDir, folder), ec);
    fs::remove_all(undoDir(modsDir, folder), ec);
    const bool left = fs::exists(root, ec);
    message = folder + ": " + std::to_string(removed) + " files removed" + (failed ? ", " + std::to_string(failed) + " in use" : "") +
              (left ? " (files you added stay in its folder)" : "");
    return failed == 0;
}

// ------------------------------------------------------ the mods release --

bool parseManifest(const std::string& text, Manifest& out) {
    out.packs.clear();
    for (const std::string& line : lines(text)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string folder = line.substr(0, tab);
        std::vector<std::string> zips;
        for (const std::string& z0 : splitChar(line.substr(tab + 1), ' ')) {
            const std::string z = trim(z0);
            if (!z.empty() && z.find_first_of("/\\") == std::string::npos) zips.push_back(z);
        }
        if (safeName(folder) && !zips.empty()) out.packs.emplace_back(folder, zips);
    }
    return !out.packs.empty();
}

std::vector<PackStatus> comparePacks(const std::string& modsDir, const std::string& apiReleaseJson, const Manifest& manifest,
                                     const FetchRange& fetchRange, std::atomic<bool>* cancel) {
    std::vector<PackStatus> out;
    update::Release rel;
    const bool haveApi = !apiReleaseJson.empty() && update::parseRelease(apiReleaseJson, rel);
    std::set<std::string> listed;
    for (const auto& [folder, zips] : manifest.packs) {
        listed.insert(folder);
        PackStatus st;
        st.folder = folder;
        for (const std::string& z : zips) {
            RemoteZip rz;
            rz.name = z;
            rz.url = std::string(kPacksUrl) + z;
            if (const update::Asset* a = haveApi ? rel.find(z) : nullptr) {
                rz.url = a->url;
                rz.sha256 = a->sha256;
                rz.size = a->size;
            }
            st.zips.push_back(rz);
        }
        Record r;
        const bool rec = loadRecord(modsDir, folder, r);
        std::error_code ec;
        const bool dir = fs::is_directory(toPath(withSlash(modsDir) + folder), ec);
        if (!rec && !dir) {
            out.push_back(std::move(st));
            continue;
        }
        st.legacy = !rec;
        // The same zips, with the same SHA-256: nothing changed.
        bool same = rec && r.zips.size() == st.zips.size();
        for (size_t i = 0; same && i < st.zips.size(); ++i)
            same = r.zips[i].name == st.zips[i].name && !st.zips[i].sha256.empty() && r.zips[i].sha256 == st.zips[i].sha256;
        if (same) {
            st.kind = PackStatus::UpToDate;
            out.push_back(std::move(st));
            continue;
        }
        st.kind = PackStatus::Unknown;
        bool sizes = true;
        for (const RemoteZip& z : st.zips) sizes = sizes && z.size > 0;
        if (!fetchRange || !sizes || (cancel && cancel->load())) {
            st.note = !sizes ? "GitHub did not say the zips' sizes" : "its files can be compared only with curl";
            out.push_back(std::move(st));
            continue;
        }
        // The zips' file lists, from their ends.
        FileList next;
        bool ok = true;
        for (size_t i = 0; i < st.zips.size() && ok; ++i) {
            const RemoteZip& z = st.zips[i];
            std::string tail, err, cdBytes;
            // An explicit range: GitHub's file servers refuse "the last n bytes" (501).
            const uint64_t want = std::min<uint64_t>(z.size, 1 << 18);
            ok = fetchRange(z.url, std::to_string(z.size - want) + "-" + std::to_string(z.size - 1), tail, err);
            std::vector<ZipEntry> entries;
            uint64_t off = 0, size = 0;
            Cd c = ok ? parseCentralDirectory(tail, z.size, entries, off, size) : Cd::Bad;
            if (c == Cd::NeedMore) {
                ok = size < (64ull << 20) &&
                     fetchRange(z.url, std::to_string(off) + "-" + std::to_string(off + size - 1), cdBytes, err) &&
                     parseCentralDirectoryAt(cdBytes, entries);
            } else {
                ok = c == Cd::Ok;
            }
            if (!ok) {
                st.note = "could not read " + z.name + (err.empty() ? "" : " (" + err + ")");
                break;
            }
            std::vector<std::string> names;
            for (const ZipEntry& e : entries) names.push_back(e.name);
            const Layout lay = layoutOf(names, stemOf(z.name));
            for (auto& [path, f] : filesOf(entries, lay, int(i))) next[path] = f;
        }
        if (!ok) {
            out.push_back(std::move(st));
            continue;
        }
        FileList before;
        static const std::vector<std::string> kNone;
        if (rec) {
            before = r.files;
        } else {
            // Installed before records were kept: what is on disk, by size.
            const fs::path root = toPath(withSlash(modsDir) + folder);
            for (const auto& [path, f] : next) {
                const fs::path fp = root / toPath(path);
                if (!fs::is_regular_file(fp, ec)) continue;
                FileInfo fi = f;
                fi.size = uint64_t(fs::file_size(fp, ec));
                if (fi.size != f.size) fi.crc = ~f.crc;
                before[path] = fi;
            }
        }
        st.diff = diffFiles(before, next, rec ? nullptr : &kNone);
        st.diffKnown = true;
        st.kind = st.diff.empty() ? PackStatus::UpToDate : PackStatus::Changed;
        if (st.kind == PackStatus::UpToDate && haveApi) {
            // The same files: remembered with the zips' SHA-256, so the next look needs only the API.
            Record nr = rec ? r : Record{};
            nr.folder = folder;
            nr.source = "pack";
            nr.zips.clear();
            for (const RemoteZip& z : st.zips) nr.zips.push_back({z.name, z.sha256, z.size});
            nr.files = next;
            saveRecord(modsDir, nr);
            st.legacy = false;
        }
        out.push_back(std::move(st));
    }
    for (const std::string& folder : recordedFolders(modsDir)) {
        if (listed.count(folder)) continue;
        Record r;
        if (!loadRecord(modsDir, folder, r) || r.source != "pack") continue;
        PackStatus st;
        st.folder = folder;
        st.kind = PackStatus::Gone;
        st.note = "no longer on the mods release";
        out.push_back(std::move(st));
    }
    return out;
}

void rememberDigests(const std::string& modsDir, const PackStatus& st) {
    Record r;
    if (!loadRecord(modsDir, st.folder, r) || r.zips.size() != st.zips.size()) return;
    for (size_t i = 0; i < st.zips.size(); ++i) {
        if (r.zips[i].name != st.zips[i].name) return;
        r.zips[i].sha256 = st.zips[i].sha256;
        r.zips[i].size = st.zips[i].size;
    }
    saveRecord(modsDir, r);
}

}  // namespace gh::modsync
