// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Mod packs screen (Freeplay: M): the packs published on the game's "mods"
// release, downloaded (the system's curl; on Android through Java), checked
// against the SHA-256 GitHub gives for them and installed with a record of
// their files (mod_sync.hpp). For the packs installed, what changed on the
// release since (issue #36): songs and files added, changed or removed, to
// install - or, once installed, to remove again - and the packs taken off
// the release, to remove.
#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "app.hpp"
#include "common.hpp"
#include "gfx.hpp"
#include "mod_sync.hpp"
#include "net.hpp"
#include "platform.hpp"
#include "raylib.h"
#include "sha256.hpp"
#include "updater.hpp"

namespace gh {

namespace {
namespace fs = std::filesystem;
using gfx::Align;
using gfx::FontId;
using modsync::PackStatus;

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

uint64_t fileSizeOf(const std::string& p) {
    std::error_code ec;
    const auto s = fs::file_size(toPath(p), ec);
    return ec ? 0 : uint64_t(s);
}

// Shared with the screen's worker thread.
struct Shared {
    std::mutex m;
    std::string status;
    std::string currentFile;  // .part being written
    bool busy = false, done = false;
};

void runPackCheck(const std::string& base, PackCheck& pc) {
    net::Http http(base);
    std::string manifestText, apiJson, err;
    modsync::Manifest man;
    bool ok = http.available();
    if (!ok) err = "curl was not found";
    if (ok) ok = http.getText(std::string(modsync::kPacksUrl) + "mod-packs.txt", base + "cache/mod-packs.txt", manifestText, err);
    if (ok && !modsync::parseManifest(manifestText, man)) {
        ok = false;
        err = "the list of packs is empty";
    }
    std::vector<PackStatus> packs;
    if (ok) {
        // Without the API's answer there are no SHA-256 to compare: each pack's files are then read.
        std::string apiErr;
        http.getText(std::string(update::kApi) + "releases/tags/mods", base + "cache/mods-release.json", apiJson, apiErr);
        modsync::FetchRange fetch;
        if (http.canRange())
            fetch = [&http, base](const std::string& url, const std::string& range, std::string& bytes, std::string& e) {
                return http.getText(url, base + "cache/pack-range.bin", bytes, e, range);
            };
        packs = modsync::comparePacks(base + "mods/", apiJson, man, fetch);
    }
    std::lock_guard<std::mutex> lk(pc.m);
    pc.packs = std::move(packs);
    pc.ok = ok;
    pc.message = ok ? "" : "Could not reach the mods release (" + err + ").";
    pc.running = false;
    pc.done = true;
    ++pc.generation;
}

std::string dateOf(long long t) {
    if (t <= 0) return "";
    const std::time_t tt = std::time_t(t);
    char buf[32];
    if (const std::tm* tm = std::localtime(&tt)) {
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tm);
        return buf;
    }
    return "";
}

}  // namespace

int PackCheck::changed() const {
    int n = 0;
    for (const PackStatus& p : packs) n += p.kind == PackStatus::Changed || p.kind == PackStatus::Gone;
    return n;
}

void App::startPackCheck() {
    {
        std::lock_guard<std::mutex> lk(packCheck->m);
        if (packCheck->running) return;
        packCheck->running = true;
    }
    std::thread([pc = packCheck, b = base] { runPackCheck(b, *pc); }).detach();
}

class ModPacksState : public State {
public:
    void enter() override {
        App& a = app();
        if (!http_.available()) {
            sh_->status = "curl was not found. Download the packs from the game's Releases page (tag 'mods') and put the zips in mods/.";
            return;
        }
        sh_->status = "Looking at the mods release...";
        a.startPackCheck();  // a fresh look each time the screen opens (one already running is used)
    }

