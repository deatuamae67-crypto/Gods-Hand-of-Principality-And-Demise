// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through
// issues.

// JSON MERGING (issue #36), on the main menu when Options turns it on: the
// parts of a split chart - picked in the system's file manager - in a list
// the player puts in order (the game orders them by "song-1", "song-2", ...
// first), then merged into one Psych chart with no limit on its notes.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app.hpp"
#include "audio.hpp"
#include "common.hpp"
#include "editor_ui.hpp"
#include "gfx.hpp"
#include "json_merge.hpp"
#include "platform.hpp"
#include "raylib.h"

namespace gh {

namespace {

namespace fs = std::filesystem;
using namespace edui;
using gfx::fade;

const Color kGold{255, 222, 64, 255};
const Color kIvory{242, 244, 255, 255};
const Color kBad{255, 130, 130, 255};
const Color kGood{150, 230, 170, 255};

fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string textOf(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string sizeText(uint64_t b) {
    if (b >= 1000000000ull) return TextFormat("%.2f GB", double(b) / 1e9);
    if (b >= 1000000) return TextFormat("%.1f MB", double(b) / 1e6);
    return TextFormat("%.1f KB", double(b) / 1e3);
}

// 1234567 -> "1,234,567".
std::string grouped(uint64_t v) {
    std::string s = std::to_string(v), o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) o += ',';
        o += s[i];
    }
    return o;
}

std::string clock(double ms) {
    if (!(ms >= 0)) ms = 0;
    const long long t = (long long)std::llround(ms);
    return TextFormat("%lld:%02lld.%03lld", t / 60000, t / 1000 % 60, t % 1000);
}

void hints(const char* s) {
    if (app().touchMode) return;  // the on-screen buttons speak for themselves
    DrawRectangleRec({0, gfx::H - 40, gfx::W, 40}, Color{0, 0, 0, 150});
    gfx::text(FontId::Ui, s, gfx::W / 2, gfx::H - 33, 22, Color{220, 214, 235, 255}, Align::Center);
}

bool isSep(char c) { return c == '-' || c == '_' || c == ' ' || c == '.'; }

// The merged chart's name: the first part's without its number ("bopeebo-hard-2"
// -> "bopeebo-hard", "song part 3" -> "song"), beside it, never over a file
// that is there.
std::string outputFor(const std::vector<std::string>& parts) {
    const fs::path first = pathOf(parts.front());
    std::string stem = textOf(first.stem());
    size_t e = stem.size();
    while (e > 0 && std::isdigit(static_cast<unsigned char>(stem[e - 1]))) --e;
    if (e < stem.size()) {
        while (e > 0 && isSep(stem[e - 1])) --e;
        if (e >= 4 && toLower(stem.substr(e - 4, 4)) == "part") {
            e -= 4;
            while (e > 0 && isSep(stem[e - 1])) --e;
        }
        if (e > 0) stem.resize(e);
    }
    const fs::path dir = first.parent_path();
    std::error_code ec;
    fs::path out = dir / pathOf(stem + ".json");
    for (int k = 1; fs::exists(out, ec); ++k)
        out = dir / pathOf(stem + (k == 1 ? std::string("-merged") : "-merged-" + std::to_string(k)) + ".json");
    return textOf(out);
}

class JsonMergeState : public State {
public:
    ~JsonMergeState() override {
        stop_ = true;
        if (inspector_.joinable()) inspector_.join();
        if (merger_.joinable()) merger_.join();
    }
    void enter() override {
        // --state json-merge: the parts in merge/ (screenshots and tests).
        if (app().autoRun.state == "json-merge") addFolder();
    }
    void update(double) override {
        App& a = app();
        Input& in = a.input;
        runActions();
        feedUi();
        if (a.autoRun.mergeGo && !autoMerged_ && !rows_.empty() && !inspecting_.load() && !inspector_.joinable()) {
            autoMerged_ = true;
            merge();
        }
        pollAndroidPick();
        // The inspector finished a batch: start the next, if parts came meanwhile.
        if (!inspecting_.load() && inspector_.joinable()) {
            inspector_.join();
            inspectPending();
        }
        if (merging_.load()) {
            if (in.back()) stop_ = true;  // stops the merge; no file is left
            return;
        }
        if (merger_.joinable()) {
            merger_.join();
            stop_ = false;
            std::lock_guard<std::mutex> lk(mtx_);
            if (ok_) {
                sfxPlay(Sfx::Confirm);
                // A chart merged into the game's folders (a mod's data/) shows in Freeplay.
                if (out_.rfind(a.base, 0) == 0) a.library.scan(a.base);
            } else {
                sfxPlay(Sfx::Cancel);
            }
        }
        if (in.back()) {
            sfxPlay(Sfx::Cancel);
            a.change(makeMainMenu());
            return;
        }
        const bool shift = in.isDown(KEY_LEFT_SHIFT) || in.isDown(KEY_RIGHT_SHIFT);
        const int n = int(rows_.size());
        if (n > 0) {
            if (in.up() && !shift) select(sel_ - 1);
            if (in.down() && !shift) select(sel_ + 1);
            if ((in.up() && shift) || in.pressed(KEY_PAGE_UP)) move(-1);
            if ((in.down() && shift) || in.pressed(KEY_PAGE_DOWN)) move(+1);
            if (in.pressed(KEY_DELETE) || in.pressed(KEY_BACKSPACE)) remove();
        }
        if (in.pressed(KEY_A)) addParts();
        if (in.pressed(KEY_F)) addFolder();
        if (in.pressed(KEY_O)) orderByName();
        if (in.pressed(KEY_M)) cycleMode();
        if (in.pressed(KEY_C)) clearAll();
        if (in.accept() || in.pressed(KEY_G)) merge();
        mouse();
    }
    void draw() override {
        App& a = app();
        gfx::beginCanvas();
        drawMenuBackground(a.now, 0x5CA0FF, menuPulse());
        DrawRectangleRec({24, 20, gfx::W - 48, gfx::H - 70}, fade(BLACK, 0.62f));
        gfx::text(FontId::Title, "JSON MERGING", 44, 30, 34, kGold, Align::Left, 2.0f);
        gfx::text(FontId::Ui, "The parts of a split chart, merged into one. No note limit: parts of any size.", 360, 42, 18,
                  fade(kIvory, 0.8f));
        std::lock_guard<std::mutex> lk(mtx_);
        drawList();
        drawSide();
        ui_.used = false;
        hints(merging_.load() ? "Merging...   ESC stop"
                              : "A add parts   O order by name   SHIFT+UP/DOWN move   DEL remove   M mode   ENTER merge   ESC back");
        gfx::endCanvas();
    }
    TouchLayout touchLayout() const override {
        TouchLayout t;
        if (merging_.load()) {
            t.pad = false;
            t.keys = {{"STOP", KEY_ESCAPE}};
            return t;
        }
        t.keys = {{"ADD PARTS", KEY_A}, {"ORDER BY NAME", KEY_O}, {"MOVE UP", KEY_PAGE_UP}, {"MOVE DOWN", KEY_PAGE_DOWN},
                  {"REMOVE", KEY_DELETE}, {"MODE", KEY_M}, {"MERGE JSON", KEY_G}};
        return t;
    }

private:
    struct Row {
        uint64_t id = 0;
        MergePart part;
        bool inspected = false;
    };

