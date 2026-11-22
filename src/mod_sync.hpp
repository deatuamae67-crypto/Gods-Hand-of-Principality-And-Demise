// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Keeping the mods in step with their zips (issue #36): every zip installed
// leaves a record of the files it put in the mod's folder (with their CRC-32
// and size, as the zip lists them), in mods/.installed/records/<mod>.pack.
// When a newer zip of the same mod comes - the mod packs release changed, or
// a new copy is dropped into mods/ - what differs is worked out file by file:
// songs and files added, changed or gone. Installing the changes writes what
// is new, removes what the mod no longer has, and keeps everything it
// replaced or removed in mods/.installed/undo/<mod>/, so "Remove changes"
// puts the mod back as it was. Files the player added to a mod's folder are
// never touched: only what a record says came from the mod's zips.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gh::modsync {

struct FileInfo {
    uint32_t crc = 0;
    uint64_t size = 0;
    int zip = 0;  // which of the record's zips it came from
};
using FileList = std::map<std::string, FileInfo>;  // path in the mod's folder ('/') -> file

struct ZipInfo {
    std::string name;    // "GodsHand-mod-x.zip"
    std::string sha256;  // "" when not known
    uint64_t size = 0;
};

struct Record {
    std::string folder;          // the mod's folder in mods/
    std::string source = "zip";  // "pack": from MOD PACKS; "zip": dropped into mods/
    std::vector<ZipInfo> zips;
    FileList files;
};
std::string recordsDir(const std::string& modsDir);  // ends with '/'
bool loadRecord(const std::string& modsDir, const std::string& folder, Record& r);
bool saveRecord(const std::string& modsDir, const Record& r);
std::vector<std::string> recordedFolders(const std::string& modsDir);
bool safeName(const std::string& folder);  // one folder name, nothing that leaves mods/

// ----------------------------------------------------------------- zips --

struct ZipEntry {
    std::string name;  // as stored, '\\' made '/'
    uint32_t crc = 0;
    uint64_t size = 0;
    bool dir = false;
};
// The central directory from the end of a zip of `fileSize` bytes. `tail`:
// its last bytes. NeedMore: the directory starts before them, at cdOffset
// (cdSize bytes): read those and call parseCentralDirectoryAt.
enum class Cd { Ok, NeedMore, Bad };
Cd parseCentralDirectory(const std::string& tail, uint64_t fileSize, std::vector<ZipEntry>& out, uint64_t& cdOffset,
                         uint64_t& cdSize);
bool parseCentralDirectoryAt(const std::string& cd, std::vector<ZipEntry>& out);
bool listZip(const std::string& zipPath, std::vector<ZipEntry>& out);  // a zip on disk

// Where a zip's files go in mods/ (the rule installModZips always used): a
// leading "mods/" goes, a single top folder is the mod's name ("data",
// "images"... are a mod's contents, not a mod), else the zip's name.
struct Layout {
    std::string folder, prefix;  // the mod's folder; what to cut from each entry's name
};
Layout layoutOf(const std::vector<std::string>& names, const std::string& zipStem);
// The zip's files as the mod's folder will hold them.
FileList filesOf(const std::vector<ZipEntry>& entries, const Layout& lay, int zipIndex);

// ----------------------------------------------------------------- diffs --

struct Diff {
    std::vector<std::string> added, changed, removed;   // files
    std::vector<std::string> songsAdded, songsRemoved;  // songs (folders in songs/ or data/)
    bool empty() const { return added.empty() && changed.empty() && removed.empty(); }
    std::string summary() const;  // "2 songs added, 1 removed; 14 files changed, 3 new"
};
// What `next` changes in `installed`. scope: the installed files the new zips
// stand for (the others stay whatever happens); nullptr: all of them.
Diff diffFiles(const FileList& installed, const FileList& next, const std::vector<std::string>* scope = nullptr);

// --------------------------------------------------------------- install --

using Progress = std::function<void(const std::string& what, double fraction)>;

struct InstallOptions {
    // The zips are the whole mod (MOD PACKS): files of its old version that
    // none of them has go. Otherwise (a zip dropped into mods/) only those an
    // earlier copy of the same zip had.
    bool wholePack = false;
    std::string source = "zip";
    std::vector<ZipInfo> zipInfo;  // names and SHA-256 (MOD PACKS); otherwise taken from the files
};
struct InstallResult {
    std::string folder;
    Diff diff;
    int written = 0, kept = 0, skipped = 0;
    std::string message;
};
// Installs zips of one mod as described above (the zips stay where they are).
bool installZips(const std::string& modsDir, const std::vector<std::string>& zipPaths, const InstallOptions& opt, InstallResult& res,
                 const Progress& progress = {});

// The last change installed into a mod, kept to be removed again.
struct UndoInfo {
    bool exists = false;
    std::string summary;
    long long when = 0;  // seconds since 1970
};
UndoInfo undoInfo(const std::string& modsDir, const std::string& folder);
bool undoChanges(const std::string& modsDir, const std::string& folder, std::string& message);
// Deletes the files a mod's record lists (and the folders left empty), then
// the record: what the player added stays.
bool removeMod(const std::string& modsDir, const std::string& folder, std::string& message);

// ------------------------------------------------------ the mods release --

constexpr const char* kPacksUrl =
    "https://github.com/deatuamae67-crypto/Gods-Hand-of-Principality-And-Demise/releases/download/mods/";

struct RemoteZip {
    std::string name, url, sha256;
    uint64_t size = 0;
};
struct PackStatus {
    enum Kind { NotInstalled, UpToDate, Changed, Gone, Unknown };
    std::string folder;
    Kind kind = NotInstalled;
    std::vector<RemoteZip> zips;  // on the release now (none: Gone)
    Diff diff;                    // Changed: what differs, when it could be read
    bool diffKnown = false;
    bool legacy = false;  // installed before records were kept
    std::string note;
};
struct Manifest {
    std::vector<std::pair<std::string, std::vector<std::string>>> packs;  // folder -> zips, in order
};
bool parseManifest(const std::string& text, Manifest& out);

// Compares every installed pack with the mods release: what the API says of
// its zips (SHA-256), and, when they differ, their file lists (the end of each
// zip, fetched by byte range). Packs the release no longer has are Gone.
// apiReleaseJson: GitHub's "releases/tags/mods"; manifest: mod-packs.txt.
// fetchRange(url, range, bytes, err): a byte range of a file ("first-last";
// the zips' sizes come from the API); empty when ranges cannot be fetched
// (the zips are then Unknown).
using FetchRange = std::function<bool(const std::string& url, const std::string& range, std::string& bytes, std::string& err)>;
std::vector<PackStatus> comparePacks(const std::string& modsDir, const std::string& apiReleaseJson, const Manifest& manifest,
                                     const FetchRange& fetchRange, std::atomic<bool>* cancel = nullptr);
// The record kept after a pack's check found it unchanged: its zips' SHA-256
// (so the next check needs only the API's answer).
void rememberDigests(const std::string& modsDir, const PackStatus& st);

}  // namespace gh::modsync