    void update(double) override {
        App& a = app();
        Input& in = a.input;
        bool busy, done;
        {
            std::lock_guard<std::mutex> lk(sh_->m);
            busy = sh_->busy;
            done = sh_->done;
            sh_->done = false;
        }
        if (done) {
            // The library finds the songs that came or went.
            a.library.scan(a.base, [](const std::string& zip, double f) {
                BeginDrawing();
                ClearBackground(Color{12, 8, 20, 255});
                const char* t1 = TextFormat("Installing %s", zip.c_str());
                const int w = GetScreenWidth(), h = GetScreenHeight();
                DrawText(t1, (w - MeasureText(t1, 24)) / 2, h / 2 - 30, 24, RAYWHITE);
                DrawRectangle(w / 4, h / 2 + 10, int(w / 2 * f), 10, Color{200, 160, 255, 255});
                EndDrawing();
                PollInputEvents();
                SwapScreenBuffer();
            });
            a.startPackCheck();  // what is installed now
        }
        pullCheck();
        const int n = int(rows_.size()) + 1;  // + "all"
        if (busy) return;
        if (detail_) {
            updateDetail();
            return;
        }
        if (in.up()) { sel_ = (sel_ + n - 1) % n; sfxPlay(Sfx::Scroll); }
        if (in.down()) { sel_ = (sel_ + 1) % n; sfxPlay(Sfx::Scroll); }
        if (in.accept() && n > 1 && http_.available()) {
            if (sel_ == 0) {
                std::vector<PackStatus> todo;
                for (const PackStatus& p : rows_)
                    if (p.kind == PackStatus::NotInstalled) todo.push_back(p);
                if (!todo.empty()) {
                    sfxPlay(Sfx::Confirm);
                    startJob([this, todo, base = a.base] { install(base, todo, false); });
                }
            } else {
                const PackStatus& p = rows_[size_t(sel_ - 1)];
                sfxPlay(Sfx::Confirm);
                if (p.kind == PackStatus::NotInstalled) startJob([this, p, base = a.base] { install(base, {p}, false); });
                else openDetail(p);
            }
        }
        if (in.back()) {
            sfxPlay(Sfx::Cancel);
            a.change(makeFreeplay());
        }
    }

    void draw() override {
        App& a = app();
        gfx::beginCanvas();
        drawMenuBackground(a.now, 0x5A3C8C, menuPulse());
        gfx::text(FontId::Title, "MOD PACKS", gfx::W / 2, 40, 54, gfx::rgb(0xFFE08A), Align::Center, 3);
        gfx::text(FontId::Ui, "Not all of these mods are by deatuamae67-crypto: the others belong to their creators", gfx::W / 2, 98,
                  17, gfx::fade(WHITE, 0.5f), Align::Center);
        std::string status, current;
        bool busy;
        {
            std::lock_guard<std::mutex> lk(sh_->m);
            status = sh_->status;
            current = sh_->currentFile;
            busy = sh_->busy;
        }
        if (detail_) {
            drawDetail();
        } else {
            float y = 130;
            auto row = [&](int i, const std::string& label, const std::string& right, Color rc) {
                const bool on = i == sel_;
                if (on) DrawRectangleRounded({120, y - 6, 1040, 36}, 0.3f, 6, Color{255, 255, 255, 30});
                gfx::text(FontId::Ui, label, 140, y, 26, on ? gfx::rgb(0xFFFFFF) : gfx::fade(WHITE, 0.7f));
                gfx::text(FontId::Ui, right, 1140, y + 3, 20, rc, Align::Right);
                y += 38;
            };
            if (!rows_.empty()) {
                int missing = 0, changed = 0;
                for (const PackStatus& p : rows_) {
                    missing += p.kind == PackStatus::NotInstalled;
                    changed += p.kind == PackStatus::Changed || p.kind == PackStatus::Gone;
                }
                row(0, "Download every missing pack",
                    std::to_string(missing) + " missing" + (changed ? ", " + std::to_string(changed) + " changed" : ""),
                    gfx::fade(WHITE, 0.6f));
                const int first = std::max(1, std::min(sel_ - 6, int(rows_.size()) - 11));
                for (int i = first; i < int(rows_.size()) + 1 && y < 600; ++i) {
                    const PackStatus& p = rows_[size_t(i - 1)];
                    Color rc = gfx::fade(WHITE, 0.6f);
                    std::string right;
                    switch (p.kind) {
                        case PackStatus::NotInstalled: right = p.zips.size() > 1 ? std::to_string(p.zips.size()) + " parts" : ""; break;
                        case PackStatus::UpToDate: right = "installed"; break;
                        case PackStatus::Changed:
                            right = "CHANGED: " + (p.diffKnown ? p.diff.summary() : std::string("new version"));
                            rc = gfx::rgb(0xFFD25A);
                            break;
                        case PackStatus::Gone:
                            right = "TAKEN OFF THE RELEASE";
                            rc = gfx::rgb(0xFF7A7A);
                            break;
                        case PackStatus::Unknown: right = "installed (not compared)"; break;
                    }
                    if (right.size() > 58) right = right.substr(0, 55) + "...";
                    row(i, prettyName(p.folder), right, rc);
                }
            } else if (checking_) {
                gfx::text(FontId::Ui, "Looking at the mods release...", gfx::W / 2, 300, 24, gfx::fade(WHITE, 0.7f), Align::Center);
            }
        }
        if (busy && !current.empty()) status += TextFormat("  (%.1f MB)", double(fileSizeOf(current)) / 1e6);
        gfx::text(FontId::Ui, status, gfx::W / 2, 620, 20, gfx::fade(WHITE, 0.8f), Align::Center);
        const char* help = busy      ? "Working... please wait"
                           : detail_ ? (a.touchMode ? "LEFT/RIGHT choose   OK do it   BACK" : "LEFT/RIGHT choose   ENTER do it   ESC back")
                           : a.touchMode ? "OK download / details"
                                         : "UP/DOWN select   ENTER download or see the changes   ESC back";
        gfx::text(FontId::Ui, help, gfx::W / 2, 670, 20, gfx::fade(WHITE, 0.55f), Align::Center);
        gfx::endCanvas();
    }

