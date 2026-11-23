// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The updater's own thread: GitHub's answer, the download, its check, the
// staging (updater.hpp).
#include <algorithm>
#include <filesystem>

#include "net.hpp"
#include "platform.hpp"
#include "sha256.hpp"
#include "updater.hpp"

namespace gh::update {

namespace fs = std::filesystem;

namespace {
fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
}  // namespace

// ------------------------------------------------------------------ Updater

struct Updater::Shared {
    mutable std::mutex m;
    Status st;
    std::atomic<bool> running{false};
    std::string partFile;
    uint64_t expect = 0;
    void set(Stage s, const std::string& msg, double progress = -1) {
        std::lock_guard<std::mutex> lk(m);
        st.stage = s;
        st.message = msg;
        if (progress >= 0) st.progress = progress;
    }
};

namespace {
void runUpdate(Updater::Shared& sh, std::string base, std::string current, std::string api);
}

Updater::~Updater() {
    if (thread_.joinable()) {
        if (sh_ && sh_->running.load()) thread_.detach();  // closing mid-download: let it go
        else thread_.join();
    }
}

bool Updater::busy() const { return sh_ && sh_->running.load(); }

void Updater::wait() {
    if (thread_.joinable()) thread_.join();
}

void Updater::start(const std::string& base, const std::string& currentVersion, const std::string& apiBase) {
    if (busy()) return;
    wait();
    sh_ = std::make_shared<Shared>();
    sh_->running = true;
    std::shared_ptr<Shared> sh = sh_;
    thread_ = std::thread([sh, base, currentVersion, apiBase] {
        runUpdate(*sh, base, currentVersion, apiBase);
        sh->running = false;
    });
}

Status Updater::status() const {
    if (!sh_) return Status{};
    std::lock_guard<std::mutex> lk(sh_->m);
    Status s = sh_->st;
    if (s.stage == Stage::Downloading && sh_->expect > 0) {
        std::error_code ec;
        const auto got = fs::file_size(toPath(sh_->partFile), ec);
        if (!ec) s.progress = std::min(1.0, double(got) / double(sh_->expect));
    }
    return s;
}

namespace {

void runUpdate(Updater::Shared& sh, std::string base, std::string current, std::string api) {
    const Version cur = parseVersion(current);
    if (!cur.ok) {
        sh.set(Stage::Idle, "A development build: it does not update itself.");
        return;
    }
    std::error_code ec;
    if (fs::exists(toPath(base + "CMakeCache.txt"), ec)) {
        sh.set(Stage::Idle, "Built from the source code: it does not update itself.");
        return;
    }
    const std::string asset = assetName();
    if (asset.empty()) {
        sh.set(Stage::Idle, "There are no ready-made downloads for this system.");
        return;
    }
    fs::create_directories(toPath(base + "update"), ec);
    if (ec && !fs::is_directory(toPath(base + "update"))) {
        sh.set(Stage::Failed, "The game's folder cannot be written to: update it by hand from the Releases page.");
        return;
    }
    net::Http http(base);
    if (!http.available()) {
        sh.set(Stage::Failed, "curl was not found: update by hand from the Releases page.");
        return;
    }
    sh.set(Stage::Checking, "Looking for a new version...");
    std::string text, err;
    if (!http.getText(api + "releases/latest", base + "update/latest.json", text, err)) {
        sh.set(Stage::Failed, "Could not look for updates (" + err + ").");
        return;
    }
    Release rel;
    if (!parseRelease(text, rel, &err)) {
        sh.set(Stage::Failed, "Could not read GitHub's answer (" + err + ").");
        return;
    }
    const Version v = parseVersion(rel.tag);
    {
        std::lock_guard<std::mutex> lk(sh.m);
        sh.st.version = rel.tag;
    }
    if (!v.ok || rel.draft || rel.prerelease || compareVersions(v, cur) <= 0) {
        sh.set(Stage::UpToDate, "God's Hand is up to date (v" + current + ").");
        return;
    }
    const Asset* a = rel.find(asset);
    if (!a) {
        sh.set(Stage::Failed, rel.tag + " has no " + asset + " yet.");
        return;
    }
    if (!platform::kAndroid && pendingVersion(base) == rel.tag) {
        sh.set(Stage::Ready, "God's Hand " + rel.tag + " is ready: it installs when the game starts again.", 1.0);
        return;
    }
    const std::string dest = base + "update/" + a->name;
    std::string hex;
    const bool have = fs::is_regular_file(toPath(dest), ec) && (a->size == 0 || fs::file_size(toPath(dest), ec) == a->size) &&
                      !a->sha256.empty() && sha256File(dest, hex) && hex == a->sha256;
    if (!have) {
        {
            std::lock_guard<std::mutex> lk(sh.m);
            sh.partFile = dest + ".part";
            sh.expect = a->size;
        }
        sh.set(Stage::Downloading, "Downloading God's Hand " + rel.tag + "...", 0.0);
        if (!http.get(a->url, dest, err)) {
            sh.set(Stage::Failed, "The download of " + rel.tag + " failed (" + err + ").");
            return;
        }
        sh.set(Stage::Verifying, "Checking the download...", 1.0);
        if (a->size && fs::file_size(toPath(dest), ec) != a->size) {
            fs::remove(toPath(dest), ec);
            sh.set(Stage::Failed, "The download of " + rel.tag + " is incomplete.");
            return;
        }
        if (!a->sha256.empty() && (!sha256File(dest, hex) || hex != a->sha256)) {
            fs::remove(toPath(dest), ec);
            sh.set(Stage::Failed, "The download of " + rel.tag + " is damaged (its SHA-256 differs from GitHub's).");
            return;
        }
    }
    if (platform::kAndroid) {
        {
            std::lock_guard<std::mutex> lk(sh.m);
            sh.st.file = dest;
        }
        sh.set(Stage::Ready, "God's Hand " + rel.tag + " is downloaded: UPDATE on the main menu installs it.", 1.0);
        return;
    }
    std::vector<std::string> files;
    if (!stagePackage(dest, base + "update/staging", files, err) || !writeReady(base, rel.tag, files, err)) {
        sh.set(Stage::Failed, "The update could not be prepared (" + err + ").");
        return;
    }
    fs::remove(toPath(dest), ec);  // unpacked: the zip is not needed any more
    sh.set(Stage::Ready, "God's Hand " + rel.tag + " is ready: it installs when the game starts again (UPDATE on the main menu).", 1.0);
}

}  // namespace

}  // namespace gh::update
