// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The game updating itself from its GitHub releases (issue #36). At start
// (Options > Auto-update) it asks GitHub for the latest release; when that is
// newer than this build, it downloads this system's file, checks it against
// the SHA-256 GitHub gives for it, and:
//  - desktop: unpacks it into <game>/update/staging/ and leaves ready.txt;
//    the files take their places the next time the game starts, before
//    anything is loaded (the main menu's UPDATE restarts it at once). Only
//    the game's own files are replaced: mods/, settings, scores and the
//    player's files are never touched, and nothing is deleted.
//  - Android: the system installer opens with the new APK (same signature,
//    so it installs over this one and keeps the game's data).
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gh::update {

constexpr const char* kApi = "https://api.github.com/repos/deatuamae67-crypto/Gods-Hand-of-Principality-And-Demise/";

// This build's file on a release ("GodsHand-linux-x64.zip"...), "" if none.
std::string assetName();
// The game's program in its folder ("godshand.exe" on Windows).
std::string exeName();

struct Version {
    int part[3] = {0, 0, 0};
    bool ok = false;
};
Version parseVersion(std::string_view s);  // "v2.2.0", "2.2", "2.2.0-beta" (ok false for "dev")
int compareVersions(const Version& a, const Version& b);

struct Asset {
    std::string name, url, sha256;  // sha256: lowercase hex, "" when GitHub gives none
    uint64_t size = 0;
};
struct Release {
    std::string tag, name, body;
    bool draft = false, prerelease = false;
    std::vector<Asset> assets;
    const Asset* find(std::string_view assetName) const;
};
// One release as GitHub's API writes it (releases/latest, releases/tags/...).
bool parseRelease(std::string_view json, Release& out, std::string* err = nullptr);

// Unpacks a release zip's game files (under its top folder, "GodsHand/") into
// staging: mods/, lib/ and include/ are left out, as is anything that would
// land outside the folder. The files written, relative, with '/'.
bool stagePackage(const std::string& zipPath, const std::string& staging, std::vector<std::string>& files, std::string& err);
// <base>update/ready.txt: the version and the staged files, one a line.
bool writeReady(const std::string& base, const std::string& version, const std::vector<std::string>& files, std::string& err);
std::string pendingVersion(const std::string& base);  // what ready.txt says is staged ("" = nothing)

// At start, before anything is loaded: puts the staged files in their places
// (each file replaced is first renamed to <name>.old, so a running program can
// be replaced on Windows too, and put back if something fails). True when it
// did; `exeChanged` says the game's program itself was replaced (restart it).
// Then the .old files of an earlier update are removed where they can be.
struct Applied {
    bool done = false, exeChanged = false;
    std::string version, message;
    int files = 0, failed = 0;
};
Applied applyPending(const std::string& base);
void removeOldFiles(const std::string& base);
// What the last applyPending said ("Updated to ..."), once: the game started
// again after it shows it.
std::string takeAppliedNote(const std::string& base);

// The check, download and staging, on a thread of its own.
enum class Stage { Idle, Checking, UpToDate, Downloading, Verifying, Ready, Failed };
struct Status {
    Stage stage = Stage::Idle;
    std::string version;   // the release found ("v2.3.0")
    std::string message;   // for the player
    std::string url;       // Android: the new APK's download link (the browser downloads it)
    double progress = 0;   // download, 0..1
};

class Updater {
public:
    // A thread of its own that may still be downloading when the game
    // closes: it is let go then (it keeps only shared state), and the next
    // start downloads again.
    ~Updater();
    // currentVersion: this build's ("dev" builds never update). apiBase: kApi,
    // or another server for the tests.
    void start(const std::string& base, const std::string& currentVersion, const std::string& apiBase = kApi);
    Status status() const;
    bool busy() const;
    void wait();  // until the thread is done
    struct Shared;  // what the thread and the game share (updater_job.cpp)

private:
    std::shared_ptr<Shared> sh_;
    std::thread thread_;
};

}  // namespace gh::update