    ~ModPacksState() override {
        if (worker_.joinable()) worker_.join();
    }

private:
    enum class Act { Install, Undo, Remove, Back };

    // The rows follow the last check (the packs of the release, then those gone from it).
    void pullCheck() {
        App& a = app();
        std::lock_guard<std::mutex> lk(a.packCheck->m);
        checking_ = a.packCheck->running;
        if (a.packCheck->generation == seenGeneration_) return;
        seenGeneration_ = a.packCheck->generation;
        rows_ = a.packCheck->packs;
        sel_ = std::min(sel_, int(rows_.size()));
        std::lock_guard<std::mutex> lk2(sh_->m);
        if (!a.packCheck->ok) {
            sh_->status = a.packCheck->message;
        } else if (sh_->status.rfind("Looking", 0) == 0 || sh_->status.empty()) {
            const int changed = a.packCheck->changed();
            sh_->status = std::to_string(rows_.size()) + " packs" +
                          (changed ? ": " + std::to_string(changed) + " changed on the release since you installed them." : ".");
        }
        if (detail_) {
            for (const PackStatus& p : rows_)
                if (p.folder == detailPack_.folder) detailPack_ = p;
            actions_ = actionsFor(detailPack_);
            act_ = std::min(act_, int(actions_.size()) - 1);
        }
    }

    std::vector<Act> actionsFor(const PackStatus& p) const {
        std::vector<Act> v;
        if (p.kind == PackStatus::Changed || p.kind == PackStatus::Unknown || p.legacy) v.push_back(Act::Install);
        if (undo_.exists) v.push_back(Act::Undo);
        modsync::Record r;
        if (modsync::loadRecord(app().base + "mods/", p.folder, r)) v.push_back(Act::Remove);
        v.push_back(Act::Back);
        return v;
    }
    static const char* label(Act a) {
        switch (a) {
            case Act::Install: return "INSTALL CHANGES";
            case Act::Undo: return "REMOVE CHANGES";
            case Act::Remove: return "REMOVE PACK";
            default: return "BACK";
        }
    }

    void openDetail(const PackStatus& p) {
        detail_ = true;
        detailPack_ = p;
        undo_ = modsync::undoInfo(app().base + "mods/", p.folder);
        actions_ = actionsFor(p);
        act_ = 0;
        confirm_ = false;
    }

    void updateDetail() {
        App& a = app();
        Input& in = a.input;
        const int n = int(actions_.size());
        if (in.left()) { act_ = (act_ + n - 1) % n; confirm_ = false; sfxPlay(Sfx::Scroll); }
        if (in.right()) { act_ = (act_ + 1) % n; confirm_ = false; sfxPlay(Sfx::Scroll); }
        if (in.back()) {
            sfxPlay(Sfx::Cancel);
            detail_ = false;
            return;
        }
        if (!in.accept()) return;
        const Act act = actions_[size_t(act_)];
        if (act == Act::Back) {
            sfxPlay(Sfx::Cancel);
            detail_ = false;
            return;
        }
        if (act == Act::Remove && !confirm_) {  // ENTER twice: it deletes files
            confirm_ = true;
            sfxPlay(Sfx::Scroll);
            return;
        }
        sfxPlay(Sfx::Confirm);
        const PackStatus p = detailPack_;
        const std::string base = a.base;
        detail_ = false;
        if (act == Act::Install) startJob([this, p, base] { install(base, {p}, true); });
        else if (act == Act::Undo) startJob([this, p, base] { undo(base, p.folder); });
        else startJob([this, p, base] { remove(base, p.folder); });
    }