    // ------------------------------------------------------------- list --
    static constexpr float kListX = 40, kListY = 120, kListW = 820, kRowH = 34;
    static constexpr int kVisible = 15;

    void select(int i) {
        if (rows_.empty()) return;
        sel_ = std::clamp(i, 0, int(rows_.size()) - 1);
        if (sel_ < scroll_) scroll_ = sel_;
        if (sel_ >= scroll_ + kVisible) scroll_ = sel_ - kVisible + 1;
        sfxPlay(Sfx::Scroll);
    }
    void move(int d) {
        std::lock_guard<std::mutex> lk(mtx_);
        const int to = sel_ + d;
        if (sel_ < 0 || to < 0 || to >= int(rows_.size())) return;
        std::swap(rows_[size_t(sel_)], rows_[size_t(to)]);
        sel_ = to;
        if (sel_ < scroll_) scroll_ = sel_;
        if (sel_ >= scroll_ + kVisible) scroll_ = sel_ - kVisible + 1;
        result_.clear();
        sfxPlay(Sfx::Scroll);
    }
    void remove() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (sel_ < 0 || sel_ >= int(rows_.size())) return;
        rows_.erase(rows_.begin() + sel_);
        sel_ = std::clamp(sel_, 0, std::max(0, int(rows_.size()) - 1));
        scroll_ = std::clamp(scroll_, 0, std::max(0, int(rows_.size()) - kVisible));
        result_.clear();
        sfxPlay(Sfx::Cancel);
    }
    void clearAll() {
        std::lock_guard<std::mutex> lk(mtx_);
        rows_.clear();
        sel_ = scroll_ = 0;
        result_.clear();
    }
    void orderByName() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<MergePart> parts;
        for (const Row& r : rows_) parts.push_back(r.part);
        orderParts(parts);
        std::vector<Row> sorted;
        for (const MergePart& p : parts)
            for (Row& r : rows_)
                if (r.id && r.part.path == p.path) {
                    sorted.push_back(std::move(r));
                    r.id = 0;
                    break;
                }
        rows_ = std::move(sorted);
        result_.clear();
        sfxPlay(Sfx::Confirm);
    }
    void cycleMode() {
        mode_ = MergeMode((int(mode_) + 1) % 3);
        result_.clear();
        sfxPlay(Sfx::Scroll);
    }

    // ------------------------------------------------------ adding parts --
    void addParts() {
        if (platform::touchFirst()) {  // Android: the picker copies them into merge/ (pollAndroidPick)
            platform::resetImport();
            picking_ = true;
            platform::pickFiles("Choose the parts of the chart", "*.json");
            note_ = "Choose the parts in the file manager...";
            return;
        }
        add(platform::pickFiles("Choose the parts of the chart", "*.json"));
    }
    void pollAndroidPick() {
        if (!picking_) return;
        int count = 0;
        const platform::Import st = platform::importStatus(&count);
        if (st == platform::Import::Copying) note_ = "Copying the parts into the game's folder...";
        if (st == platform::Import::Done || st == platform::Import::Nothing) {
            picking_ = false;
            platform::resetImport();
            note_.clear();
            if (st == platform::Import::Done) add(platform::pickedFiles());
        }
    }
    // Every chart in <game folder>/merge/: where Android copies the parts, and
    // where to put them when the system has no file dialog.
    void addFolder() {
        std::vector<std::string> files;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(pathOf(mergeFolder()), ec)) {
            const std::string p = textOf(e.path());
            if (e.is_regular_file(ec) && endsWithI(p, ".json")) files.push_back(p);
        }
        if (files.empty()) {
            note_ = "No .json files in " + platform::displayPath(mergeFolder());
            return;
        }
        add(files);
    }
    static std::string mergeFolder() {
        const std::string dir = app().base + "merge/";
        std::error_code ec;
        fs::create_directories(pathOf(dir), ec);
        return dir;
    }
    void add(const std::vector<std::string>& files) {
        if (files.empty()) return;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            const bool wasEmpty = rows_.empty();
            for (const std::string& f : files) {
                bool have = false;
                for (const Row& r : rows_) have = have || r.part.path == f;
                if (have) continue;
                Row r;
                r.id = ++nextId_;
                r.part.path = f;
                r.part.name = textOf(pathOf(f).filename());
                rows_.push_back(std::move(r));
            }
            // The game's first guess at the order: "song-1", "song-2", ..., "song-10".
            if (wasEmpty) {
                std::vector<MergePart> parts;
                for (const Row& r : rows_) parts.push_back(r.part);
                orderParts(parts);
                std::vector<Row> sorted;
                for (const MergePart& p : parts)
                    for (Row& r : rows_)
                        if (r.id && r.part.path == p.path) {
                            sorted.push_back(std::move(r));
                            r.id = 0;
                            break;
                        }
                rows_ = std::move(sorted);
            }
            result_.clear();
            note_.clear();
        }
        sfxPlay(Sfx::Confirm);
        inspectPending();
    }
    // Reads the parts not read yet, one after another, in the background.
    void inspectPending() {
        if (inspecting_.load() || inspector_.joinable()) return;
        std::vector<std::pair<uint64_t, std::string>> todo;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (const Row& r : rows_)
                if (!r.inspected) todo.emplace_back(r.id, r.part.path);
        }
        if (todo.empty()) return;
        inspecting_ = true;
        inspector_ = std::thread([this, todo] {
            for (const auto& [id, path] : todo) {
                if (stop_.load()) break;
                MergePart p = inspectPart(path);
                std::lock_guard<std::mutex> lk(mtx_);
                for (Row& r : rows_)
                    if (r.id == id) {
                        r.part = std::move(p);
                        r.inspected = true;
                    }
            }
            inspecting_ = false;
        });
    }

    // ----------------------------------------------------------- merging --
    void merge() {
        std::vector<std::string> paths;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (rows_.empty()) {
                note_ = "Add the parts first (A).";
                return;
            }
            for (const Row& r : rows_) {
                if (!r.inspected) {
                    note_ = "Still reading the parts...";
                    return;
                }
                if (!r.part.ok) {
                    note_ = r.part.name + " is not a chart: remove it (DEL).";
                    return;
                }
                paths.push_back(r.part.path);
            }
            out_ = outputFor(paths);
            result_.clear();
            note_.clear();
        }
        sfxPlay(Sfx::Confirm);
        progress_ = 0;
        stop_ = false;
        merging_ = true;
        const MergeMode mode = mode_;
        const std::string out = out_;
        merger_ = std::thread([this, paths, mode, out] {
            MergeResult res;
            std::string err;
            const bool ok = mergeChartParts(paths, out, mode, res, err, &progress_, &stop_);
            std::lock_guard<std::mutex> lk(mtx_);
            ok_ = ok;
            res_ = res;
            result_ = ok ? "Merged into " + textOf(pathOf(out).filename()) : "Not merged: " + err;
            where_ = ok ? "in " + platform::displayPath(textOf(pathOf(out).parent_path())) : std::string();
            merging_ = false;
        });
    }
    std::string detected() const {
        std::vector<MergePart> parts;
        for (const Row& r : rows_) {
            if (!r.inspected || !r.part.ok) return {};
            parts.push_back(r.part);
        }
        if (parts.empty()) return {};
        return detectMergeMode(parts) == MergeMode::Overlay ? "Overlay" : "Append";
    }

    // ------------------------------------------------------------- mouse --
    void feedUi() {
        App& a = app();
        ui_.in = &a.input;
        ui_.mouse = gfx::toCanvas(GetMousePosition());
        ui_.click = a.input.mousePressed(MOUSE_BUTTON_LEFT) && !(a.touchMode && a.touch.covers(GetMousePosition()));
        ui_.wheel = a.input.wheel();
    }
    // A click picks a part; dragging it moves it; the wheel scrolls.
    void mouse() {
        const Rectangle list{kListX, kListY, kListW, kRowH * kVisible};
        const bool over = CheckCollisionPointRec(ui_.mouse, list);
        if (over && ui_.wheel != 0) {
            std::lock_guard<std::mutex> lk(mtx_);
            scroll_ = std::clamp(scroll_ - int(ui_.wheel), 0, std::max(0, int(rows_.size()) - kVisible));
        }
        const int at = scroll_ + int((ui_.mouse.y - kListY) / kRowH);
        if (over && ui_.click && !ui_.used) {
            std::lock_guard<std::mutex> lk(mtx_);
            if (at >= 0 && at < int(rows_.size())) {
                sel_ = at;
                dragging_ = true;
            }
        }
        if (dragging_) {
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                dragging_ = false;
            } else if (at != sel_ && at >= 0) {
                int to;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    to = std::clamp(at, 0, int(rows_.size()) - 1);
                }
                if (to != sel_) move(to > sel_ ? 1 : -1);
            }
        }
    }

    // -------------------------------------------------------------- draw --
    void drawList() {
        gfx::text(FontId::Ui, "#", kListX + 8, kListY - 24, 16, fade(kIvory, 0.55f));
        gfx::text(FontId::Ui, "PART", kListX + 44, kListY - 24, 16, fade(kIvory, 0.55f));
        gfx::text(FontId::Ui, "NOTES", kListX + 470, kListY - 24, 16, fade(kIvory, 0.55f), Align::Right);
        gfx::text(FontId::Ui, "SECTIONS", kListX + 570, kListY - 24, 16, fade(kIvory, 0.55f), Align::Right);
        gfx::text(FontId::Ui, "NOTES FROM - TO", kListX + 600, kListY - 24, 16, fade(kIvory, 0.55f));
        DrawRectangleRec({kListX, kListY, kListW, kRowH * kVisible}, fade(BLACK, 0.35f));
        if (rows_.empty()) {
            gfx::text(FontId::Ui, "No parts yet.", kListX + 20, kListY + 16, 22, kIvory);
            gfx::text(FontId::Ui, "ADD PARTS (A) opens the file manager: choose every part of the chart at once.", kListX + 20,
                      kListY + 52, 18, fade(kIvory, 0.75f));
            gfx::text(FontId::Ui, "The game puts them in order by their names (song-1, song-2, ...); move them as you like.",
                      kListX + 20, kListY + 80, 18, fade(kIvory, 0.75f));
            gfx::text(FontId::Ui, "No file manager? Put them in " + platform::displayPath(app().base + "merge/") + " and press F.",
                      kListX + 20, kListY + 108, 18, fade(kIvory, 0.75f));
        }
        uint64_t total = 0;
        for (int i = scroll_; i < std::min(int(rows_.size()), scroll_ + kVisible); ++i) {
            const Row& r = rows_[size_t(i)];
            const float y = kListY + float(i - scroll_) * kRowH;
            const bool on = i == sel_;
            if (on) DrawRectangleRec({kListX, y, kListW, kRowH}, fade(kGold, dragging_ ? 0.3f : 0.18f));
            const float ty = y + (kRowH - 19) * 0.5f;
            gfx::text(FontId::Ui, std::to_string(i + 1), kListX + 8, ty, 19, on ? kGold : fade(kIvory, 0.7f));
            std::string name = r.part.name;
            if (name.size() > 34) name = name.substr(0, 32) + "...";
            gfx::text(FontId::Ui, name, kListX + 44, ty, 19, on ? kGold : kIvory);
            if (!r.inspected) {
                gfx::text(FontId::Ui, "reading...", kListX + 470, ty, 17, fade(kIvory, 0.6f), Align::Right);
            } else if (!r.part.ok) {
                gfx::text(FontId::Ui, "not a chart: " + r.part.err, kListX + 300, ty, 17, kBad);
            } else {
                gfx::text(FontId::Ui, grouped(r.part.notes), kListX + 470, ty, 19, kIvory, Align::Right);
                gfx::text(FontId::Ui, std::to_string(r.part.sections), kListX + 570, ty, 19, fade(kIvory, 0.85f), Align::Right);
                gfx::text(FontId::Ui, r.part.notes ? clock(r.part.firstMs) + " - " + clock(r.part.lastMs) : "-", kListX + 600, ty, 17,
                          fade(kIvory, 0.75f));
            }
        }
        for (const Row& r : rows_) total += r.part.notes;
        if (int(rows_.size()) > kVisible) {
            const float h = kRowH * kVisible, bar = std::max(24.0f, h * float(kVisible) / float(rows_.size()));
            const float y = kListY + (h - bar) * float(scroll_) / float(std::max(1, int(rows_.size()) - kVisible));
            DrawRectangleRec({kListX + kListW - 6, y, 4, bar}, fade(kIvory, 0.5f));
        }
        if (!rows_.empty())
            gfx::text(FontId::Ui, TextFormat("%d parts, %s notes", int(rows_.size()), grouped(total).c_str()), kListX,
                      kListY + kRowH * kVisible + 10, 19, fade(kIvory, 0.85f));
    }
    void drawSide() {
        const float x = kListX + kListW + 24, w = gfx::W - 40 - x;
        float y = kListY - 4;
        const bool busy = merging_.load() || picking_;
        auto btn = [&](const char* label, bool enabled = true) {
            const Rectangle r{x, y, w, 36};
            y += 44;
            if (!enabled || busy) {
                ui_.box(r);
                gfx::text(FontId::Ui, label, r.x + r.width * 0.5f, r.y + 9, 18, fade(kIvory, 0.35f), Align::Center);
                return false;
            }
            return ui_.button(r, label);
        };
        // Buttons act after this frame's drawing: the lock is held while drawing.
        if (btn("ADD PARTS...  (A)")) pending_ = KEY_A;
        if (btn("ORDER BY NAME  (O)", rows_.size() > 1)) pending_ = KEY_O;
        if (btn("MOVE UP  (SHIFT+UP)", sel_ > 0)) pending_ = KEY_PAGE_UP;
        if (btn("MOVE DOWN  (SHIFT+DOWN)", sel_ + 1 < int(rows_.size()))) pending_ = KEY_PAGE_DOWN;
        if (btn("REMOVE  (DEL)", !rows_.empty())) pending_ = KEY_DELETE;
        const std::string d = detected();
        const std::string modeLabel = mode_ == MergeMode::Auto ? "MODE: AUTO" + (d.empty() ? std::string() : " (" + d + ")")
                                      : mode_ == MergeMode::Overlay ? std::string("MODE: OVERLAY")
                                                                    : std::string("MODE: APPEND");
        if (btn((modeLabel + "  (M)").c_str())) pending_ = KEY_M;
        gfx::text(FontId::Ui,
                  mode_ == MergeMode::Overlay  ? "Every part is the whole song with some of its notes."
                  : mode_ == MergeMode::Append ? "Each part goes on after the one before."
                                               : "The game tells from the parts.",
                  x, y - 4, 15, fade(kIvory, 0.7f));
        y += 22;
        const Rectangle go{x, y, w, 52};
        const bool can = !rows_.empty() && !busy;
        if (can) {
            const bool h = ui_.hover(go);
            DrawRectangleRounded(go, 0.25f, 6, h ? Color{90, 160, 90, 255} : Color{60, 120, 60, 255});
            DrawRectangleRoundedLinesEx(go, 0.25f, 6, 2.0f, kGold);
            if (h && ui_.click) {
                ui_.used = true;
                pending_ = KEY_G;
            }
        } else {
            ui_.box(go);
        }
        gfx::text(FontId::Title, "MERGE JSON", go.x + go.width * 0.5f, go.y + 10, 30, can ? WHITE : fade(kIvory, 0.35f),
                  Align::Center, 2.0f);
        y += 66;
        if (merging_.load()) {
            const float f = std::clamp(progress_.load(), 0.0f, 1.0f);
            DrawRectangleRec({x, y, w, 18}, fade(BLACK, 0.5f));
            DrawRectangleRec({x, y, w * f, 18}, kGold);
            gfx::text(FontId::Ui, TextFormat("%s  %d%%", f < 0.6f ? "Reading the parts" : "Writing the chart", int(f * 100)), x, y + 24,
                      17, kIvory);
        } else if (!result_.empty()) {
            const Color c = ok_ ? kGood : kBad;
            float ty = y;
            for (const std::string& line : wrap(result_, 34)) {
                gfx::text(FontId::Ui, line, x, ty, 17, c);
                ty += 21;
            }
            if (ok_) {
                for (const std::string& line : wrap(where_, 38)) {
                    gfx::text(FontId::Ui, line, x, ty, 15, fade(kIvory, 0.6f));
                    ty += 18;
                }
                gfx::text(FontId::Ui, grouped(res_.notes) + " notes, " + std::to_string(res_.sections) + " sections", x, ty + 4, 17, kIvory);
                gfx::text(FontId::Ui, std::to_string(res_.events) + " events, " + sizeText(res_.bytes), x, ty + 25, 17, kIvory);
                gfx::text(FontId::Ui, TextFormat("%s, in %.1f s", res_.mode == MergeMode::Overlay ? "overlaid" : "one after another",
                                                 res_.seconds),
                          x, ty + 46, 17, fade(kIvory, 0.75f));
            }
        }
        if (!note_.empty()) {
            float ty = gfx::H - 120;
            for (const std::string& line : wrap(note_, 34)) {
                gfx::text(FontId::Ui, line, x, ty, 17, kGold);
                ty += 21;
            }
        }
        // What a button asked for, as its key would.
        if (pending_) {
            const int k = pending_;
            pending_ = 0;
            actions_.push_back(k);
        }
    }
    std::vector<std::string> wrap(const std::string& s, size_t width) const {
        std::vector<std::string> lines;
        std::string cur;
        size_t b = 0;
        while (b < s.size()) {
            size_t e = s.find(' ', b);
            if (e == std::string::npos) e = s.size();
            std::string word = s.substr(b, e - b);
            while (word.size() > width) {  // a long path: cut it
                if (!cur.empty()) lines.push_back(cur), cur.clear();
                lines.push_back(word.substr(0, width));
                word = word.substr(width);
            }
            if (!cur.empty() && cur.size() + 1 + word.size() > width) lines.push_back(cur), cur.clear();
            cur += (cur.empty() ? "" : " ") + word;
            b = e + 1;
        }
        if (!cur.empty()) lines.push_back(cur);
        return lines;
    }

    // Clicked buttons, done at the start of the next update, as their keys.
    void runActions() {
        std::vector<int> act;
        act.swap(actions_);
        for (int k : act) {
            switch (k) {
                case KEY_A: addParts(); break;
                case KEY_O: orderByName(); break;
                case KEY_PAGE_UP: move(-1); break;
                case KEY_PAGE_DOWN: move(+1); break;
                case KEY_DELETE: remove(); break;
                case KEY_M: cycleMode(); break;
                case KEY_G: merge(); break;
                default: break;
            }
        }
    }

private:
    std::vector<Row> rows_;
    uint64_t nextId_ = 0;
    int sel_ = 0, scroll_ = 0;
    bool dragging_ = false, picking_ = false, autoMerged_ = false;
    MergeMode mode_ = MergeMode::Auto;
    std::string note_, result_, where_, out_;
    bool ok_ = false;
    MergeResult res_;
    Ui ui_;
    int pending_ = 0;
    std::vector<int> actions_;
    mutable std::mutex mtx_;
    std::thread inspector_, merger_;
    std::atomic<bool> inspecting_{false}, merging_{false}, stop_{false};
    std::atomic<float> progress_{0};
};

}  // namespace

std::unique_ptr<State> makeJsonMerge() { return std::make_unique<JsonMergeState>(); }

}  // namespace gh
