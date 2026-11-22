// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Title, main menu, story, freeplay and credits screens.
#include <atomic>
#include <cctype>
#include <cstring>
#include <map>
#include <cmath>
#include <mutex>
#include <thread>

#include "app.hpp"
#include "characters.hpp"
#include "common.hpp"
#include "editor_ui.hpp"
#include "gfx.hpp"
#include "platform.hpp"
#include "psych_menus.hpp"
#include "raylib.h"
#include "song_icons.hpp"
#include "video.hpp"

#ifndef GH_VERSION
#define GH_VERSION "dev"  // set by CMake from project(VERSION)
#endif

namespace gh {

using gfx::Align;
using gfx::FontId;
using gfx::fade;

namespace {

const Color kGold{255, 222, 64, 255};   // the accent: a bright, cartoon yellow
const Color kIvory{242, 244, 255, 255};  // text: a cool white

Color diffColor(const std::string& d) {
    const std::string l = toLower(d);
    if (l == "easy") return Color{120, 220, 140, 255};
    if (l == "normal") return Color{255, 214, 100, 255};
    if (l == "hard") return Color{255, 120, 90, 255};
    if (l == "infernal") return Color{255, 50, 70, 255};
    return Color{190, 160, 255, 255};
}

void hints(const char* s) {
    if (app().touchMode) return;  // the on-screen buttons speak for themselves
    DrawRectangleRec({0, gfx::H - 40, gfx::W, 40}, Color{0, 0, 0, 150});
    gfx::text(FontId::Ui, s, gfx::W / 2, gfx::H - 33, 22, Color{220, 214, 235, 255}, Align::Center);
}

double lastBeatTime() {
    const double b = app().menuBeat();
    const double frac = b - std::floor(b);
    return app().now - frac * 60.0 / app().menuBpm();
}

std::vector<std::string> wrap(const std::string& s, size_t width) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flush = [&] {
        if (cur.size() + word.size() + (cur.empty() ? 0 : 1) > width && !cur.empty()) {
            lines.push_back(cur);
            cur.clear();
        }
        if (!word.empty()) cur += (cur.empty() ? "" : " ") + word;
        word.clear();
    };
    for (char c : s) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// Background jobs that must not block the UI (or the exit) while composing.
std::atomic<int> g_jobs{0};
struct PcmJob {
    struct Slot {
        std::mutex m;
        bool ready = false;
        PcmPtr pcm;
    };
    std::shared_ptr<Slot> slot;
    template <class F> void start(F fn) {
        slot = std::make_shared<Slot>();
        auto s = slot;
        ++g_jobs;
        std::thread([s, fn] {
            PcmPtr r = fn();
            {
                std::lock_guard<std::mutex> lk(s->m);
                s->pcm = r;
                s->ready = true;
            }
            --g_jobs;
        }).detach();
    }
    bool running() const { return slot != nullptr; }
    bool ready() const {
        if (!slot) return false;
        std::lock_guard<std::mutex> lk(slot->m);
        return slot->ready;
    }
    PcmPtr take() {
        PcmPtr r;
        if (slot) {
            std::lock_guard<std::mutex> lk(slot->m);
            r = slot->pcm;
        }
        slot.reset();
        return r;
    }
};

}  // namespace

void waitForBackgroundJobs() {
    for (int i = 0; i < 600 && g_jobs.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// ------------------------------------------------------------------ Title --

class TitleState : public State {
public:
    TouchLayout touchLayout() const override {
        TouchLayout t;
        t.pad = false;
        t.tapCode = KEY_ENTER;
        return t;
    }
    void update(double) override {
        App& a = app();
        flash_ = std::max(0.0f, flash_ - float(a.dt) * 1.8f);
        if (!leaving_ && (a.input.accept() || a.input.firstPressed() != 0) && !a.input.pressed(KEY_F11)) {
            leaving_ = true;
            leaveAt_ = a.now;
            flash_ = 1.0f;
            sfxPlay(Sfx::Confirm);
        }
        if (leaving_ && a.now - leaveAt_ > 0.75) a.change(makeMainMenu());
    }
    void draw() override {
        if (pm::on()) {
            drawPsych();
            return;
        }
        App& a = app();
        const double now = a.now;
        const float pulse = menuPulse();
        gfx::beginCanvas();
        drawMenuBackground(now, 0xF4C76B, pulse);
        CharacterState p;
        p.id = CharId::Principality;
        p.beatAt = lastBeatTime();
        drawCharacter(p, {230, 690}, 0.9f, now, false);
        CharacterState h;
        h.id = CharId::Herald;
        h.beatAt = p.beatAt;
        drawCharacter(h, {1060, 690}, 0.95f, now, true);
        const float s = 1.0f + 0.025f * pulse;
        gfx::glowText(FontId::Title, "GOD'S HAND", gfx::W / 2, 150 - 6 * s, 104 * s, kGold, Color{255, 190, 80, 255});
        gfx::text(FontId::Title, "of Principality and Demise", gfx::W / 2, 286, 38, kIvory, Align::Center, 2.0f);
        const float blink = leaving_ ? (std::fmod(float(now) * 12.0f, 2.0f) < 1.0f ? 1.0f : 0.2f)
                                     : 0.55f + 0.45f * std::sin(float(now) * 3.5f);
        gfx::text(FontId::Ui, a.touchMode ? "TAP TO BEGIN" : "PRESS ENTER TO BEGIN", gfx::W / 2, 470, 40, fade(WHITE, blink),
                  Align::Center, 2.0f);
        gfx::text(FontId::Ui, "A rhythm duel  -  v" GH_VERSION, gfx::W / 2, 680, 22, fade(kIvory, 0.7f), Align::Center, 1.0f);
        if (!a.mixer.audioOk())
            gfx::text(FontId::Ui, "No audio device found - the game will run silently", gfx::W / 2, 520, 22,
                      Color{255, 160, 140, 255}, Align::Center, 1.5f);
        if (flash_ > 0) DrawRectangleRec({0, 0, gfx::W, gfx::H}, fade(WHITE, flash_ * 0.8f));
        gfx::endCanvas();
    }

private:
    // Psych's TitleState: on black, the bumping logo (logoBumpin, at -150,
    // -100), GF dancing (gfDanceTitle, at 512, 40: left and right on
    // alternate beats) and "Press Enter to Begin" (titleEnter, at 100, 576),
    // its colour swaying between cyan and deep blue until it is pressed.
    void drawPsych() {
        App& a = app();
        const double now = a.now;
        const pm::Assets& pa = pm::assets();
        const double beat = a.menuBeat(), sinceBeat = std::max(0.0, now - lastBeatTime());
        gfx::beginCanvas();
        DrawRectangleRec({0, 0, gfx::W, gfx::H}, BLACK);
        if (pa.gfTitle.ok()) {
            const std::vector<int> all = pa.gfTitle.anim("gfDance");
            const bool left = (long long)std::floor(beat) % 2 == 0;
            std::vector<int> idx;  // Psych's indices: left 30, 0-14; right 15-29
            if (left) idx.push_back(30);
            for (int i = left ? 0 : 15; i <= (left ? 14 : 29); ++i) idx.push_back(i);
            const int k = idx[size_t(std::min(int(sinceBeat * 24.0), int(idx.size()) - 1))];
            if (!all.empty()) pa.gfTitle.draw(all[size_t(std::min(k, int(all.size()) - 1))], {512, 40}, 1.0f, WHITE);
        }
        if (pa.logo.ok()) {
            const int f = pa.logo.frameAt(pa.logo.anim("logo bumpin"), sinceBeat, false);
            pa.logo.draw(f, {-150, -100}, 1.0f, WHITE);
        } else {
            const float s = 1.0f + 0.06f * menuPulse();
            gfx::text(FontId::Title, "GOD'S HAND", 330, 210 - 50 * s, 110 * s, WHITE, Align::Center, 9.0f, BLACK);
            gfx::text(FontId::Title, "of Principality and Demise", 330, 300, 34, Color{255, 196, 56, 255}, Align::Center, 4.0f, BLACK);
        }
        if (pa.enter.ok()) {
            std::vector<int> f = pa.enter.anim(leaving_ ? "ENTER PRESSED" : "ENTER IDLE");
            if (f.empty() && !leaving_) f = pa.enter.anim("Press Enter to Begin");
            Color c = WHITE;
            if (!leaving_) {  // Psych 0.7's new title: the sway, two seconds back and forth
                double tt = std::fmod(now, 2.0);
                tt = tt <= 1.0 ? tt : 2.0 - tt;
                const float q = float(tt < 0.5 ? 2 * tt * tt : 1 - std::pow(-2 * tt + 2, 2) / 2);
                c = gfx::fade(gfx::mix(Color{0x33, 0xFF, 0xFF, 255}, Color{0x33, 0x33, 0xCC, 255}, q), 1.0f - 0.36f * q);
            }
            pa.enter.draw(pa.enter.frameAt(f, now - (leaving_ ? leaveAt_ : 0.0)), {100, 576}, 1.0f, c);
        } else {
            const float blink = leaving_ ? (std::fmod(float(now) * 12.0f, 2.0f) < 1.0f ? 1.0f : 0.2f) : 1.0f;
            gfx::text(FontId::Title, a.touchMode ? "TAP TO BEGIN" : "PRESS ENTER TO BEGIN", gfx::W / 2, 600, 56,
                      fade(leaving_ ? WHITE : Color{0x33, 0xFF, 0xFF, 255}, blink), Align::Center, 5.0f, BLACK);
        }
        if (flash_ > 0) DrawRectangleRec({0, 0, gfx::W, gfx::H}, fade(WHITE, flash_));
        gfx::endCanvas();
    }

    bool leaving_ = false;
    double leaveAt_ = 0;
    float flash_ = 0;
};

// -------------------------------------------------------------- Main menu --

// RENDER MODE and AUTOCHART open the song list for recording a video or
// for playing a chart made from the song's audio. AUTOCHART and JSON MERGING
// are there only when they are turned on in Options. UPDATE appears when a
// newer version is downloaded (Options: Auto-update), MOD PACKS when packs
// you installed changed on the release.
enum class MainItem { Story, Freeplay, Render, AutoChart, JsonMerge, ModPacks, Update, Options, Credits, Quit };
struct MainEntry {
    MainItem id;
    const char* label;
    const char* psych;  // Psych's picture for it (mainmenu/menu_<name>); the others are lettered
};
std::vector<MainEntry> mainItems() {
    const Settings& s = app().settings;
    std::vector<MainEntry> v = {{MainItem::Story, "STORY MODE", "story_mode"},
                                {MainItem::Freeplay, "FREEPLAY", "freeplay"},
                                {MainItem::Render, "RENDER MODE", ""}};
    if (s.autoChart) v.push_back({MainItem::AutoChart, "AUTOCHART", ""});
    if (s.jsonMerging) v.push_back({MainItem::JsonMerge, "JSON MERGING", ""});
    bool packsChanged;
    {
        std::lock_guard<std::mutex> lk(app().packCheck->m);
        packsChanged = app().packCheck->changed() > 0;
    }
    if (packsChanged) v.push_back({MainItem::ModPacks, "MOD PACKS", ""});
    if (app().updater.status().stage == update::Stage::Ready) v.push_back({MainItem::Update, "UPDATE", ""});
    v.push_back({MainItem::Options, "OPTIONS", "options"});
    v.push_back({MainItem::Credits, "CREDITS", "credits"});
    v.push_back({MainItem::Quit, "QUIT", ""});
    return v;
}

class MainMenuState : public State {
public:
    void enter() override {
        items_ = mainItems();
        n_ = int(items_.size());
        app().mainSel = clampv(app().mainSel, 0, n_ - 1);
        anim_ = float(app().mainSel);
    }
    void update(double dt) override {
        App& a = app();
        int& sel = a.mainSel;
        if (chosen_ < 0 && a.now - itemsAt_ > 0.5) {
            // UPDATE and MOD PACKS come when the checks at start find something.
            itemsAt_ = a.now;
            std::vector<MainEntry> fresh = mainItems();
            if (fresh.size() != items_.size()) {
                const MainItem was = items_[size_t(clampv(sel, 0, n_ - 1))].id;
                items_ = std::move(fresh);
                n_ = int(items_.size());
                sel = 0;
                for (int i = 0; i < n_; ++i)
                    if (items_[size_t(i)].id == was) sel = i;
            }
        }
        anim_ = approach(anim_, float(sel), 14.0f, float(dt));
        // Psych's camera follows the chosen item (JS Engine: lerp 7.5 a second).
        camY_ = camY_ < -1e8f ? psychCamTarget(sel) : approach(camY_, psychCamTarget(sel), 7.5f, float(dt));
        if (chosen_ >= 0) {
            // Psych: the chosen item flickers for a second before its menu opens.
            const MainItem id = items_[size_t(chosen_)].id;
            if (a.now - chosenAt_ > (pm::on() && id != MainItem::Quit ? 1.0 : 0.55)) {
                switch (id) {
                    case MainItem::Story: a.change(makeStory()); break;
                    case MainItem::Freeplay:
                    case MainItem::Render:
                    case MainItem::AutoChart:
                        a.songListMode = id == MainItem::Render      ? App::SongListMode::Render
                                         : id == MainItem::AutoChart ? App::SongListMode::AutoChart
                                                                     : App::SongListMode::Play;
                        a.change(makeFreeplay());
                        break;
                    case MainItem::JsonMerge: a.change(makeJsonMerge()); break;
                    case MainItem::ModPacks: a.change(makeModPacks()); break;
                    case MainItem::Update:
                        if (platform::kAndroid) {
                            // The system installer, with the APK the updater downloaded.
                            const int r = platform::installApk(a.updater.status().file);
                            a.notify(r == 1   ? "The installer is open: confirm the update there."
                                     : r == 2 ? "Allow installing apps from God's Hand, then choose UPDATE again."
                                              : "The installer could not be opened.",
                                     10.0);
                            chosen_ = -1;
                        } else {
                            a.restartAfterQuit = true;  // the game starts again and the update takes its place
                            a.quit = true;
                        }
                        break;
                    case MainItem::Options: a.change(makeOptions()); break;
                    case MainItem::Credits: a.change(makeCredits()); break;
                    default: a.quit = true; break;
                }
            }
            return;
        }
        if (a.input.up()) { sel = (sel + n_ - 1) % n_; sfxPlay(Sfx::Scroll); }
        if (a.input.down()) { sel = (sel + 1) % n_; sfxPlay(Sfx::Scroll); }
        if (a.input.accept()) {
            chosen_ = sel;
            chosenAt_ = a.now;
            sfxPlay(items_[size_t(sel)].id == MainItem::Quit ? Sfx::Cancel : Sfx::Confirm);
        }
        if (a.input.back()) { sfxPlay(Sfx::Cancel); a.change(makeTitle()); }
        // 7: the editors (chart, character, stage, events), as Psych's Master Editor Menu.
        if (a.input.pressed(KEY_SEVEN) || a.input.pressed(KEY_KP_7)) {
            sfxPlay(Sfx::Confirm);
            a.change(makeEditorMenu());
        }
    }
    void draw() override {
        if (pm::on()) {
            drawPsych();
            return;
        }
        App& a = app();
        const double now = a.now;
        const float pulse = menuPulse();
        gfx::beginCanvas();
        drawMenuBackground(now, 0x8C6CFF, pulse);
        CharacterState h;
        h.id = CharId::Herald;
        h.beatAt = lastBeatTime();
        gfx::glow({1080, 500}, 230 + 20 * pulse, fade(kGold, 0.3f));
        drawCharacter(h, {1080, 640}, 0.85f, now, true);
        gfx::text(FontId::Title, "GOD'S HAND", 60, 40, 44, kGold, Align::Left, 2.0f);
        // 70 pixels apart, closer when there are more items than fit.
        const float gap = std::min(70.0f, 460.0f / float(std::max(1, n_ - 1)));
        for (int i = 0; i < n_; ++i) {
            const char* label = items_[size_t(i)].label;
            const bool on = i == a.mainSel;
            const float y = 128.0f + i * gap - (on ? 6.0f : 0.0f);
            float size = on ? 56.0f + 4.0f * pulse : 42.0f;
            Color c = on ? kGold : fade(kIvory, 0.55f);
            if (chosen_ >= 0 && on && std::fmod(float(now - chosenAt_) * 10.0f, 2.0f) < 1.0f) c = WHITE;
            if (chosen_ >= 0 && !on) c = fade(c, std::max(0.0f, 1.0f - float(now - chosenAt_) * 3.0f));
            if (on) gfx::glowText(FontId::Title, label, gfx::W / 2, y, size, c, Color{255, 180, 80, 255});
            else gfx::text(FontId::Title, label, gfx::W / 2, y + 6, size, c, Align::Center, 2.0f);
            if (on) {
                const float w = gfx::measure(FontId::Title, label, size).x * 0.5f + 40 + 6 * pulse;
                gfx::drawNote(gfx::kRight, {gfx::W / 2 - w, y + size * 0.55f}, 44);
                gfx::drawNote(gfx::kLeft, {gfx::W / 2 + w, y + size * 0.55f}, 44);
            }
        }
        hints("UP/DOWN select   ENTER confirm   7 editors   ESC back   F11 fullscreen");
        gfx::text(FontId::Ui, a.library.report(), 16, gfx::H - 70, 20, fade(kIvory, 0.6f));
        drawUpdateLine();
        gfx::endCanvas();
    }

private:
    // Psych 0.7's MainMenuState (JS Engine's, which keeps every item in the
    // list): items 140 pixels apart from 108 - (n - 4) x 80, centred, and a
    // camera that follows the chosen one, the picture behind at a tenth of its
    // speed (scroll factor 0.25 - 0.05 x (n - 4)), the items at 0.135 x (n - 4).
    float psychItemY(int i) const { return float(i) * 140.0f + 108.0f - float(std::max(n_, 4) - 4) * 80.0f; }
    float psychCamTarget(int sel) const {
        const Vector2 sz = pm::menuItemSize(items_[size_t(sel)].psych, items_[size_t(sel)].label);
        return psychItemY(sel) + sz.y * 0.5f - (n_ > 4 ? float(n_) * 8.0f : 0.0f);
    }
    void drawPsych() {
        App& a = app();
        const double now = a.now;
        gfx::beginCanvas();
        DrawRectangleRec({0, 0, gfx::W, gfx::H}, BLACK);
        const float scroll = camY_ - gfx::H * 0.5f;  // the camera's scroll
        const float bgFactor = std::max(0.25f - 0.05f * float(n_ - 4), 0.1f);
        const float itemFactor = n_ >= 6 ? float(n_ - 4) * 0.135f : 0.0f;
        const auto bg = [&] { pm::background(pm::kMenuBG, WHITE, scroll * bgFactor, 1.175f); };
        if (a.settings.menuBlur) gfx::drawBlurred(bg);
        else bg();
        const double since = now - chosenAt_;
        // Confirming: the magenta picture flickers (Flashing lights), the
        // chosen item too, faster, and the others fade away.
        if (chosen_ >= 0 && a.settings.flashing && since < 1.1 && std::fmod(since, 0.30) < 0.15)
            pm::background(pm::kDesat, Color{0xFD, 0x71, 0x9B, 255}, scroll * bgFactor, 1.175f);
        for (int i = 0; i < n_; ++i) {
            const MainEntry& e = items_[size_t(i)];
            const bool on = i == a.mainSel;
            float alpha = 1.0f;
            if (chosen_ >= 0 && !on) alpha = std::max(0.0f, 1.0f - float(since) / 0.4f);
            if (chosen_ >= 0 && on && since < 1.0 && std::fmod(since, 0.12) >= 0.06) alpha = 0.0f;
            if (alpha <= 0) continue;
            const Vector2 sz = pm::menuItemSize(e.psych, e.label);
            const float y = psychItemY(i) - scroll * itemFactor;
            pm::menuItem(e.psych, e.label, {gfx::W * 0.5f, y + sz.y * 0.5f}, on, alpha, now);
        }
        pm::vcr("God's Hand Engine v" GH_VERSION, 12, gfx::H - 44, 16);
        pm::vcr("Friday Night Funkin' v0.2.8", 12, gfx::H - 24, 16);
        drawUpdateLine();
        gfx::endCanvas();
    }
    // Bottom right: the update being downloaded, or ready.
    void drawUpdateLine() {
        const update::Status us = app().updater.status();
        std::string t;
        if (us.stage == update::Stage::Downloading) t = "Downloading " + us.version + ": " + std::to_string(int(us.progress * 100)) + "%";
        else if (us.stage == update::Stage::Verifying) t = "Checking " + us.version + "...";
        else if (us.stage == update::Stage::Ready) t = us.version + " ready: UPDATE";
        if (!t.empty()) gfx::text(FontId::Ui, t, gfx::W - 14, gfx::H - 30, 18, fade(WHITE, 0.75f), Align::Right);
    }

    std::vector<MainEntry> items_ = mainItems();
    int n_ = int(items_.size());
    float anim_ = 0;
    float camY_ = -1e9f;
    int chosen_ = -1;
    double chosenAt_ = 0;
    double itemsAt_ = 0;
};

// ------------------------------------------------------------------ Story --

class StoryState : public State {
public:
    void update(double) override {
        App& a = app();
        static const char* diffs[3] = {"Easy", "Normal", "Hard"};
        int& d = a.storyDiff;
        d = clampv(d, 0, 2);
        if (go_) {  // Psych: the week flashes for a second, then the first song
            if (a.now - goAt_ > 1.0) startWeek(diffs[d]);
            return;
        }
        if (a.input.left()) { d = (d + 2) % 3; sfxPlay(Sfx::Scroll); }
        if (a.input.right()) { d = (d + 1) % 3; sfxPlay(Sfx::Scroll); }
        if (a.input.back()) { sfxPlay(Sfx::Cancel); a.change(makeMainMenu()); }
        if (a.input.accept()) {
            go_ = true;
            goAt_ = a.now;
            sfxPlay(Sfx::Confirm);
            if (!pm::on()) startWeek(diffs[d]);
        }
    }
    void startWeek(const char* diff) {
        App& a = app();
        {
            PlayRequest r;
            r.songId = "builtin:principality";
            r.diff = diff;
            r.story = true;
            r.storyQueue = {"builtin:godshand", "builtin:demise"};
            a.change(makePlay(r));
        }
    }
    void draw() override {
        if (pm::on()) {
            drawPsych();
            return;
        }
        App& a = app();
        const double now = a.now;
        static const char* diffs[3] = {"Easy", "Normal", "Hard"};
        gfx::beginCanvas();
        drawMenuBackground(now, 0xFF8A5C, menuPulse());
        gfx::gradientRect({0, 70, gfx::W, 330}, Color{10, 6, 20, 220}, Color{40, 18, 40, 220});
        const CharId ids[3] = {CharId::Principality, CharId::Hand, CharId::Demise};
        const double bt = lastBeatTime();
        for (int i = 0; i < 3; ++i) {
            CharacterState c;
            c.id = ids[i];
            c.beatAt = bt;
            const float x = 330.0f + i * 310.0f;
            drawCharacter(c, {x, 370}, i == 2 ? 0.62f : 0.68f, now, false);
            gfx::text(FontId::Ui, charDisplayName(ids[i]), x, 372, 24, charColor(ids[i]), Align::Center, 1.5f);
        }
        gfx::text(FontId::Title, "WEEK 1", 60, 14, 40, kGold, Align::Left, 2.0f);
        gfx::text(FontId::Title, "THE BIG SHOW", gfx::W - 60, 14, 40, kIvory, Align::Right, 2.0f);
        gfx::text(FontId::Ui, "TRACKS", 120, 440, 28, kGold);
        const char* tracks[3] = {"Principality", "Godshand", "Demise"};
        for (int i = 0; i < 3; ++i) gfx::text(FontId::Ui, tracks[i], 120, 478.0f + i * 40, 34, kIvory, Align::Left, 1.5f);
        const std::string d = diffs[clampv(a.storyDiff, 0, 2)];
        const Color dc = diffColor(d);
        const float bob = std::sin(float(now) * 4.0f) * 4.0f;
        gfx::drawNote(gfx::kLeft, {760 - bob, 500}, 54);
        gfx::drawNote(gfx::kRight, {1160 + bob, 500}, 54);
        gfx::glowText(FontId::Title, toLower(d) == "hard" ? "HARD" : (d == "Easy" ? "EASY" : "NORMAL"), 960, 470, 56, dc, dc);
        const ScoreEntry* best = a.scores.get(Scores::key("week:ascension", d));
        gfx::text(FontId::Ui, best ? ("WEEK SCORE: " + fmtThousands(best->score)) : std::string("WEEK SCORE: -"), 960, 560,
                  28, kIvory, Align::Center, 1.5f);
        hints("LEFT/RIGHT difficulty   ENTER start the week   ESC back");
        gfx::endCanvas();
    }

private:
    // Psych's StoryMenuState: a black bar with the week's score and title,
    // the yellow band (0xF9CF51) with the week's characters, the week under
    // it, the difficulty between its arrows on the right, and the tracks in
    // pink under Menu_Tracks.
    void drawPsych() {
        App& a = app();
        const double now = a.now;
        static const char* diffs[3] = {"Easy", "Normal", "Hard"};
        const std::string d = diffs[clampv(a.storyDiff, 0, 2)];
        gfx::beginCanvas();
        DrawRectangleRec({0, 0, gfx::W, gfx::H}, BLACK);
        const ScoreEntry* best = a.scores.get(Scores::key("week:ascension", d));
        pm::vcr("WEEK SCORE: " + std::to_string(best ? best->score : 0), 10, 10, 32, WHITE, -1, 0);
        pm::vcr("THE BIG SHOW", gfx::W - 10, 10, 32, fade(WHITE, 0.7f), 1, 0);
        DrawRectangleRec({0, 56, gfx::W, 386}, Color{0xF9, 0xCF, 0x51, 255});
        const CharId ids[3] = {CharId::Principality, CharId::Hand, CharId::Demise};
        const double bt = lastBeatTime();
        for (int i = 0; i < 3; ++i) {  // Psych: at a quarter of the width apart
            CharacterState c;
            c.id = ids[i];
            c.beatAt = bt;
            drawCharacter(c, {gfx::W * 0.25f * float(i + 1), 420}, i == 2 ? 0.56f : 0.6f, now, false);
        }
        // The week (storymenu/<week>.png in Psych; lettered here, it has no picture).
        const float ww = pm::boldWidth("WEEK 1", 64);  // chosen: it flashes, as Psych's
        pm::bold("WEEK 1", (gfx::W - ww) * 0.5f, 466, 64, go_ && std::fmod((now - goAt_) * 10.0, 2.0) < 1.0 ? 0.35f : 1.0f);
        // Difficulty: < picture >
        const Vector2 as = pm::storyArrowSize();
        const float lx = 850, ly = 476;
        pm::storyArrow(true, a.input.isDown(KEY_LEFT), {lx, ly});
        pm::storyArrow(false, a.input.isDown(KEY_RIGHT), {lx + 376, ly});
        pm::difficulty(d, {lx + as.x + (376 - as.x) * 0.5f, ly + as.y * 0.5f}, 1.0f, diffColor(d));
        // Tracks
        const Assets& pa = pm::assets();
        if (pa.tracks.id) DrawTexture(pa.tracks, int(gfx::W * 0.07f + 100), 56 + 425, WHITE);
        else pm::vcr("TRACKS", gfx::W * 0.07f + 200, 56 + 430, 32, Color{0xE5, 0x57, 0x77, 255}, 0, 0);
        const char* tracks[3] = {"Principality", "Godshand", "Demise"};
        for (int i = 0; i < 3; ++i)
            pm::vcr(tracks[i], gfx::W * 0.5f - gfx::W * 0.35f, 56 + 485 + 32.0f * float(i), 32, Color{0xE5, 0x57, 0x77, 255}, 0, 0);
        gfx::endCanvas();
    }
    using Assets = pm::Assets;

    bool go_ = false;
    double goAt_ = 0;
};

// --------------------------------------------------------------- Freeplay --

class FreeplayState : public State {
public:
    void enter() override {
        rebuild();
        anim_ = float(app().freeplaySel);
        selChangedAt_ = app().now;
        bg_ = currentColor();
    }
    bool wantsMenuMusic() const override { return !previewPlaying_; }
    TouchLayout touchLayout() const override {
        TouchLayout t;
        if (!modsOpen_) t.keys = {{"SEARCH", KEY_F}, {"MODIFIERS", KEY_TAB}, {"MOD PACKS", KEY_M}, {"CHART EDITOR", KEY_SEVEN}, {"RESCAN", KEY_R}};
        if (!modsOpen_ && platform::kAndroid) t.keys.insert(t.keys.begin() + 3, {"IMPORT ZIP", KEY_I});
        return t;
    }
    // While typing a search, the volume keys and the other letters type.
    bool keyReserved(int) const override { return typing_; }

    void update(double dt) override {
        App& a = app();
        Input& in = a.input;
        int& sel = a.freeplaySel;
        const int n = int(list_.size());
        anim_ = approach(anim_, float(sel), 12.0f, float(dt));
        const Color target = currentColor();
        bg_ = gfx::mix(bg_, target, saturate(float(dt) * 4.0f));

        // Searching (JS Engine's Freeplay search): F or / starts typing, the
        // list keeps the songs whose name, mod or id has the text; ENTER keeps
        // the search, ESC clears it. Android: the system's text box.
        if (askingText_) {
            std::string text;
            const platform::TextAsk st = platform::textStatus(&text);
            if (st == platform::TextAsk::Done || st == platform::TextAsk::Cancelled) {
                askingText_ = false;
                platform::resetText();
                if (st == platform::TextAsk::Done) setQuery(text);
            }
            return;
        }
        if (typing_) {
            std::string q = query_;
            for (char32_t c : in.typed()) q += edui::utf8(c);
            if (in.pressed(KEY_BACKSPACE) && !q.empty()) {
                q.pop_back();
                while (!q.empty() && (static_cast<unsigned char>(q.back()) & 0xC0) == 0x80) q.pop_back();
            }
            if (in.pressed(KEY_ESCAPE)) {
                typing_ = false;
                q.clear();
                sfxPlay(Sfx::Cancel);
            } else if (in.pressed(KEY_ENTER) || in.pressed(KEY_KP_ENTER) || in.pressed(KEY_DOWN) || in.pressed(KEY_UP)) {
                typing_ = false;
                sfxPlay(Sfx::Confirm);
            }
            if (q != query_) setQuery(q);
            return;
        }
        if (!modsOpen_ && (in.pressed(KEY_F) || in.pressed(KEY_SLASH))) {
            sfxPlay(Sfx::Scroll);
            if (platform::kAndroid && platform::askText("Search songs", query_)) askingText_ = true;
            else typing_ = true;
            return;
        }
        if (!modsOpen_ && !query_.empty() && in.back()) {  // ESC first clears the search
            setQuery("");
            sfxPlay(Sfx::Cancel);
            return;
        }
        if (modsOpen_) {
            Modifiers& m = a.mods;
            if (in.up()) { modSel_ = (modSel_ + kModRows - 1) % kModRows; sfxPlay(Sfx::Scroll); }
            if (in.down()) { modSel_ = (modSel_ + 1) % kModRows; sfxPlay(Sfx::Scroll); }
            const int dir = in.left() ? -1 : (in.right() ? 1 : 0);
            if (dir || in.accept()) {
                sfxPlay(Sfx::Scroll);
                if (modSel_ == 0) m.rate = clampv(std::round((m.rate + 0.05 * (dir ? dir : 1)) * 20.0) / 20.0, 0.5, 2.0);
                if (modSel_ == 1) m.botplay = !m.botplay;
                if (modSel_ == 2) m.practice = !m.practice;
                if (modSel_ == 3) m.mirror = !m.mirror;
                if (modSel_ == 4) {
                    m.scrollType = (m.scrollType + (dir ? dir : 1) + 3) % 3;
                    m.scrollValue = m.scrollType == 2 ? 2.5 : 1.0;
                }
                if (modSel_ == 5 && m.scrollType != 0) {
                    const double step = m.scrollType == 1 ? 0.05 : 0.1;
                    m.scrollValue = std::round((m.scrollValue + step * (dir ? dir : 1)) / step) * step;
                    m.scrollValue = m.scrollType == 1 ? clampv(m.scrollValue, 0.25, 5.0) : clampv(m.scrollValue, 0.5, 10.0);
                }
                // Kept for the next time the game starts, like the options.
                Settings& s = a.settings;
                s.modRate = float(m.rate);
                s.modBotplay = m.botplay;
                s.modPractice = m.practice;
                s.modMirror = m.mirror;
                s.modScrollType = m.scrollType;
                s.modScrollValue = float(m.scrollValue);
                a.saveSettings();
            }
            if (in.back() || in.pressed(KEY_TAB)) modsOpen_ = false;
            return;
        }
        if (n > 0) {
            if (in.up()) { sel = (sel + n - 1) % n; changed(); }
            if (in.down()) { sel = (sel + 1) % n; changed(); }
            const int nd = int(diffsOf(list_[size_t(sel)]).size());
            if (nd > 0) {
                if (in.left()) { a.freeplayDiff = (diffIndex() + nd - 1) % nd; sfxPlay(Sfx::Scroll); }
                if (in.right()) { a.freeplayDiff = (diffIndex() + 1) % nd; sfxPlay(Sfx::Scroll); }
            }
        }
        if (in.pressed(KEY_TAB)) { modsOpen_ = true; sfxPlay(Sfx::Scroll); }
        // Android: mod zips picked in the system file browser are copied to mods/.
        if (platform::kAndroid && in.pressed(KEY_I) && !importing_) {
            importing_ = platform::pickModZips();
            note(importing_ ? "Pick one or more mod .zip files" : "The file picker is not available");
        }
        bool rescan = in.pressed(KEY_R);
        if (importing_) {
            int count = 0;
            const platform::Import st = platform::importStatus(&count);
            if (st == platform::Import::Done || st == platform::Import::Nothing) {
                importing_ = false;
                platform::resetImport();
                if (st == platform::Import::Done) {
                    rescan = true;
                    note(TextFormat("Imported %d mod zip(s)", count));
                } else {
                    note("Nothing imported (mods must be .zip files)");
                }
            } else if (st == platform::Import::Copying) {
                note("Copying...");
            }
        }
        if (rescan) {
            const std::string id = n > 0 ? list_[size_t(sel)]->id : "";
            a.library.scan(a.base, installProgress);
            rebuild();
            for (int i = 0; i < int(list_.size()); ++i)
                if (list_[size_t(i)]->id == id) sel = i;
            sfxPlay(Sfx::Confirm);
        }
        if (in.back()) { remember(); sfxPlay(Sfx::Cancel); a.change(makeMainMenu()); return; }
        if (in.pressed(KEY_M)) { remember(); sfxPlay(Sfx::Confirm); a.change(makeModPacks()); return; }
        if ((in.pressed(KEY_SEVEN) || in.pressed(KEY_KP_7)) && n > 0) {  // chart editor, as in Psych
            const SongEntry* e = list_[size_t(sel)];
            if (!e->diffs.empty()) {
                remember();
                sfxPlay(Sfx::Confirm);
                a.change(makeChartEditor(e->id, e->diffs[size_t(diffIndex())]));
                return;
            }
        }
        if (in.accept() && n > 0 && autoMode()) {  // AUTOCHART: its screen, for this song
            remember();
            sfxPlay(Sfx::Confirm);
            a.change(makeAutoChartStudio(list_[size_t(sel)]->id, diffIndex()));
            return;
        }
        if (in.accept() && n > 0) {
            const SongEntry* e = list_[size_t(sel)];
            if (!diffsOf(e).empty()) {
                remember();
                sfxPlay(Sfx::Confirm);
                PlayRequest r;
                r.songId = e->id;
                r.diff = diffsOf(e)[size_t(diffIndex())];
                r.render = a.songListMode == App::SongListMode::Render;
                r.autoChart = autoMode();
                a.change(makePlay(r));
                return;
            }
        }
        // song preview
        if (n > 0) {
            const SongEntry* e = list_[size_t(sel)];
            if (!job_.running() && previewId_ != e->id && a.now - selChangedAt_ > 0.45) {
                pendingId_ = e->id;
                const SongEntry copy = *e;
                job_.start([copy] { return app().library.preview(copy); });
            }
            if (job_.ready()) {
                PcmPtr pcm = job_.take();
                if (pcm && pendingId_ == e->id) {
                    a.stopMenuMusic();
                    a.mixer.setTracks(pcm, nullptr, nullptr);
                    const double len = double(pcm->frames()) / Mixer::kRate;
                    const double start = e->builtin ? len * 0.38 : len * 0.3;
                    a.mixer.setLoop(true, start, len);
                    a.mixer.setRate(1.0);
                    a.mixer.play(start);
                    previewPlaying_ = true;
                }
                previewId_ = pendingId_;
            }
        }
    }

    void draw() override {
        App& a = app();
        const double now = a.now;
        gfx::beginCanvas();
        const int n = int(list_.size());
        const bool psych = pm::on();
        if (psych) drawPsychList();
        else drawList();
        if (a.library.customCount() == 0) {
            const std::string tip = a.touchMode
                ? "Tip: get mods in MOD PACKS, or copy them to " + platform::displayPath(a.base + "mods") + " and tap RESCAN"
                : std::string("Tip: drop FNF / Psych Engine songs into the 'songs' or 'mods' folder, then press R");
            gfx::text(FontId::Ui, tip, 40, psych ? 652 : 640, 20, fade(psych ? WHITE : kIvory, 0.75f), Align::Left, psych ? 1.5f : 0.0f);
        }
        if (!note_.empty() && a.now - noteAt_ < 4.0)
            gfx::text(FontId::Ui, note_, 40, 600, 24, fade(kGold, float(std::min(1.0, 4.0 - (a.now - noteAt_)))), Align::Left, 1.5f);
        if (typing_ || askingText_ || !query_.empty()) {
            const std::string shown = (query_.empty() && !typing_ ? std::string("...") : query_) +
                                      (typing_ && std::fmod(now, 1.0) < 0.5 ? "_" : "");
            DrawRectangleRounded({40, 22, 740, 42}, 0.4f, 8, Color{8, 6, 16, 210});
            gfx::text(FontId::Ui, "SEARCH", 58, 30, 22, kGold, Align::Left, 1.5f);
            gfx::text(FontId::Ui, shown, 160, 28, 26, WHITE, Align::Left, 1.0f);
            const std::string count = n == 0 ? std::string("no songs") : TextFormat("%d song%s", n, n == 1 ? "" : "s");
            gfx::text(FontId::Ui, count, 766, 32, 20, fade(kIvory, 0.7f), Align::Right, 1.0f);
        }
        if (a.songListMode != App::SongListMode::Play) {
            const bool render = a.songListMode == App::SongListMode::Render;
            const float by = psych ? 76 : 18;  // under Psych's score box
            DrawRectangleRounded({830, by, 410, 46}, 0.4f, 8, render ? Color{120, 20, 30, 230} : Color{20, 70, 120, 230});
            gfx::text(FontId::Title, render ? "RENDER MODE" : "AUTOCHART", 1035, by + 2, 26, WHITE, Align::Center, 2.0f);
            if (!psych)
                gfx::text(FontId::Ui,
                          render ? "The song plays by itself and is saved as a video"
                                 : "A chart made from the song's own audio",
                          1035, 640, 18, fade(kIvory, 0.75f), Align::Center);
        }
        if (modsOpen_) drawMods();
        const char* go = a.songListMode == App::SongListMode::Render ? "ENTER record" : "ENTER play";
        const std::string help = typing_ ? std::string("Type the song's name   ENTER keep   BACKSPACE erase   ESC clear")
                                 : query_.empty()
                                     ? std::string(go) + "   LEFT/RIGHT difficulty   F search   TAB modifiers   7 chart editor   M mod packs   R rescan   ESC back"
                                     : std::string(go) + "   LEFT/RIGHT difficulty   F search   ESC clear the search   TAB modifiers   7 chart editor";
        if (psych) {
            // Psych's bottom line: a dark band with the keys in vcr.ttf.
            if (!a.touchMode) {
                DrawRectangleRec({0, gfx::H - 26, gfx::W, 26}, fade(BLACK, 0.6f));
                pm::vcr(help, gfx::W * 0.5f, gfx::H - 24, 16, WHITE, 0);
            }
        } else {
            hints(help.c_str());
        }
        gfx::endCanvas();
    }

private:
    // Psych's FreeplayState: menuDesat tinted with the song's colour, the
    // songs in Alphabet's bold letters gliding along a slanted list (90 + 20 x
    // its place, 320 + 156 x its place), each with its icon after it; the
    // best score and the difficulty in a dark box at the top right.
    void drawPsychList() {
        App& a = app();
        const double now = a.now;
        const auto bg = [&] { pm::background(pm::kDesat, bg_, 0, 1.0f); };
        if (a.settings.menuBlur) gfx::drawBlurred(bg);
        else bg();
        const int n = int(list_.size());
        for (int i = std::max(0, int(anim_) - 4); i < std::min(n, int(anim_) + 6); ++i) {
            const float target = float(i) - anim_;
            const float x = 90 + 20 * target, y = 320 + 156 * target;
            const bool on = i == a.freeplaySel;
            const float alpha = on ? 1.0f : 0.6f;
            const SongEntry* e = list_[size_t(i)];
            float h = 70;  // Psych 1.0: at most 980 pixels wide
            const float w0 = pm::boldWidth(e->title, h);
            if (w0 > 980) h *= 980.0f / w0;
            const float w = pm::bold(e->title, x, y, h, alpha);
            const Vector2 ic{x + w + 10 + 75, y - 30 + 75};
            int frames = 1;
            if (const Texture2D* t = icons_.get(*e, &frames)) drawSongIcon(*t, frames, ic, 150, alpha);
            else drawIcon(e->builtin ? charFromName(e->opponent) : CharId::Shade, gfx::rgb(e->color), ic, 110, false, now);
        }
        // PERSONAL BEST: score (accuracy%), and the difficulty under it.
        if (n > 0) {
            const SongEntry* e = list_[size_t(a.freeplaySel)];
            const std::vector<std::string> diffs = diffsOf(e);
            std::string diff = diffs.empty() ? std::string("NORMAL") : diffs[size_t(diffIndex())];
            const ScoreEntry* best = a.scores.get(Scores::key(scoreId(e), diff));
            const std::string score = "PERSONAL BEST: " + std::to_string(best ? best->score : 0) + " (" +
                                      TextFormat("%.2f", best ? best->acc * 100.0 : 0.0) + "%)";
            for (char& c : diff) c = char(std::toupper((unsigned char)c));
            const float tw = pm::vcrWidth(score, 32), boxX = gfx::W - tw - 12;
            DrawRectangleRec({boxX, 0, gfx::W - boxX, 66}, fade(BLACK, 0.6f));
            pm::vcr(score, gfx::W - 6, 5, 32, WHITE, 1, 0);
            pm::vcr(diffs.size() > 1 ? "< " + diff + " >" : diff, boxX + (gfx::W - boxX) * 0.5f, 41, 24, WHITE, 0, 0);
            const Modifiers& m = a.mods;
            if (m.rate != 1.0 || m.botplay || m.practice || m.mirror || m.scrollType != 0) {
                std::string ms = TextFormat("RATE %.2fx", m.rate);
                if (m.botplay) ms += "  BOTPLAY";
                if (m.practice) ms += "  PRACTICE";
                if (m.mirror) ms += "  OPPONENT";
                pm::vcr(ms, gfx::W - 6, 70, 16, kGold, 1);
            }
        }
    }

    void drawList() {
        App& a = app();
        const double now = a.now;
        const unsigned hex = (unsigned(bg_.r) << 16) | (unsigned(bg_.g) << 8) | bg_.b;
        drawMenuBackground(now, hex, previewPlaying_ ? 0.0f : menuPulse());
        const int n = int(list_.size());
        for (int i = std::max(0, int(anim_) - 7); i < std::min(n, int(anim_) + 8); ++i) {
            const float d = float(i) - anim_;
            const float y = 330 + d * 76;
            const float x = 150 + d * 22;
            const bool on = i == a.freeplaySel;
            const float alpha = std::max(0.0f, 1.0f - std::fabs(d) * 0.2f);
            const SongEntry* e = list_[size_t(i)];
            int frames = 1;
            if (const Texture2D* t = icons_.get(*e, &frames)) {  // the chart's character (issue #36)
                drawSongIcon(*t, frames, {x - 50, y + 26}, on ? 84.0f : 68.0f, alpha);
            } else {
                const CharId cid = e->builtin ? charFromName(e->opponent) : CharId::Shade;
                drawIcon(cid, gfx::rgb(e->color), {x - 50, y + 26}, on ? 70.0f : 56.0f, false, now);
            }
            gfx::text(FontId::Ui, e->title, x, y, on ? 52.0f : 42.0f, fade(on ? WHITE : kIvory, alpha), Align::Left, 2.0f);
            if (on && !e->builtin)
                gfx::text(FontId::Ui, e->subtitle, x + 4, y + 50, 20, fade(kGold, 0.8f), Align::Left, 1.0f);
        }
        // info panel
        DrawRectangleRounded({830, 70, 410, 560}, 0.06f, 12, Color{8, 6, 16, 190});
        if (n > 0) {
            const SongEntry* e = list_[size_t(a.freeplaySel)];
            int frames = 1;
            if (const Texture2D* t = icons_.get(*e, &frames)) {
                drawSongIcon(*t, frames, {1035, 175}, 150);
            } else {
                const CharId cid = e->builtin ? charFromName(e->opponent) : CharId::Shade;
                drawIcon(cid, gfx::rgb(e->color), {1035, 175}, 150, false, now);
            }
            gfx::text(FontId::Title, e->title, 1035, 262, e->title.size() > 16 ? 26.0f : 34.0f, kGold, Align::Center, 2.0f);
            float y = 312;
            for (const std::string& l : wrap(e->builtin ? e->subtitle : ("From: " + e->subtitle), 36)) {
                gfx::text(FontId::Ui, l, 1035, y, 22, kIvory, Align::Center);
                y += 24;
            }
            if (!diffsOf(e).empty()) {
                const std::string d = diffsOf(e)[size_t(diffIndex())];
                const Color dc = diffColor(d);
                const float bob = std::sin(float(now) * 4.0f) * 3.0f;
                gfx::drawNote(gfx::kLeft, {880 - bob, 430}, 40);
                gfx::drawNote(gfx::kRight, {1190 + bob, 430}, 40);
                gfx::text(FontId::Title, toLower(d) == "infernal" ? "INFERNAL" : d, 1035, 410, 34, dc, Align::Center, 2.0f);
                const ScoreEntry* best = a.scores.get(Scores::key(scoreId(e), d));
                const std::string bs = best ? fmtThousands(best->score) + "  -  " + TextFormat("%.2f%%", best->acc * 100.0) +
                                                  "  -  " + best->rank
                                            : "No record yet";
                gfx::text(FontId::Ui, "BEST", 1035, 466, 20, fade(kGold, 0.8f), Align::Center);
                gfx::text(FontId::Ui, bs, 1035, 488, 26, WHITE, Align::Center, 1.5f);
                gfx::text(FontId::Ui, TextFormat("Difficulty %d / %d", diffIndex() + 1, int(diffsOf(e).size())), 1035, 524, 20,
                          fade(kIvory, 0.6f), Align::Center);
            }
            const Modifiers& m = a.mods;
            std::string ms = TextFormat("Rate %.2fx", m.rate);
            if (m.botplay) ms += "  Botplay";
            if (m.practice) ms += "  Practice";
            if (m.mirror) ms += "  Opponent";
            if (m.scrollType == 1) ms += TextFormat("  Scroll x%.2f", m.scrollValue);
            if (m.scrollType == 2) ms += TextFormat("  Scroll %.1f", m.scrollValue);
            gfx::text(FontId::Ui, ms, 1035, 586, 22,
                      (m.rate != 1.0 || m.botplay || m.practice || m.mirror || m.scrollType != 0) ? kGold : fade(kIvory, 0.5f), Align::Center);
        }
    }

private:
    void rebuild() {
        App& a = app();
        const std::string keep = !list_.empty() ? list_[size_t(clampv(a.freeplaySel, 0, int(list_.size()) - 1))]->id : std::string();
        list_.clear();
        const std::string q = toLower(query_);
        for (const SongEntry& e : a.library.songs())
            if (q.empty() || toLower(e.title).find(q) != std::string::npos || toLower(e.subtitle).find(q) != std::string::npos ||
                toLower(e.id).find(q) != std::string::npos)
                list_.push_back(&e);
        if (!keep.empty())  // the same song stays picked when it is still listed
            for (int i = 0; i < int(list_.size()); ++i)
                if (list_[size_t(i)]->id == keep) a.freeplaySel = i;
        // The first time after the game starts: the song picked last time.
        static bool restored = false;
        if (!restored && !a.settings.freeplaySong.empty()) {
            for (int i = 0; i < int(list_.size()); ++i)
                if (list_[size_t(i)]->id == a.settings.freeplaySong) a.freeplaySel = i;
        }
        restored = true;
        a.freeplaySel = clampv(a.freeplaySel, 0, std::max(0, int(list_.size()) - 1));
    }
    void setQuery(const std::string& q) {
        if (q == query_) return;
        query_ = q;
        rebuild();
        anim_ = float(app().freeplaySel);
        selChangedAt_ = app().now;
    }
    // The song and difficulty picked, kept for the next time the game starts.
    void remember() {
        App& a = app();
        if (list_.empty()) return;
        a.settings.freeplaySong = list_[size_t(clampv(a.freeplaySel, 0, int(list_.size()) - 1))]->id;
        a.settings.freeplayDiff = a.freeplayDiff;
        a.saveSettings();
    }
    void changed() {
        sfxPlay(Sfx::Scroll);
        selChangedAt_ = app().now;
    }
    void note(const std::string& s) {
        note_ = s;
        noteAt_ = app().now;
    }
    // Unpacking big zips takes a while: keep the screen alive.
    static void installProgress(const std::string& zip, double f) {
        BeginDrawing();
        ClearBackground(Color{12, 8, 20, 255});
        const char* t1 = TextFormat("Installing %s", zip.c_str());
        const int w = GetScreenWidth(), h = GetScreenHeight();
        DrawText(t1, (w - MeasureText(t1, 24)) / 2, h / 2 - 30, 24, RAYWHITE);
        DrawRectangle(w / 4, h / 2 + 10, w / 2, 10, Color{60, 50, 80, 255});
        DrawRectangle(w / 4, h / 2 + 10, int(w / 2 * f), 10, Color{200, 160, 255, 255});
        EndDrawing();
        PollInputEvents();
        SwapScreenBuffer();
    }
    // AUTOCHART: every song in the three difficulties AutoChart makes.
    static bool autoMode() { return app().songListMode == App::SongListMode::AutoChart; }
    static const std::vector<std::string>& diffsOf(const SongEntry* e) {
        static const std::vector<std::string> kAuto = {"Easy", "Normal", "Hard"};
        return autoMode() ? kAuto : e->diffs;
    }
    static std::string scoreId(const SongEntry* e) { return autoMode() ? e->id + "#autochart" : e->id; }
    int diffIndex() const {
        if (list_.empty()) return 0;
        const int nd = int(diffsOf(list_[size_t(app().freeplaySel)]).size());
        return nd > 0 ? clampv(app().freeplayDiff, 0, nd - 1) : 0;
    }
    Color currentColor() const {
        if (list_.empty()) return kGold;
        return gfx::rgb(list_[size_t(app().freeplaySel)]->color);
    }
    void drawMods() {
        const Modifiers& m = app().mods;
        DrawRectangleRec({0, 0, gfx::W, gfx::H}, Color{0, 0, 0, 150});
        DrawRectangleRounded({370, 140, 540, 430}, 0.08f, 12, Color{16, 10, 30, 245});
        gfx::text(FontId::Title, "MODIFIERS", 640, 158, 38, kGold, Align::Center, 2.0f);
        static const char* scrollTypes[3] = {"the chart's", "multiplied", "constant"};
        const std::string rows[kModRows] = {
            TextFormat("Playback rate   %.2fx", m.rate), std::string("Botplay   ") + (m.botplay ? "ON" : "OFF"),
            std::string("Practice (no death)   ") + (m.practice ? "ON" : "OFF"),
            std::string("Play as the opponent   ") + (m.mirror ? "ON" : "OFF"), std::string("Scroll speed   ") + scrollTypes[m.scrollType],
            m.scrollType == 0 ? std::string("Scroll value   -")
                              : std::string(TextFormat(m.scrollType == 1 ? "Scroll value   x%.2f" : "Scroll value   %.1f", m.scrollValue))};
        for (int i = 0; i < kModRows; ++i)
            gfx::text(FontId::Ui, rows[i], 640, 222.0f + i * 50, 30, i == modSel_ ? kGold : (i == 5 && m.scrollType == 0 ? fade(kIvory, 0.4f) : kIvory),
                      Align::Center, 1.5f);
        gfx::text(FontId::Ui, "Scores are only saved at 1.00x or faster, without botplay or practice", 640, 532, 18,
                  fade(kIvory, 0.6f), Align::Center);
    }

    std::vector<const SongEntry*> list_;
    SongIcons icons_;  // the charts' characters' icons
    float anim_ = 0;
    Color bg_ = kGold;
    double selChangedAt_ = 0;
    PcmJob job_;
    std::string pendingId_, previewId_;
    bool previewPlaying_ = false;
    bool modsOpen_ = false;
    static constexpr int kModRows = 6;
    int modSel_ = 0;
    bool importing_ = false;
    std::string query_;        // the search (empty: every song)
    bool typing_ = false;      // typing it with the keyboard
    bool askingText_ = false;  // Android: the system's text box is open
    std::string note_;
    double noteAt_ = 0;
};

// ---------------------------------------------------------------- Credits --

struct PsychCredit {
    const char* name;
    const char* desc;
    unsigned color;               // 0: a section's title
    const char* photo = nullptr;  // a person: assets/credits/<photo>.png, or their initials
    const char* role = "";        // a person, in the scrolling credits
};

class CreditsState : public State {
public:
    void enter() override {
        t0_ = app().now;
        sel_ = 1;
        anim_ = float(sel_);
        tint_ = gfx::rgb(kPsychCredits[sel_].color);
        // The people's pictures: assets/credits/<name>.png (or .jpg); the
        // game draws their initials where there is none.
        for (const PsychCredit& c : kPsychCredits) {
            if (!c.photo) continue;
            for (const char* ext : {".png", ".jpg"}) {
                const std::string path = app().base + "assets/credits/" + c.photo + ext;
                if (!FileExists(path.c_str())) continue;
                Texture2D t = LoadTexture(path.c_str());
                if (t.id) {
                    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
                    photos_[c.photo] = t;
                }
                break;
            }
        }
    }
    ~CreditsState() override {
        for (auto& [name, t] : photos_) UnloadTexture(t);
    }
    void update(double dt) override {
        App& a = app();
        if (pm::on()) {  // Psych's CreditsState: a list to move through
            const int n = int(sizeof(kPsychCredits) / sizeof(kPsychCredits[0]));
            auto step = [&](int d) {
                do { sel_ = (sel_ + d + n) % n; } while (kPsychCredits[sel_].color == 0);
                sfxPlay(Sfx::Scroll);
            };
            if (a.input.up()) step(-1);
            if (a.input.down()) step(1);
            anim_ = approach(anim_, float(sel_), 9.6f, float(dt));
            // The background eases to the chosen line's colour (Psych tweens it in a second).
            const Color want = gfx::rgb(kPsychCredits[sel_].color);
            const float k = 1.0f - std::exp(-float(dt) * 4.0f);
            tint_ = gfx::mix(tint_, want, k);
            if (a.input.back()) {
                sfxPlay(Sfx::Cancel);
                a.change(makeMainMenu());
            }
            return;
        }
        if (a.input.back() || a.input.accept()) {
            sfxPlay(Sfx::Cancel);
            a.change(makeMainMenu());
        }
    }
    void draw() override {
        if (pm::on()) {
            drawPsych();
            return;
        }
        static const char* lines[] = {
            "#GOD'S HAND",
            "of Principality and Demise",
            "",
            "@A RHYTHM DUEL",
            "A project by deatuamae67-crypto",
            "",
            "@ENGINE, MUSIC, ART AND CHARTS",
            "Written from scratch in C++20",
            "Every song is synthesized and every character is drawn in code",
            "Built with Claude Code",
            "",
            "@LIBRARIES",
            "raylib 5.5 by Ramon Santamaria (zlib license)",
            "Lua 5.4 (MIT license), miniz by Rich Geldreich (MIT), pl_mpeg by Dominic Szablewski (MIT)",
            "Zstandard by Meta (BSD license)",
            "",
            "@FONTS",
            "Luckiest Guy by Astigmatic (Apache License 2.0)",
            "Rajdhani by Indian Type Foundry (SIL Open Font License)",
            "",
            "@OWNERS",
            "%Shashangabush",
            "%ph3n0_typ3",
            "%anter_guifer",
            "%xxdet0xl1nt3rxx",
            "%xeno_fiber d1ztr0",
            "%That One Brudda From The Booshes",
            "",
            "@CONTRIBUTORS",
            "%amogosgaming",
            "%iutkuiytkuyitkf",
            "%melody",
            "%auroraaastralis",
            "%IAmCykluss",
            "%manber468",
            "%xentaphyll",
            "%toazp",
            "%TofpFL",
            "",
            "@ENGINE INSPIRED BY",
            "ShadowMario - Psych Engine",
            "JordanSantiagoYT - JS Engine, SNIFF, Mods for JS Engine and his engine forks",
            "HRK-EXEX - SNIFF, H-Slice",
            "SENAndrhevn23 - Rust Funkin HP, H-Slice archive, FNF Tools, Java Funkin, Rust Editor,",
            "midi-to-chart-rust, AndreCharts, HTML Engine",
            "TofpFL - R-Slice (RAID Engine), Universal FNF Sprite Studio",
            "IAmCyklus - iutk engine",
            "buatedz - strand",
            "NAEL2XD - fnf-sdl-showcase, JS Engine fork, Psych Lua scripts",
            "A-Slice Productions - A-Slice",
            "alediomacaj - DenpaEx",
            "amogosgaming - Gods-Hand-Violets-Tweak",
            "PulseForge, S2X Engine",
            "The Funkin' Crew - Friday Night Funkin'",
            "The Haxe Foundation and HaxeFlixel - Haxe, Flixel",
            "Haxepixel - the audio visualizer's idea",
            "",
            "@LICENSE",
            "All rights reserved, every version (see LICENSE)",
            "Play it, record it and make mods for it",
            "No forks, copies or rebrands: contribute through issues",
            "",
            "@THE CAST",
            "Hype - the hype man with the mic",
            "Principal P - no rapping in the halls",
            "DJ Hand - the hand on the decks",
            "D-Mize - king of the underground",
            "",
            "Thank you for playing.",
        };
        const double now = app().now;
        gfx::beginCanvas();
        drawMenuBackground(now, 0x6C8CFF, menuPulse());
        float y = 640 - float(now - t0_) * 42.0f;
        const float total = float(sizeof(lines) / sizeof(lines[0])) * 46 + 700;
        while (y < -total + 640) y += total;
        for (const char* l : lines) {
            if (l[0] == '#') gfx::glowText(FontId::Title, l + 1, gfx::W / 2, y, 64, kGold, Color{255, 180, 80, 255});
            else if (l[0] == '@') gfx::text(FontId::Title, l + 1, gfx::W / 2, y + 10, 30, kGold, Align::Center, 2.0f);
            else if (l[0] == '%') {
                // A person: their picture, their name and what they did.
                const PsychCredit* c = person(l + 1);
                const std::string text = c ? std::string(c->name) + " - " + c->role : std::string(l + 1);
                const float w = gfx::measure(FontId::Ui, text, 28).x;
                if (c) drawPhoto(*c, gfx::W / 2 - w / 2 - 26, y + 26, 40);
                gfx::text(FontId::Ui, text, gfx::W / 2 + 22, y + 10, 28, kIvory, Align::Center, 1.5f);
            } else gfx::text(FontId::Ui, l, gfx::W / 2, y + 10, 28, kIvory, Align::Center, 1.5f);
            y += l[0] == '#' ? 80 : 46;
        }
        hints("ESC back");
        gfx::endCanvas();
    }

private:
    static constexpr PsychCredit kPsychCredits[] = {
        {"God's Hand", "", 0},
        {"deatuamae67-crypto", "A project by deatuamae67-crypto: God's Hand of Principality and Demise, a rhythm duel", 0x8C6CFF},
        {"Claude Code", "The engine, the music, the art and the charts, written from scratch in C++20: every song synthesized, "
                        "every character drawn in code", 0xD97757},
        {"Libraries", "", 0},
        {"raylib", "raylib 5.5 by Ramon Santamaria (zlib license): windows, drawing, sound and input", 0xF5F5F5},
        {"Lua", "Lua 5.4 (MIT license): the mods' scripts", 0x2C2D72},
        {"miniz", "miniz by Rich Geldreich (MIT license): mods in .zip and the binary charts", 0x5AA0E6},
        {"pl_mpeg", "pl_mpeg by Dominic Szablewski (MIT license): the videos", 0x9A6AD8},
        {"Zstandard", "Zstandard by Meta (BSD license): the threadripper engine's .thr charts", 0x4C8CE0},
        {"Fonts", "", 0},
        {"Luckiest Guy", "Luckiest Guy by Astigmatic (Apache License 2.0)", 0xFFC83D},
        {"Rajdhani", "Rajdhani by Indian Type Foundry (SIL Open Font License)", 0x3DD1FF},
        {"Owners", "", 0},
        {"Shashangabush", "One of God's Hand's owners", 0xF2B84B, "shashangabush", "owner"},
        {"ph3n0_typ3", "One of God's Hand's owners", 0x7CE0C6, "ph3n0_typ3", "owner"},
        {"anter_guifer", "One of God's Hand's owners", 0xE07C7C, "anter_guifer", "owner"},
        {"xxdet0xl1nt3rxx", "One of God's Hand's owners", 0xA77CE0, "xxdet0xl1nt3rxx", "owner"},
        {"xeno_fiber d1ztr0", "One of God's Hand's owners", 0x7CA8E0, "xeno_fiber_d1ztr0", "owner"},
        {"That One Brudda From The Booshes", "One of God's Hand's owners", 0x9CD65C, "that_one_brudda_from_the_booshes", "owner"},
        {"Contributors", "", 0},
        {"amogosgaming", "Ideas (the Psych look everywhere, the chart editor's preview, shader layers...), testing, and the "
                         "Violets Tweak that brought the game to older systems", 0x5CD6A0, "amogosgaming",
         "ideas, testing and the Violets Tweak (older systems)"},
        {"iutkuiytkuyitkf", "Contributor to God's Hand", 0xE0C04C, "iutkuiytkuyitkf", "contributor"},
        {"melody", "Contributor to God's Hand", 0xFF8FC8, "melody", "contributor"},
        {"auroraaastralis", "Contributor to God's Hand", 0x8FE8FF, "auroraaastralis", "contributor"},
        {"IAmCykluss", "Contributor to God's Hand", 0xD8B04C, "iamcykluss", "contributor"},
        {"manber468", "Contributor to God's Hand", 0x6CC0FF, "manber468", "contributor"},
        {"xentaphyll", "Contributor to God's Hand", 0x6CE08C, "xentaphyll", "contributor"},
        {"toazp", "Contributor to God's Hand", 0xFFA45C, "toazp", "contributor"},
        {"TofpFL", "Contributor to God's Hand; R-Slice and Universal FNF Sprite Studio are among the engines it learned from",
         0x4CD7E0, "tofpfl", "contributor"},
        {"Engine inspired by", "", 0},
        {"ShadowMario", "Psych Engine: the interface, the scripts and the menus God's Hand follows", 0xFF5DA0},
        {"JordanSantiagoYT", "JS Engine, SNIFF (MIDI to chart), Mods for JS Engine, and his engine forks (ZS Engine, "
                             "Psych EK Spam, SB Engine, DenpaEx PR, JS Engine Rewrite)", 0x58A8FF},
        {"HRK-EXEX", "SNIFF and H-Slice", 0xB070FF},
        {"SENAndrhevn23", "Rust Funkin HP, the H-Slice archive, FNF Tools, JS Engine without chart editor lag, Java Funkin, "
                          "Rust Editor, midi-to-chart-rust, AndreCharts and HTML Engine", 0xFF8C42},
        {"TofpFL", "R-Slice (RAID Engine) and its .raid charts, and Universal FNF Sprite Studio", 0x4CD7E0},
        {"IAmCyklus", "iutk engine and its stacked-note charts", 0xE0C04C},
        {"buatedz", "strand: big charts on little memory", 0x8CE04C},
        {"NAEL2XD", "fnf-sdl-showcase, a JS Engine fork and Psych Lua scripts", 0xE04C8C},
        {"A-Slice Productions", "A-Slice", 0x9A9AFF},
        {"alediomacaj", "DenpaEx", 0xFF6464},
        {"PulseForge", "The engine whose best parts and mod packs God's Hand took in", 0x6C8CFF},
        {"S2X Engine", "Its loading and memory tricks (Psych 0.7.3 based)", 0x7FD0FF},
        {"The Funkin' Crew", "Friday Night Funkin', where it all comes from", 0xFF9A3C},
        {"Haxe and HaxeFlixel", "The Haxe Foundation and HaxeFlixel: what Psych and the others are made with", 0xF7A93B},
        {"Haxepixel", "The idea of the audio visualizer", 0x6FE3B4},
        {"License", "", 0},
        {"All rights reserved", "Every version (see LICENSE). Play it, record it and make mods for it. No forks, copies or "
                                "rebrands: contribute through issues", 0xE04848},
        {"The Cast", "", 0},
        {"Hype", "The hype man with the mic", 0x5CC8FF},
        {"Principal P", "No rapping in the halls", 0xB48CDC},
        {"DJ Hand", "The hand on the decks", 0xF4C76B},
        {"D-Mize", "King of the underground", 0xFF5C8A},
    };
    // Psych's CreditsState: sections in bold letters, centred; people in the
    // plain ones from the middle, 156 pixels apart; menuDesat in the chosen
    // line's colour and its description in a dark box at the bottom.
    void drawPsych() {
        App& a = app();
        gfx::beginCanvas();
        const auto bg = [&] { pm::background(pm::kDesat, tint_); };
        if (a.settings.menuBlur) gfx::drawBlurred(bg);
        else bg();
        const int n = int(sizeof(kPsychCredits) / sizeof(kPsychCredits[0]));
        for (int i = 0; i < n; ++i) {
            const PsychCredit& c = kPsychCredits[i];
            const float y = 300 + 156 * (float(i) - anim_);
            if (y < -120 || y > gfx::H + 40) continue;
            if (c.color == 0) {
                const float w = pm::boldWidth(c.name, 68);
                pm::bold(c.name, (gfx::W - w) * 0.5f, y, 68);
            } else {
                // A long name gets smaller to fit, its picture right of it (as Psych's icons).
                const float x = gfx::W * 0.5f - 70, room = gfx::W - 30 - x - (c.photo ? 140.0f : 0.0f);
                const float full = pm::plainWidth(c.name, 60), h = full > room ? 60 * room / full : 60.0f;
                const float w = pm::plain(c.name, x, y + (60 - h) * 0.5f, h, i == sel_ ? 1.0f : 0.6f);
                if (c.photo) drawPhoto(c, x + w + 70, y + 30, 120, i == sel_ ? 1.0f : 0.6f);
            }
        }
        const std::string desc = kPsychCredits[sel_].desc;
        std::vector<std::string> lines;
        std::string cur;
        for (size_t p = 0; p < desc.size();) {
            size_t q = desc.find(' ', p);
            if (q == std::string::npos) q = desc.size();
            const std::string word = desc.substr(p, q - p), tryLine = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && pm::vcrWidth(tryLine, 32) > 1180) {
                lines.push_back(cur);
                cur = word;
            } else {
                cur = tryLine;
            }
            p = q + 1;
        }
        if (!cur.empty()) lines.push_back(cur);
        const float lh = 32 * 1.15f, th = lh * float(lines.size()), ty = gfx::H - th - 135;
        float tw = 0;
        for (const std::string& l : lines) tw = std::max(tw, pm::vcrWidth(l, 32));
        DrawRectangleRec({(gfx::W - tw) * 0.5f - 10, ty - 10, tw + 20, th + 25}, fade(BLACK, 0.6f));
        for (size_t k = 0; k < lines.size(); ++k) pm::vcr(lines[k], gfx::W * 0.5f, ty + lh * float(k), 32, WHITE, 0, 2.4f);
        gfx::endCanvas();
    }

    static const PsychCredit* person(const char* name) {
        for (const PsychCredit& c : kPsychCredits)
            if (c.photo && !std::strcmp(c.name, name)) return &c;
        return nullptr;
    }
    // A person's picture (square, framed), or their initials on their colour.
    void drawPhoto(const PsychCredit& c, float cx, float cy, float size, float alpha = 1.0f) const {
        const Color col = gfx::rgb(c.color);
        const auto it = photos_.find(c.photo);
        if (it != photos_.end()) {
            const Texture2D& t = it->second;
            const float side = float(std::min(t.width, t.height));
            const Rectangle src{(t.width - side) * 0.5f, (t.height - side) * 0.5f, side, side};
            DrawRectangleRec({cx - size / 2 - 3, cy - size / 2 - 3, size + 6, size + 6}, fade(col, alpha));
            DrawTexturePro(t, src, {cx - size / 2, cy - size / 2, size, size}, {0, 0}, 0, fade(WHITE, alpha));
            return;
        }
        std::string initials;
        bool start = true;
        for (const char* p = c.name; *p && initials.size() < 2; ++p) {
            const unsigned char ch = static_cast<unsigned char>(*p);
            if (std::isalnum(ch) && start) initials += char(std::toupper(ch));
            start = !std::isalnum(ch);
        }
        if (initials.size() < 2 && std::strlen(c.name) > 1) initials = {char(std::toupper(static_cast<unsigned char>(c.name[0]))), c.name[1]};
        DrawCircleV({cx, cy}, size / 2, fade(col, alpha));
        DrawCircleLinesV({cx, cy}, size / 2, fade(WHITE, 0.8f * alpha));
        const float lum = 0.299f * col.r + 0.587f * col.g + 0.114f * col.b;
        gfx::text(FontId::Title, initials, cx, cy - size * 0.27f, size * 0.46f, fade(lum > 150 ? Color{25, 20, 35, 255} : WHITE, alpha),
                  Align::Center);
    }

    double t0_ = 0;
    int sel_ = 1;
    float anim_ = 1;
    Color tint_ = WHITE;
    std::map<std::string, Texture2D> photos_;
};

std::unique_ptr<State> makeTitle() { return std::make_unique<TitleState>(); }
std::unique_ptr<State> makeMainMenu() { return std::make_unique<MainMenuState>(); }
std::unique_ptr<State> makeStory() { return std::make_unique<StoryState>(); }
std::unique_ptr<State> makeFreeplay() { return std::make_unique<FreeplayState>(); }
std::unique_ptr<State> makeCredits() { return std::make_unique<CreditsState>(); }

}  // namespace gh