    void drawDetail() {
        const PackStatus& p = detailPack_;
        DrawRectangleRounded({100, 120, 1080, 480}, 0.05f, 8, Color{10, 6, 24, 220});
        gfx::text(FontId::Title, prettyName(p.folder), gfx::W / 2, 132, 34, gfx::rgb(0xFFE08A), Align::Center, 2);
        float y = 182;
        auto line = [&](const std::string& s, Color c, float size = 20) {
            if (y > 520) return;
            gfx::text(FontId::Ui, s.size() > 100 ? s.substr(0, 97) + "..." : s, 130, y, size, c);
            y += size + 6;
        };
        switch (p.kind) {
            case PackStatus::Changed:
                line("Changed on the release since it was installed: " + (p.diffKnown ? p.diff.summary() : std::string("a new version")),
                     gfx::rgb(0xFFD25A), 22);
                break;
            case PackStatus::Gone: line("No longer on the mods release.", gfx::rgb(0xFF7A7A), 22); break;
            case PackStatus::UpToDate: line("Installed, the same as on the release.", gfx::fade(WHITE, 0.85f), 22); break;
            case PackStatus::Unknown:
                line("Installed; not compared with the release" + (p.note.empty() ? std::string(".") : " (" + p.note + ")."),
                     gfx::fade(WHITE, 0.85f), 22);
                break;
            default: break;
        }
        if (p.legacy) line("Installed before the game kept a record of the packs' files: installing it again starts one.", gfx::fade(WHITE, 0.6f));
        auto names = [](const std::vector<std::string>& v) {
            std::string s;
            for (size_t i = 0; i < v.size() && i < 8; ++i) s += (i ? ", " : "") + prettyName(v[i]);
            if (v.size() > 8) s += " and " + std::to_string(v.size() - 8) + " more";
            return s;
        };
        if (p.diffKnown) {
            if (!p.diff.songsAdded.empty()) line("Songs added: " + names(p.diff.songsAdded), gfx::rgb(0x9CFF9C));
            if (!p.diff.songsRemoved.empty()) line("Songs removed: " + names(p.diff.songsRemoved), gfx::rgb(0xFF9C9C));
            int shown = 0;
            const int room = 9;
            for (const std::string& f : p.diff.added)
                if (shown++ < room) line("+ " + f, gfx::rgb(0x9CFF9C), 18);
            for (const std::string& f : p.diff.changed)
                if (shown++ < room) line("~ " + f, gfx::rgb(0xFFE08A), 18);
            for (const std::string& f : p.diff.removed)
                if (shown++ < room) line("- " + f, gfx::rgb(0xFF9C9C), 18);
            if (shown > room) line("... and " + std::to_string(shown - room) + " more files", gfx::fade(WHITE, 0.6f), 18);
        }
        y = std::max(y, 470.0f);
        if (undo_.exists)
            gfx::text(FontId::Ui, "Last change installed (" + dateOf(undo_.when) + "): " + undo_.summary + "  -  REMOVE CHANGES puts it back",
                      gfx::W / 2, 500, 17, gfx::fade(WHITE, 0.6f), Align::Center);
        float x = 0, total = 0;
        for (const Act a : actions_) total += gfx::measure(FontId::Ui, label(a), 26).x + 60;
        x = gfx::W / 2 - total / 2;
        for (size_t i = 0; i < actions_.size(); ++i) {
            std::string l = label(actions_[i]);
            if (actions_[i] == Act::Remove && confirm_ && int(i) == act_) l = "SURE? ENTER";
            const float w = gfx::measure(FontId::Ui, l, 26).x + 40;
            const bool on = int(i) == act_;
            DrawRectangleRounded({x, 536, w, 44}, 0.4f, 6, on ? Color{255, 210, 90, 220} : Color{255, 255, 255, 40});
            gfx::text(FontId::Ui, l, x + w / 2, 545, 26, on ? Color{20, 10, 30, 255} : gfx::fade(WHITE, 0.8f), Align::Center);
            x += w + 20;
        }
    }

    void startJob(std::function<void()> fn) {
        if (worker_.joinable()) worker_.join();
        {
            std::lock_guard<std::mutex> lk(sh_->m);
            sh_->busy = true;
        }
        worker_ = std::thread([this, fn] {
            fn();
            std::lock_guard<std::mutex> lk(sh_->m);
            sh_->busy = false;
            sh_->currentFile.clear();
            sh_->done = true;
        });
    }
    void setStatus(const std::string& s) {
        std::lock_guard<std::mutex> lk(sh_->m);
        sh_->status = s;
    }

    // A pack's zip, downloaded and checked against what GitHub says of it.
    bool fetchZip(const modsync::RemoteZip& z, const std::string& dest, std::string& err) {
        {
            std::lock_guard<std::mutex> lk(sh_->m);
            sh_->currentFile = dest + ".part";
        }
        if (!http_.get(z.url, dest, err)) return false;
        std::error_code ec;
        if (z.size && fileSizeOf(dest) != z.size) {
            err = "incomplete download";
            fs::remove(toPath(dest), ec);
            return false;
        }
        std::string hex;
        if (!z.sha256.empty() && (!sha256File(dest, hex) || hex != z.sha256)) {
            err = "damaged download (its SHA-256 differs from GitHub's)";
            fs::remove(toPath(dest), ec);
            return false;
        }
        return true;
    }

    // Downloads each pack's zips and installs them (a pack installed before
    // gets its changes; the last change can be removed again).
    void install(const std::string& base, const std::vector<PackStatus>& packs, bool update) {
        const std::string mods = base + "mods/", dl = mods + ".installed/dl/";
        int ok = 0, failed = 0;
        std::string last;
        for (const PackStatus& p : packs) {
            std::vector<std::string> paths;
            modsync::InstallOptions opt;
            opt.wholePack = true;
            opt.source = "pack";
            bool good = !p.zips.empty();
            std::string err;
            for (const modsync::RemoteZip& z : p.zips) {
                setStatus("Downloading " + z.name + "...");
                if (!fetchZip(z, dl + z.name, err)) {
                    good = false;
                    break;
                }
                paths.push_back(dl + z.name);
                opt.zipInfo.push_back({z.name, z.sha256, z.size});
            }
            modsync::Record before;
            const bool hadRecord = modsync::loadRecord(mods, p.folder, before);
            modsync::InstallResult res;
            if (good) {
                setStatus("Installing " + prettyName(p.folder) + "...");
                good = modsync::installZips(mods, paths, opt, res);
                err = res.message;
            }
            std::error_code ec;
            if (good) {
                // The zips are kept with the others installed (as the game always did); parts the pack no longer has go.
                for (const modsync::RemoteZip& z : p.zips) {
                    fs::remove(toPath(mods + ".installed/" + z.name), ec);
                    fs::rename(toPath(dl + z.name), toPath(mods + ".installed/" + z.name), ec);
                    if (ec) fs::remove(toPath(dl + z.name), ec);
                }
                if (hadRecord)
                    for (const modsync::ZipInfo& z : before.zips) {
                        bool still = false;
                        for (const modsync::RemoteZip& n : p.zips) still = still || n.name == z.name;
                        if (!still) fs::remove(toPath(mods + ".installed/" + z.name), ec);
                    }
                ++ok;
                last = update ? res.message : prettyName(p.folder) + " installed";
            } else {
                for (const std::string& path : paths) fs::remove(toPath(path), ec);
                ++failed;
                last = prettyName(p.folder) + ": " + (err.empty() ? "failed" : err);
            }
        }
        setStatus(packs.size() == 1 ? last
                                    : std::to_string(ok) + " pack(s) installed" + (failed ? ", " + std::to_string(failed) + " failed." : "."));
    }
    void undo(const std::string& base, const std::string& folder) {
        std::string msg;
        modsync::undoChanges(base + "mods/", folder, msg);
        setStatus(msg);
    }
    void remove(const std::string& base, const std::string& folder) {
        std::string msg;
        modsync::removeMod(base + "mods/", folder, msg);
        setStatus(msg);
    }

    std::shared_ptr<Shared> sh_ = std::make_shared<Shared>();
    net::Http http_{app().base};
    std::thread worker_;
    std::vector<PackStatus> rows_;
    int seenGeneration_ = -1;
    bool checking_ = false;
    int sel_ = 0;
    bool detail_ = false, confirm_ = false;
    PackStatus detailPack_;
    modsync::UndoInfo undo_;
    std::vector<Act> actions_;
    int act_ = 0;
};

std::unique_ptr<State> makeModPacks() { return std::make_unique<ModPacksState>(); }

}  // namespace gh
