// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The editors' menu (main menu: 7), as Psych Engine's Master Editor Menu: the
// chart editor and the character, stage and event editors. They work on the
// mods the game found; the menu picks the mod they save into.
//
// Character editor: a Psych character JSON (characters/<name>.json): its
// animations and their offsets (the arrow keys, with the idle ghost behind),
// image, scale, flip, health icon and bar colour, position and camera point.
// Stage editor: a Psych stage JSON (stages/<name>.json): zoom, where the three
// characters stand and where the camera looks at each, shown over the stage
// itself (its JSON objects and its Lua script) with the characters dragged by
// the mouse. Event editor: custom events (custom_events/<name>.txt and .lua)
// from a template. Files keep the fields the editors do not know.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <mutex>
#include <cstring>

#include "app.hpp"
#include "basegame.hpp"
#include "chart_binary.hpp"
#include "chart_stream.hpp"
#include "common.hpp"
#include "editor_ui.hpp"
#include "gfx.hpp"
#include "json.hpp"
#include "lua_host.hpp"
#include "process.hpp"
#include "psych.hpp"
#include "raylib.h"
#include "scene.hpp"
#include "scene_draw.hpp"

namespace gh {

namespace {
namespace fs = std::filesystem;
using namespace edui;
using gfx::fade;

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathStr(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

const Color kGold{255, 222, 64, 255};   // the accent: a bright, cartoon yellow
const Color kIvory{242, 244, 255, 255};  // text: a cool white
constexpr float kPanelX = 872, kPanelW = gfx::W - kPanelX - 8;

// The keys line at the bottom, as the menus show it.
void hints(const char* s) {
    if (app().touchMode) return;
    DrawRectangleRec({0, gfx::H - 40, gfx::W, 40}, Color{0, 0, 0, 150});
    gfx::text(FontId::Ui, s, gfx::W / 2, gfx::H - 33, 20, Color{220, 214, 235, 255}, Align::Center);
}

// ------------------------------------------------------------------- mods --

struct ModChoice {
    std::string name;                // its folder in mods/ ("" = the mods folder itself)
    std::vector<std::string> roots;  // its asset folders; the editors save into roots[0]
};

std::vector<ModChoice> modChoices() {
    App& a = app();
    std::vector<ModChoice> out;
    for (const auto& [pack, roots] : a.library.packRoots())
        if (!pack.empty() && !roots.empty()) out.push_back({pack, roots});
    out.push_back({"", {a.base + "mods/"}});
    return out;
}

int& modIndex() {
    static int i = 0;
    return i;
}

ModChoice currentMod() {
    const std::vector<ModChoice> list = modChoices();
    int& i = modIndex();
    i = clampv(i, 0, int(list.size()) - 1);
    return list[size_t(i)];
}

std::string modLabel(const ModChoice& m) { return m.name.empty() ? std::string("mods/ (no mod)") : m.name; }

void useMod(scene::Scene& sc, const ModChoice& m) {
    std::vector<std::string> roots = m.roots;
    roots.push_back(app().base + "mods/");
    sc.files.setRoots(roots);
    sc.files.setSharedRoots(app().library.allAssetRoots());
}

// The files in `dir` ending in `ext`, by name: the chosen mod's first, then
// the ones other mods have.
std::vector<std::string> listNames(const scene::Scene& sc, const std::string& dir, const std::string& ext) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto scan = [&](const std::vector<std::string>& roots) {
        std::vector<std::string> found;
        for (const std::string& r : roots) {
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(toPath(r + dir), ec)) {
                if (!e.is_regular_file(ec)) continue;
                std::string n = pathStr(e.path().filename());
                if (!endsWithI(n, ext)) continue;
                n.resize(n.size() - ext.size());
                if (seen.insert(n).second) found.push_back(n);
            }
        }
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    };
    scan(sc.files.roots());
    scan(sc.files.sharedRoots());
    return out;
}

// ------------------------------------------------------------ JSON files --

std::string jnum(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) return std::to_string((long long)v);
    char b[48];
    std::snprintf(b, sizeof b, "%.6f", v);
    std::string s = b;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// A JSON value as Psych's editors write it: tabs, short arrays on one line.
void emit(const json::Document& d, const json::Value& v, std::string& o, int ind) {
    const std::string pad(size_t(ind), '\t');
    switch (v.type) {
        case json::Type::Null: o += "null"; break;
        case json::Type::Bool: o += v.b ? "true" : "false"; break;
        case json::Type::Number: o += jnum(v.num); break;
        case json::Type::String: o += jsonStr(std::string(d.str(v))); break;
        case json::Type::Array: {
            const size_t n = d.size(v);
            bool flat = true;
            for (size_t i = 0; i < n; ++i) {
                const json::Type t = d.at(v, i).type;
                flat = flat && t != json::Type::Array && t != json::Type::Object;
            }
            if (n == 0 || flat) {
                o += "[";
                for (size_t i = 0; i < n; ++i) {
                    if (i) o += ", ";
                    emit(d, d.at(v, i), o, ind);
                }
                o += "]";
                break;
            }
            o += "[\n";
            for (size_t i = 0; i < n; ++i) {
                o += pad + "\t";
                emit(d, d.at(v, i), o, ind + 1);
                o += i + 1 < n ? ",\n" : "\n";
            }
            o += pad + "]";
            break;
        }
        case json::Type::Object: {
            const size_t n = d.size(v);
            if (n == 0) {
                o += "{}";
                break;
            }
            o += "{\n";
            for (size_t i = 0; i < n; ++i) {
                o += pad + "\t" + jsonStr(std::string(d.keyAt(v, i))) + ": ";
                emit(d, d.valAt(v, i), o, ind + 1);
                o += i + 1 < n ? ",\n" : "\n";
            }
            o += pad + "}";
            break;
        }
    }
}

// An object: `fields` (key, JSON text) in their order, then the fields of the
// file as it was (`oldText`) that `fields` does not set.
std::string writeObject(const std::vector<std::pair<std::string, std::string>>& fields, const std::string& oldText) {
    std::vector<std::string> parts;
    std::set<std::string> have;
    for (const auto& [k, v] : fields) {
        parts.push_back("\t" + jsonStr(k) + ": " + v);
        have.insert(k);
    }
    json::Document d;
    if (!oldText.empty() && d.parse(oldText) && d.root().type == json::Type::Object) {
        const json::Value& r = d.root();
        for (size_t i = 0; i < d.size(r); ++i) {
            const std::string k(d.keyAt(r, i));
            if (have.count(k)) continue;
            std::string v;
            emit(d, d.valAt(r, i), v, 1);
            parts.push_back("\t" + jsonStr(k) + ": " + v);
        }
    }
    std::string o = "{\n";
    for (size_t i = 0; i < parts.size(); ++i) o += parts[i] + (i + 1 < parts.size() ? ",\n" : "\n");
    return o + "}\n";
}

std::string pair(double a, double b) { return "[" + jnum(a) + ", " + jnum(b) + "]"; }

bool saveText(const std::string& path, const std::string& text, std::string& err) {
    std::error_code ec;
    fs::create_directories(toPath(path).parent_path(), ec);
    std::ofstream f(toPath(path), std::ios::binary);
    if (!f) {
        err = "could not write " + path;
        return false;
    }
    f << text;
    if (!f) err = "could not write " + path;
    return bool(f);
}

// A safe file name for what the player typed.
std::string fileName(std::string s) {
    s = trim(s);
    for (char& c : s)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '-';
    return s;
}

// ------------------------------------------------------------- characters --

bool loadCharacterDef(const scene::Scene& sc, const std::string& name, psych::CharacterDef& def, std::string& path, std::string& text,
                      std::string& err) {
    def = psych::CharacterDef{};
    text.clear();
    path = sc.files.find("characters/" + name + ".json");
    if (path.empty()) path = sc.files.find("data/characters/" + name + ".json");
    if (!path.empty()) {
        if (!scene::readFile(path, text)) {
            err = "could not read " + path;
            return false;
        }
        return psych::parseCharacter(text, def, err);
    }
    if (const char* base = basegame::characterJson(name)) {  // older engines' hard-coded characters
        path.clear();
        return psych::parseCharacter(base, def, err);
    }
    err = "characters/" + name + ".json not found";
    return false;
}

std::string imageKeys(const psych::CharacterDef& def) {
    std::string keys;
    for (const std::string& im : def.images) keys += (keys.empty() ? "" : ",") + im;
    return keys;
}

// The character as the play state builds it (Psych's Character), or nullptr
// when its image cannot be found.
scene::CharacterPtr buildCharacter(scene::Scene& sc, const psych::CharacterDef& def, bool isPlayer, std::string& err) {
    const std::string keys = imageKeys(def);
    scene::GraphicPtr g = keys.empty() ? nullptr : sc.gfx.atlas(keys);
    if (!g) {
        err = "image not found: " + (keys.empty() ? std::string("(none)") : keys);
        return nullptr;
    }
    auto c = std::make_shared<scene::Character>();
    c->kind = scene::Kind::Character;
    c->def = def;
    c->isPlayer = isPlayer;
    c->setGraphic(g);
    for (const psych::AnimDef& ad : def.anims) {
        scene::Anim a;
        a.name = ad.anim;
        a.frames = ad.indices.empty() ? psych::framesByPrefix(g->frames, ad.prefix) : psych::framesByIndices(g->frames, ad.prefix, ad.indices);
        if (a.frames.empty()) continue;
        a.fps = ad.fps;
        a.loop = ad.loop;
        a.hasOffset = true;
        a.offX = ad.offX;
        a.offY = ad.offY;
        c->addAnim(std::move(a));
    }
    if (def.scale != 1.0) {
        c->setGraphicSize(std::floor(c->width * float(def.scale)), 0);
        c->updateHitbox();
    }
    c->flipX = def.flipX != isPlayer;
    c->antialiasing = def.antialiasing;
    c->camX = def.camX;
    c->camY = def.camY;
    c->posX = def.posX;
    c->posY = def.posY;
    c->icon = def.icon;
    c->barColor = def.barColor;
    c->singDuration = def.singDuration;
    if (!def.anims.empty()) c->playAnim(def.anims[0].anim, true);
    return c;
}

// The animation names an atlas holds (frame names without their numbers).
std::vector<std::string> atlasPrefixes(const scene::Graphic& g) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const psych::AtlasFrame& f : g.frames) {
        std::string n = f.name;
        while (!n.empty() && std::isdigit((unsigned char)n.back())) n.pop_back();
        if (!n.empty() && seen.insert(n).second) out.push_back(n);
    }
    return out;
}

std::string iconPath(const scene::Scene& sc, const std::string& icon) {
    for (const std::string& rel : {"images/icons/icon-" + icon + ".png", "images/icons/" + icon + ".png", "images/icon-" + icon + ".png"}) {
        const std::string p = sc.files.find(rel);
        if (!p.empty()) return p;
    }
    return {};
}

// Psych's "Get Icon Color": the commonest colour of the icon's first frame,
// black and see-through pixels left out.
bool iconColor(const std::string& path, uint32_t& rgb) {
    Image img = LoadImage(path.c_str());
    if (!img.data) return false;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const int w = img.width >= img.height * 2 ? img.width / 2 : img.width;
    std::map<uint32_t, int> count;
    const unsigned char* px = static_cast<const unsigned char*>(img.data);
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = px + (size_t(y) * size_t(img.width) + size_t(x)) * 4;
            if (p[3] == 0) continue;
            const uint32_t c = uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
            if (c == 0) continue;
            ++count[c];
        }
    UnloadImage(img);
    int best = 0;
    for (const auto& [c, n] : count)
        if (n >= best) {
            best = n;
            rgb = c;
        }
    return best > 0;
}

// ------------------------------------------------------------- the view --

// What part of the world the editor shows (centre and zoom), through the
// game camera of its scene; dragged with the right mouse button, zoomed with
// the wheel.
struct View {
    float x = 640, y = 360, zoom = 1;
    bool dragging = false;
    Vector2 last{};

    void apply(scene::Camera& cam) const {
        cam.zoom = zoom;
        cam.scrollX = x - gfx::W * 0.5f;
        cam.scrollY = y - gfx::H * 0.5f;
    }
    Vector2 toWorld(Vector2 canvas) const { return {x + (canvas.x - gfx::W * 0.5f) / zoom, y + (canvas.y - gfx::H * 0.5f) / zoom}; }
    Vector2 toCanvas(float wx, float wy) const { return {gfx::W * 0.5f + (wx - x) * zoom, gfx::H * 0.5f + (wy - y) * zoom}; }
    void handle(const Input& in, const Ui& ui, bool overPanel) {
        const Vector2 m = ui.mouse;
        if (!overPanel && (in.mousePressed(MOUSE_BUTTON_RIGHT) || in.mousePressed(MOUSE_BUTTON_MIDDLE))) {
            dragging = true;
            last = m;
        }
        if (dragging) {
            if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) dragging = false;
            x -= (m.x - last.x) / zoom;
            y -= (m.y - last.y) / zoom;
            last = m;
        }
        if (!overPanel && ui.wheel != 0 && !ui.used) {
            const Vector2 before = toWorld(m);
            zoom = clampv(zoom * (ui.wheel > 0 ? 1.1f : 1.0f / 1.1f), 0.05f, 8.0f);
            const Vector2 after = toWorld(m);
            x += before.x - after.x;
            y += before.y - after.y;
        }
    }
};

void drawGrid(const View& v) {
    DrawRectangleRec({0, 0, gfx::W, gfx::H}, Color{24, 20, 34, 255});
    const float step = 100 * v.zoom;
    if (step < 8) return;
    const Vector2 o = v.toCanvas(0, 0);
    for (float x = std::fmod(o.x, step); x < gfx::W; x += step) DrawLineEx({x, 0}, {x, gfx::H}, 1, Color{40, 34, 56, 255});
    for (float y = std::fmod(o.y, step); y < gfx::H; y += step) DrawLineEx({0, y}, {gfx::W, y}, 1, Color{40, 34, 56, 255});
    DrawLineEx({o.x, 0}, {o.x, gfx::H}, 1.5f, Color{70, 60, 100, 255});
    DrawLineEx({0, o.y}, {gfx::W, o.y}, 1.5f, Color{70, 60, 100, 255});
}

void cross(Vector2 c, float r, Color col) {
    DrawLineEx({c.x - r, c.y}, {c.x + r, c.y}, 3, BLACK);
    DrawLineEx({c.x, c.y - r}, {c.x, c.y + r}, 3, BLACK);
    DrawLineEx({c.x - r, c.y}, {c.x + r, c.y}, 1.5f, col);
    DrawLineEx({c.x, c.y - r}, {c.x, c.y + r}, 1.5f, col);
}

void panelBox(float y0, float y1) {
    DrawRectangleRounded({kPanelX - 6, y0, kPanelW + 12, y1 - y0}, 0.03f, 6, kPanel);
    DrawRectangleRoundedLinesEx({kPanelX - 6, y0, kPanelW + 12, y1 - y0}, 0.03f, 6, 1.5f, Color{110, 92, 150, 160});
}

void feedUi(Ui& ui) {
    App& a = app();
    SceneRenderer::smoothScaling = a.settings.antialiasing;
    ui.in = &a.input;
    ui.mouse = gfx::toCanvas(GetMousePosition());
    ui.click = a.input.mousePressed(MOUSE_BUTTON_LEFT) && !(a.touchMode && a.touch.covers(GetMousePosition()));
    ui.rclick = a.input.mousePressed(MOUSE_BUTTON_RIGHT);
    ui.wheel = a.input.wheel();
    ui.used = false;
}

bool overPanel(const Ui& ui) { return ui.mouse.x >= kPanelX - 8 || !ui.dropId.empty(); }

// Scripts in the editors: a stage's Lua builds its sprites; nothing else happens.
struct EditorGame : ScriptGame {
    double clockMs = 0;     // the stage editor runs a clock so scripts and shaders move
    bool shaders = false;   // the stage editor shows shaders (as the Shaders option says)
    double songPosition() const override { return clockMs; }
    bool getGameProp(std::string_view, scene::Value&) override { return false; }
    bool setGameProp(std::string_view, const scene::Value&) override { return false; }
    void triggerEvent(const std::string&, const std::string&, const std::string&) override {}
    void debugPrint(const std::string&, uint32_t) override {}
    void debugNotice(const std::string&, bool, bool) override {}
    bool shadersEnabled() override { return shaders; }
};

// A line of news at the bottom of an editor.
struct Status {
    std::string text;
    double at = -10;
    bool bad = false;
    void set(const std::string& t, bool error = false) {
        text = t;
        bad = error;
        at = app().now;
    }
    void draw() const {
        if (app().now - at > 6 || text.empty()) return;
        const float al = float(std::min(1.0, 6 - (app().now - at)));
        const float w = gfx::measure(FontId::Ui, text, 18).x;
        DrawRectangleRec({12, gfx::H - 70, w + 20, 28}, fade(BLACK, 0.7f * al));
        gfx::text(FontId::Ui, text, 22, gfx::H - 66, 18, fade(bad ? Color{255, 120, 120, 255} : kGold, al));
    }
};

// ======================================================= chart converter ==
// Issue #22: a mod's charts to God's Hand's binary format (.ghc), each one
// next to its JSON (kept: Psych and the other engines read that). When both
// are there the game loads the .ghc: a fraction of the size, read faster.

class ChartConverterState : public State {
public:
    void enter() override {
        mod_ = currentMod();
        rows();
    }
    ~ChartConverterState() override {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
    }
    void update(double) override {
        App& a = app();
        Input& in = a.input;
        if (running_.load()) {
            if (in.back()) stop_ = true;  // stops after the chart being converted
            return;
        }
        if (worker_.joinable()) {  // finished: the song list sees the new files
            worker_.join();
            a.library.scan(a.base);
            rows();
        }
        if (in.back()) {
            sfxPlay(Sfx::Cancel);
            a.change(makeEditorMenu());
            return;
        }
        const int n = int(rows_.size());
        if (n == 0) return;
        if (in.up()) sel_ = (sel_ + n - 1) % n;
        if (in.down()) sel_ = (sel_ + 1) % n;
        if (in.accept()) start({size_t(sel_)});
        if (in.pressed(KEY_A)) {
            std::vector<size_t> all;
            for (size_t i = 0; i < rows_.size(); ++i) all.push_back(i);
            start(all);
        }
    }
    void draw() override {
        App& a = app();
        gfx::beginCanvas();
        drawMenuBackground(a.now, 0x6C8CFF, menuPulse());
        DrawRectangleRec({40, 30, gfx::W - 80, gfx::H - 90}, fade(BLACK, 0.6f));
        gfx::text(FontId::Ui, "Binary charts (.ghc)   mod: " + modLabel(mod_), 60, 44, 26, kGold);
        gfx::text(FontId::Ui, "Each chart gets a .ghc beside its JSON (the JSON stays, for Psych). The game loads the .ghc.", 60, 78,
                  17, fade(kIvory, 0.75f));
        std::lock_guard<std::mutex> lk(mtx_);
        if (rows_.empty()) gfx::text(FontId::Ui, "No charts in this mod.", 60, 130, 22, kIvory);
        const int visible = 17, first = std::clamp(sel_ - visible / 2, 0, std::max(0, int(rows_.size()) - visible));
        for (int i = first; i < std::min(int(rows_.size()), first + visible); ++i) {
            const Row& r = rows_[size_t(i)];
            const float y = 116.0f + float(i - first) * 30.0f;
            if (i == sel_) DrawRectangleRec({52, y - 3, gfx::W - 104, 28}, fade(kGold, 0.18f));
            gfx::text(FontId::Ui, r.song + "  " + r.diff, 64, y, 19, i == sel_ ? kGold : kIvory);
            gfx::text(FontId::Ui, r.json.empty() ? "-" : megabytes(r.jsonBytes), 700, y, 19, fade(kIvory, 0.8f), Align::Right);
            gfx::text(FontId::Ui, r.ghc.empty() ? "-" : megabytes(r.ghcBytes), 840, y, 19, fade(kIvory, 0.8f), Align::Right);
            gfx::text(FontId::Ui, r.note, 870, y, 17, r.bad ? Color{255, 130, 130, 255} : Color{150, 230, 170, 255});
        }
        gfx::text(FontId::Ui, "JSON", 700, 98, 15, fade(kIvory, 0.5f), Align::Right);
        gfx::text(FontId::Ui, ".ghc", 840, 98, 15, fade(kIvory, 0.5f), Align::Right);
        hints(running_.load() ? "Converting...   ESC stop" : "UP/DOWN choose   ENTER convert this one   A convert all   ESC back");
        gfx::endCanvas();
    }
    TouchLayout touchLayout() const override { return {}; }

private:
    struct Row {
        std::string song, diff, json, ghc, note;
        uint64_t jsonBytes = 0, ghcBytes = 0;
        bool bad = false;
    };

    static std::string megabytes(uint64_t b) {
        return b >= 1000000 ? TextFormat("%.1f MB", double(b) / 1e6) : TextFormat("%.1f KB", double(b) / 1e3);
    }

    // The mod's charts that are JSON or .ghc files (not FL Studio projects or
    // the V-Slice ones, which hold several difficulties).
    void rows() {
        std::lock_guard<std::mutex> lk(mtx_);
        rows_.clear();
        std::set<std::string> seen;
        for (const SongEntry& e : app().library.songs()) {
            if (e.builtin || e.autoChart || !e.metaPath.empty()) continue;
            if (!mod_.name.empty() && e.pack != mod_.name) continue;
            for (size_t i = 0; i < e.diffs.size() && i < e.chartPaths.size(); ++i) {
                const std::string& p = e.chartPaths[i];
                std::string base;
                if (endsWithI(p, ".json")) base = p.substr(0, p.size() - 5);
                else if (endsWithI(p, kBinaryChartExt)) base = p.substr(0, p.size() - std::string(kBinaryChartExt).size());
                else continue;
                if (!seen.insert(base).second) continue;
                Row r;
                r.song = e.title;
                r.diff = e.diffs[i];
                std::error_code ec;
                if (fs::exists(toPath(base + ".json"), ec)) {
                    r.json = base + ".json";
                    r.jsonBytes = fileSize(r.json);
                }
                if (fs::exists(toPath(base + kBinaryChartExt), ec)) {
                    r.ghc = base + kBinaryChartExt;
                    r.ghcBytes = fileSize(r.ghc);
                }
                if (r.json.empty()) r.note = "only the .ghc";
                rows_.push_back(std::move(r));
            }
        }
        sel_ = std::clamp(sel_, 0, std::max(0, int(rows_.size()) - 1));
    }

    void start(std::vector<size_t> which) {
        if (running_.load() || which.empty()) return;
        stop_ = false;
        running_ = true;
        worker_ = std::thread([this, which] {
            for (size_t i : which) {
                if (stop_.load()) break;
                std::string json;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    if (i >= rows_.size() || rows_[i].json.empty()) continue;
                    json = rows_[i].json;
                    rows_[i].note = "converting...";
                    rows_[i].bad = false;
                }
                const std::string ghc = json.substr(0, json.size() - 5) + kBinaryChartExt;
                ConvertStats st;
                std::string err;
                const bool ok = convertChartToBinary(json, ghc, err, &st);
                std::lock_guard<std::mutex> lk(mtx_);
                Row& r = rows_[i];
                r.bad = !ok;
                if (ok) {
                    r.ghc = ghc;
                    r.ghcBytes = st.outBytes;
                    r.note = TextFormat("%.1fx smaller, %.2f s", double(st.inBytes) / double(std::max<uint64_t>(1, st.outBytes)), st.seconds);
                } else {
                    r.note = err;
                }
            }
            running_ = false;
        });
    }

    ModChoice mod_;
    std::vector<Row> rows_;
    int sel_ = 0;
    std::mutex mtx_;
    std::thread worker_;
    std::atomic<bool> running_{false}, stop_{false};
};

// ========================================================= script editor ==
// Issue #22: the mod's scripts (Lua, HScript) and shaders, edited in the
// game: a file list, line numbers, colours for keywords, strings and
// comments, undo, and saving with a .bak of the file as it was.

class ScriptEditorState : public State {
public:
    void enter() override {
        mod_ = currentMod();
        listFiles();
        if (!files_.empty()) open(0);
    }
    bool keyReserved(int) const override { return true; }  // every key types

    void update(double dt) override {
        App& a = app();
        Input& in = a.input;
        const bool ctrl = in.isDown(KEY_LEFT_CONTROL) || in.isDown(KEY_RIGHT_CONTROL);
        if (in.pressed(KEY_ESCAPE)) {
            if (dirty_ && !leaveArmed_) {
                status_ = "Unsaved changes: ESC again to leave without saving (CTRL+S saves)";
                leaveArmed_ = true;
            } else {
                a.change(makeEditorMenu());
            }
            return;
        }
        if (ctrl && in.pressed(KEY_S)) save();
        if (ctrl && in.pressed(KEY_Z)) undo();
        if (ctrl && in.pressed(KEY_N)) newFile();
        if (ctrl && (in.pressed(KEY_PAGE_UP) || in.pressed(KEY_PAGE_DOWN)) && !files_.empty()) {
            const int n = int(files_.size());
            open(size_t(((cur_ + (in.pressed(KEY_PAGE_UP) ? -1 : 1)) % n + n) % n));
            return;
        }
        mouse(in);
        if (lines_.empty()) lines_.push_back("");
        if (!ctrl)
            for (char32_t c : in.typed())
                if (c >= 32 && c != 127) insert(utf8(c));
        auto hit = [&](int key) { return repeat(in, key, dt); };
        if (hit(KEY_ENTER) || hit(KEY_KP_ENTER)) newline();
        if (hit(KEY_TAB)) insert("    ");
        if (hit(KEY_BACKSPACE)) backspace();
        if (hit(KEY_DELETE)) del();
        if (hit(KEY_LEFT)) left();
        if (hit(KEY_RIGHT)) right();
        if (hit(KEY_UP)) moveLine(-1);
        if (hit(KEY_DOWN)) moveLine(1);
        if (hit(KEY_PAGE_UP) && !ctrl) moveLine(-kRows);
        if (hit(KEY_PAGE_DOWN) && !ctrl) moveLine(kRows);
        if (in.pressed(KEY_HOME)) cx_ = 0;
        if (in.pressed(KEY_END)) cx_ = int(lines_[size_t(cy_)].size());
        if (cy_ < top_) top_ = cy_;
        if (cy_ >= top_ + kRows) top_ = cy_ - kRows + 1;
    }

    void draw() override {
        gfx::beginCanvas();
        ClearBackground(Color{14, 10, 22, 255});
        DrawRectangleRec({0, 0, gfx::W, gfx::H}, Color{14, 10, 22, 255});
        // Files
        DrawRectangleRec({0, 0, kListW, gfx::H}, Color{10, 7, 16, 255});
        gfx::text(FontId::Ui, "Scripts: " + modLabel(mod_), 12, 10, 18, kGold);
        for (size_t i = 0; i < files_.size() && i < 40; ++i) {
            const float y = 40.0f + float(i) * 17.0f;
            if (int(i) == cur_) DrawRectangleRec({4, y - 1, kListW - 8, 17}, fade(kGold, 0.2f));
            gfx::text(FontId::Ui, files_[i].label, 10, y, 14, int(i) == cur_ ? kGold : fade(kIvory, 0.75f));
        }
        // Text
        const float x0 = kListW + 56, y0 = 40;
        gfx::text(FontId::Ui, (cur_ >= 0 ? files_[size_t(cur_)].label : std::string("(no file)")) + (dirty_ ? "  *" : ""), kListW + 12,
                  10, 18, kIvory);
        for (int k = 0; k < kRows && top_ + k < int(lines_.size()); ++k) {
            const int ln = top_ + k;
            const float y = y0 + float(k) * kLineH;
            if (ln == cy_) DrawRectangleRec({kListW, y - 1, gfx::W - kListW, kLineH}, Color{30, 22, 46, 255});
            gfx::text(FontId::Ui, std::to_string(ln + 1), x0 - 10, y, 15, fade(kIvory, 0.35f), Align::Right);
            drawLine(lines_[size_t(ln)], x0, y);
        }
        if (cy_ >= top_ && cy_ < top_ + kRows && std::fmod(GetTime(), 1.0) < 0.6) {
            const std::string& l = lines_[size_t(cy_)];
            const float cx = x0 + gfx::measure(FontId::Ui, l.substr(0, size_t(std::min<int>(cx_, int(l.size())))), 16).x;
            DrawRectangleRec({cx, y0 + float(cy_ - top_) * kLineH, 2, kLineH - 2}, kGold);
        }
        if (!status_.empty()) gfx::text(FontId::Ui, status_, kListW + 12, gfx::H - 52, 16, kGold);
        hints("Type to edit   CTRL+S save   CTRL+Z undo   CTRL+N new script   CTRL+PGUP/PGDN file   click a file   ESC back");
        gfx::endCanvas();
    }
    TouchLayout touchLayout() const override { return {}; }

private:
    static constexpr float kListW = 300, kLineH = 19;
    static constexpr int kRows = 33;
    struct File {
        std::string path, label;
    };

    void listFiles() {
        files_.clear();
        static const char* exts[] = {".lua", ".hx", ".hxs", ".hscript", ".frag", ".vert"};
        for (const std::string& root : mod_.roots) {
            std::error_code ec;
            for (fs::recursive_directory_iterator it(toPath(root), ec), end; !ec && it != end && files_.size() < 2000; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const std::string p = pathStr(it->path());
                for (const char* e : exts)
                    if (endsWithI(p, e)) {
                        std::string label = pathStr(fs::relative(it->path(), toPath(root), ec));
                        files_.push_back({p, label});
                        break;
                    }
            }
        }
        std::sort(files_.begin(), files_.end(), [](const File& a, const File& b) { return a.label < b.label; });
    }

    void open(size_t i) {
        if (i >= files_.size()) return;
        if (dirty_) save();
        cur_ = int(i);
        lines_.clear();
        std::string text;
        if (std::FILE* f = std::fopen(files_[i].path.c_str(), "rb")) {
            char buf[65536];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        std::string line;
        for (char c : text) {
            if (c == '\r') continue;
            if (c == '\n') {
                lines_.push_back(line);
                line.clear();
            } else {
                line += c;
            }
        }
        lines_.push_back(line);
        cy_ = cx_ = top_ = 0;
        undo_.clear();
        dirty_ = leaveArmed_ = false;
        backedUp_ = false;
        status_.clear();
    }

    void save() {
        if (cur_ < 0) return;
        const std::string& p = files_[size_t(cur_)].path;
        std::error_code ec;
        if (!backedUp_ && fs::exists(toPath(p), ec) && !fs::exists(toPath(p + ".bak"), ec)) fs::copy_file(toPath(p), toPath(p + ".bak"), ec);
        backedUp_ = true;
        std::string text;
        for (size_t i = 0; i < lines_.size(); ++i) text += lines_[i] + (i + 1 < lines_.size() ? "\n" : "");
        std::FILE* f = std::fopen(p.c_str(), "wb");
        const bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
        if (f) std::fclose(f);
        status_ = ok ? "Saved " + files_[size_t(cur_)].label : "Could not save " + p;
        if (ok) dirty_ = false;
    }

    void newFile() {
        if (mod_.roots.empty()) return;
        if (dirty_) save();
        const fs::path dir = toPath(mod_.roots[0]) / "scripts";
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path p;
        for (int k = 1; k < 1000; ++k) {
            p = dir / ("new_script" + (k > 1 ? std::to_string(k) : std::string()) + ".lua");
            if (!fs::exists(p, ec)) break;
        }
        std::FILE* f = std::fopen(pathStr(p).c_str(), "wb");
        if (!f) {
            status_ = "Could not make " + pathStr(p);
            return;
        }
        const char* tmpl = "function onCreate()\n    -- runs when the song starts\nend\n\nfunction onUpdate(elapsed)\nend\n";
        std::fwrite(tmpl, 1, std::strlen(tmpl), f);
        std::fclose(f);
        listFiles();
        for (size_t i = 0; i < files_.size(); ++i)
            if (files_[i].path == pathStr(p)) open(i);
        status_ = "New script: " + pathStr(p.filename()) + " (scripts/ runs in every song of the mod)";
    }

    // A key held repeats (as in any text editor).
    bool repeat(const Input& in, int key, double dt) {
        if (in.pressed(key)) {
            held_[key] = 0;
            return true;
        }
        if (!in.isDown(key)) {
            held_.erase(key);
            return false;
        }
        double& t = held_[key];
        const double before = t;
        t += dt;
        return t > 0.4 && std::floor((t - 0.4) / 0.035) != std::floor((before - 0.4) / 0.035);
    }

    void mouse(const Input& in) {
        const Vector2 m = gfx::toCanvas(GetMousePosition());
        if (in.wheel() != 0 && m.x > kListW) {
            top_ = std::clamp(top_ - int(in.wheel() * 3), 0, std::max(0, int(lines_.size()) - 1));
        }
        if (!in.mousePressed(MOUSE_BUTTON_LEFT)) return;
        if (m.x < kListW) {
            const int i = int((m.y - 40) / 17);
            if (i >= 0 && i < int(files_.size())) open(size_t(i));
            return;
        }
        const int ln = top_ + int((m.y - 40) / kLineH);
        if (ln < 0 || ln >= int(lines_.size())) return;
        cy_ = ln;
        const std::string& l = lines_[size_t(ln)];
        const float x0 = kListW + 56;
        cx_ = int(l.size());
        for (size_t k = 0; k <= l.size(); ++k)
            if (x0 + gfx::measure(FontId::Ui, l.substr(0, k), 16).x > m.x) {
                cx_ = int(k == 0 ? 0 : k - 1);
                break;
            }
    }

    void remember() {
        undo_.push_back({lines_, {cy_, cx_}});
        if (undo_.size() > 200) undo_.erase(undo_.begin());
        dirty_ = true;
        leaveArmed_ = false;
    }
    void undo() {
        if (undo_.empty()) return;
        lines_ = std::move(undo_.back().first);
        cy_ = undo_.back().second.first;
        cx_ = undo_.back().second.second;
        undo_.pop_back();
        dirty_ = true;
    }
    std::string& line() { return lines_[size_t(cy_)]; }
    void clampCx() { cx_ = std::clamp(cx_, 0, int(line().size())); }
    void insert(const std::string& s) {
        clampCx();
        // One undo step per word typed, not per letter.
        if (undo_.empty() || s == " " || s.size() > 1) remember();
        line().insert(size_t(cx_), s);
        cx_ += int(s.size());
        dirty_ = true;
    }
    void newline() {
        remember();
        clampCx();
        std::string rest = line().substr(size_t(cx_));
        line().erase(size_t(cx_));
        size_t indent = 0;  // the new line starts where this one does
        while (indent < line().size() && (line()[indent] == ' ' || line()[indent] == '\t')) ++indent;
        const std::string pad = line().substr(0, indent);
        lines_.insert(lines_.begin() + cy_ + 1, pad + rest);
        ++cy_;
        cx_ = int(pad.size());
    }
    void backspace() {
        clampCx();
        if (cx_ > 0) {
            remember();
            int k = cx_ - 1;
            while (k > 0 && (static_cast<unsigned char>(line()[size_t(k)]) & 0xC0) == 0x80) --k;  // a whole UTF-8 letter
            line().erase(size_t(k), size_t(cx_ - k));
            cx_ = k;
        } else if (cy_ > 0) {
            remember();
            cx_ = int(lines_[size_t(cy_ - 1)].size());
            lines_[size_t(cy_ - 1)] += line();
            lines_.erase(lines_.begin() + cy_);
            --cy_;
        }
    }
    void del() {
        clampCx();
        if (cx_ < int(line().size())) {
            remember();
            int k = cx_ + 1;
            while (k < int(line().size()) && (static_cast<unsigned char>(line()[size_t(k)]) & 0xC0) == 0x80) ++k;
            line().erase(size_t(cx_), size_t(k - cx_));
        } else if (cy_ + 1 < int(lines_.size())) {
            remember();
            line() += lines_[size_t(cy_ + 1)];
            lines_.erase(lines_.begin() + cy_ + 1);
        }
    }
    void left() {
        clampCx();
        if (cx_ > 0) {
            --cx_;
            while (cx_ > 0 && (static_cast<unsigned char>(line()[size_t(cx_)]) & 0xC0) == 0x80) --cx_;
        } else if (cy_ > 0) {
            --cy_;
            cx_ = int(line().size());
        }
    }
    void right() {
        clampCx();
        if (cx_ < int(line().size())) {
            ++cx_;
            while (cx_ < int(line().size()) && (static_cast<unsigned char>(line()[size_t(cx_)]) & 0xC0) == 0x80) ++cx_;
        } else if (cy_ + 1 < int(lines_.size())) {
            ++cy_;
            cx_ = 0;
        }
    }
    void moveLine(int d) { cy_ = std::clamp(cy_ + d, 0, int(lines_.size()) - 1); }

    // Keywords, strings, numbers and comments in their colours.
    void drawLine(const std::string& l, float x, float y) const {
        static const std::set<std::string> kw = {
            "function", "end", "if", "then", "else", "elseif", "local", "return", "for", "in", "do", "while", "repeat",
            "until", "and", "or", "not", "nil", "true", "false", "var", "final", "new", "import", "class", "public",
            "private", "static", "override", "null", "switch", "case", "default", "break", "continue", "try", "catch",
            "uniform", "void", "float", "vec2", "vec3", "vec4", "int", "bool", "sampler2D", "this", "extends"};
        const bool luaFile = cur_ >= 0 && endsWithI(files_[size_t(cur_)].path, ".lua");
        size_t i = 0;
        float cx = x;
        auto put = [&](const std::string& s, Color c) {
            gfx::text(FontId::Ui, s, cx, y, 16, c);
            cx += gfx::measure(FontId::Ui, s, 16).x;
        };
        while (i < l.size() && cx < gfx::W) {
            const char c = l[i];
            const bool comment = luaFile ? l.compare(i, 2, "--") == 0 : l.compare(i, 2, "//") == 0;
            if (comment) {
                put(l.substr(i), Color{120, 130, 150, 255});
                break;
            }
            if (c == '"' || c == '\'') {
                size_t j = i + 1;
                while (j < l.size() && l[j] != c) j += l[j] == '\\' ? 2 : 1;
                j = std::min(j + 1, l.size());
                put(l.substr(i, j - i), Color{150, 220, 140, 255});
                i = j;
            } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                size_t j = i;
                while (j < l.size() && (std::isalnum(static_cast<unsigned char>(l[j])) || l[j] == '_')) ++j;
                const std::string w = l.substr(i, j - i);
                put(w, kw.count(w) ? Color{255, 170, 110, 255} : kInk);
                i = j;
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                size_t j = i;
                while (j < l.size() && (std::isalnum(static_cast<unsigned char>(l[j])) || l[j] == '.')) ++j;
                put(l.substr(i, j - i), Color{130, 190, 255, 255});
                i = j;
            } else {
                size_t j = i + 1;
                while (j < l.size() && !std::isalnum(static_cast<unsigned char>(l[j])) && l[j] != '_' && l[j] != '"' && l[j] != '\'' &&
                       l.compare(j, 2, luaFile ? "--" : "//") != 0)
                    ++j;
                put(l.substr(i, j - i), fade(kInk, 0.85f));
                i = j;
            }
        }
    }

    ModChoice mod_;
    std::vector<File> files_;
    int cur_ = -1;
    std::vector<std::string> lines_;
    int cy_ = 0, cx_ = 0, top_ = 0;
    std::vector<std::pair<std::vector<std::string>, std::pair<int, int>>> undo_;
    std::map<int, double> held_;
    bool dirty_ = false, leaveArmed_ = false, backedUp_ = false;
    std::string status_;
};

// ============================================================ editor menu ==

class EditorMenuState : public State {
public:
    void update(double) override {
        App& a = app();
        Input& in = a.input;
        const int mods = int(modChoices().size());
        if (in.up()) { sel_ = (sel_ + kItems - 1) % kItems; sfxPlay(Sfx::Scroll); }
        if (in.down()) { sel_ = (sel_ + 1) % kItems; sfxPlay(Sfx::Scroll); }
        if ((in.left() || in.right()) && mods > 1) {
            int& m = modIndex();
            m = ((m + (in.left() ? -1 : 1)) % mods + mods) % mods;
            sfxPlay(Sfx::Scroll);
        }
        if (in.back()) {
            sfxPlay(Sfx::Cancel);
            a.change(makeMainMenu());
            return;
        }
        if (!in.accept()) return;
        sfxPlay(Sfx::Confirm);
        switch (sel_) {
            case 0: openChartEditor(); break;
            case 1: a.change(makeCharacterEditor()); break;
            case 2: a.change(makeStageEditor()); break;
            case 3: a.change(makeEventEditor()); break;
            case 4: a.change(makeScriptEditor()); break;
            default: a.change(makeChartConverter()); break;
        }
    }
    void draw() override {
        App& a = app();
        gfx::beginCanvas();
        drawMenuBackground(a.now, 0x6C8CFF, menuPulse());
        gfx::text(FontId::Title, "EDITORS", gfx::W / 2, 60, 64, kGold, Align::Center, 2.0f);
        static const char* items[kItems] = {"Chart Editor", "Character Editor", "Stage Editor", "Event Editor", "Script Editor",
                                            "Chart Converter"};
        static const char* about[kItems] = {"A song of this mod (the one chosen in Freeplay if it is here)",
                                            "Animations, offsets, image, icon and colours of a character",
                                            "Zoom, where the characters stand and what the camera sees",
                                            "New events for charts: a description and a Lua script",
                                            "The mod's Lua and HScript scripts and its shaders",
                                            "The mod's charts to binary .ghc files: much smaller, loaded faster"};
        for (int i = 0; i < kItems; ++i) {
            const bool on = i == sel_;
            const float y = 140.0f + i * 74.0f;
            gfx::text(FontId::Title, items[i], gfx::W / 2, y, on ? 50.0f : 40.0f, on ? kGold : fade(kIvory, 0.55f), Align::Center, 2.0f);
            if (on) gfx::text(FontId::Ui, about[i], gfx::W / 2, y + 50, 18, fade(kIvory, 0.85f), Align::Center, 1.5f);
        }
        const ModChoice m = currentMod();
        gfx::text(FontId::Ui, "<  Mod: " + modLabel(m) + "  >", gfx::W / 2, gfx::H - 112, 26, kIvory, Align::Center, 1.5f);
        gfx::text(FontId::Ui, "The editors save into " + m.roots[0], gfx::W / 2, gfx::H - 80, 16, fade(kIvory, 0.6f), Align::Center, 1.0f);
        hints("UP/DOWN choose   LEFT/RIGHT mod   ENTER open   ESC back");
        gfx::endCanvas();
    }
    TouchLayout touchLayout() const override { return {}; }

private:
    static constexpr int kItems = 6;
    int sel_ = 0;

    void openChartEditor() {
        App& a = app();
        const ModChoice m = currentMod();
        const auto& songs = a.library.songs();
        const SongEntry* pick = nullptr;
        if (a.freeplaySel >= 0 && a.freeplaySel < int(songs.size())) {
            const SongEntry& e = songs[size_t(a.freeplaySel)];
            if (!e.builtin && (m.name.empty() || e.pack == m.name)) pick = &e;
        }
        for (size_t i = 0; !pick && i < songs.size(); ++i)
            if (!songs[i].builtin && !songs[i].diffs.empty() && (m.name.empty() || songs[i].pack == m.name)) pick = &songs[i];
        if (!pick || pick->diffs.empty()) {
            a.notify("No song in this mod to chart: choose one in Freeplay and press 7 there");
            a.change(makeFreeplay());
            return;
        }
        const int d = clampv(a.freeplayDiff, 0, int(pick->diffs.size()) - 1);
        a.change(makeChartEditor(pick->id, pick->diffs[size_t(d)]));
    }
};

// ======================================================= character editor ==

class CharacterEditorState : public State {
public:
    void enter() override {
        mod_ = currentMod();
        useMod(sc_, mod_);
        names_ = listNames(sc_, "characters/", ".json");
        auto it = std::find(names_.begin(), names_.end(), "bf");
        load(it != names_.end() ? *it : names_.empty() ? std::string("bf") : names_[0]);
    }
    bool keyReserved(int) const override { return !ui_.focus.empty(); }

    void update(double dt) override {
        App& a = app();
        Input& in = a.input;
        feedUi(ui_);
        if (ch_) ch_->updateAnim(dt);
        view_.handle(in, ui_, overPanel(ui_));
        if (!ui_.focus.empty()) return;
        const bool shift = in.isDown(KEY_LEFT_SHIFT) || in.isDown(KEY_RIGHT_SHIFT);
        const bool ctrl = in.isDown(KEY_LEFT_CONTROL) || in.isDown(KEY_RIGHT_CONTROL);
        if (in.pressed(KEY_ESCAPE)) {
            if (dirty_ && !leaveArmed_) {
                status_.set("Unsaved changes: ESC again to leave without saving (CTRL+S saves)");
                leaveArmed_ = true;
            } else {
                a.change(makeEditorMenu());
            }
            return;
        }
        if (ctrl && in.pressed(KEY_S)) save();
        const int n = int(def_.anims.size());
        if (n > 0 && (in.pressed(KEY_W) || in.pressed(KEY_S)) && !ctrl) selectAnim((anim_ + (in.pressed(KEY_W) ? n - 1 : 1)) % n);
        if (in.pressed(KEY_SPACE)) playSelected();
        if (in.pressed(KEY_G)) ghostOn_ = !ghostOn_;
        if (in.pressed(KEY_R) && !ctrl) resetView();
        const int step = shift ? 10 : 1;
        int dx = 0, dy = 0;
        if (in.pressed(KEY_LEFT)) dx += step;  // Psych: the arrows move the picture, its offset goes the other way
        if (in.pressed(KEY_RIGHT)) dx -= step;
        if (in.pressed(KEY_UP)) dy += step;
        if (in.pressed(KEY_DOWN)) dy -= step;
        if ((dx || dy) && anim_ < n) {
            psych::AnimDef& ad = def_.anims[size_t(anim_)];
            ad.offX += float(dx);
            ad.offY += float(dy);
            offsetsChanged();
        }
    }

    void draw() override {
        gfx::beginCanvas();
        view_.apply(sc_.camGame);
        drawGrid(view_);
        rend_.drawCamera(sc_, scene::Cam::Game, nullptr, nullptr);
        if (ch_) {
            // Where the character stands (its position) and where the camera looks.
            cross(view_.toCanvas(ch_->x, ch_->y), 10, Color{120, 200, 255, 255});
            const float mx = ch_->x + ch_->width * 0.5f, my = ch_->y + ch_->height * 0.5f;
            const Vector2 camPt = player_ ? Vector2{mx - 100 - def_.camX, my - 100 + def_.camY} : Vector2{mx + 150 + def_.camX, my - 100 + def_.camY};
            cross(view_.toCanvas(camPt.x, camPt.y), 16, kGold);
        } else {
            gfx::text(FontId::Ui, error_.empty() ? "No character" : error_, 430, 340, 22, Color{255, 140, 140, 255}, Align::Center, 1.5f);
        }
        drawInfo();
        drawPanel();
        status_.draw();
        gfx::endCanvas();
    }

private:
    scene::Scene sc_;
    SceneRenderer rend_;
    Ui ui_;
    View view_;
    Status status_;
    ModChoice mod_;
    std::vector<std::string> names_;
    int nameIdx_ = 0;
    psych::CharacterDef def_;
    std::string loadedPath_, loadedText_, saveName_, image_, icon_, error_;
    scene::CharacterPtr ch_, ghost_;
    std::vector<std::string> prefixes_;
    int anim_ = 0, tab_ = 0, prefixIdx_ = 0;
    bool player_ = true, ghostOn_ = true, dirty_ = false, leaveArmed_ = false;
    std::string fName_, fPrefix_, fIndices_;
    double fFps_ = 24;
    bool fLoop_ = false;
    float listScroll_ = 0;

    void load(const std::string& name) {
        std::string err;
        if (!loadCharacterDef(sc_, name, def_, loadedPath_, loadedText_, err)) {
            status_.set(err, true);
            if (def_.images.empty()) def_.images = {"characters/" + name};
        }
        saveName_ = name;
        auto it = std::find(names_.begin(), names_.end(), name);
        nameIdx_ = it == names_.end() ? 0 : int(it - names_.begin());
        player_ = name.rfind("bf", 0) == 0 || name.find("player") != std::string::npos || name.find("boyfriend") != std::string::npos;
        anim_ = 0;
        dirty_ = leaveArmed_ = false;
        syncFields();
        rebuild();
        resetView();
    }

    void syncFields() {
        image_ = imageKeys(def_);
        icon_ = def_.icon;
        fillAnimFields();
    }

    void fillAnimFields() {
        if (anim_ >= int(def_.anims.size())) return;
        const psych::AnimDef& ad = def_.anims[size_t(anim_)];
        fName_ = ad.anim;
        fPrefix_ = ad.prefix;
        fFps_ = ad.fps;
        fLoop_ = ad.loop;
        fIndices_.clear();
        for (size_t i = 0; i < ad.indices.size(); ++i) fIndices_ += (i ? "," : "") + std::to_string(ad.indices[i]);
    }

    // Base positions as in a song: the player at the stage's (770, 100), the opponent at (100, 100).
    Vector2 basePos() const { return player_ ? Vector2{770, 100} : Vector2{100, 100}; }

    void rebuild() {
        error_.clear();
        ch_ = buildCharacter(sc_, def_, player_, error_);
        ghost_.reset();
        sc_.order.clear();
        prefixes_.clear();
        if (!ch_) return;
        const Vector2 b = basePos();
        ch_->x = b.x + def_.posX;
        ch_->y = b.y + def_.posY;
        if (ch_->graphic) prefixes_ = atlasPrefixes(*ch_->graphic);
        // The ghost: the first animation's first frame, faint, to line the others up against.
        ghost_ = buildCharacter(sc_, def_, player_, error_);
        if (ghost_) {
            ghost_->x = ch_->x;
            ghost_->y = ch_->y;
            ghost_->alpha = 0.33f;
            ghost_->paused = true;
            ghost_->visible = ghostOn_;
            sc_.order.push_back(ghost_);
        }
        sc_.order.push_back(ch_);
        playSelected();
    }

    void resetView() {
        if (ch_) {
            view_.x = ch_->x + ch_->width * 0.5f;
            view_.y = ch_->y + ch_->height * 0.5f;
        } else {
            view_.x = 640;
            view_.y = 360;
        }
        view_.zoom = 1;
        view_.x -= (kPanelX - gfx::W) * 0.5f / view_.zoom;  // centred in the part left of the panel
    }

    void selectAnim(int i) {
        anim_ = clampv(i, 0, std::max(0, int(def_.anims.size()) - 1));
        fillAnimFields();
        playSelected();
    }

    void playSelected() {
        if (!ch_ || anim_ >= int(def_.anims.size())) return;
        ch_->playAnim(def_.anims[size_t(anim_)].anim, true);
        if (ghost_) ghost_->visible = ghostOn_;
    }

    void offsetsChanged() {
        dirty_ = true;
        leaveArmed_ = false;
        if (!ch_ || anim_ >= int(def_.anims.size())) return;
        const psych::AnimDef& ad = def_.anims[size_t(anim_)];
        for (scene::CharacterPtr c : {ch_, ghost_}) {
            if (!c) continue;
            if (scene::Anim* a = c->anim(ad.anim)) {
                a->offX = ad.offX;
                a->offY = ad.offY;
            }
            if (c->curAnimName() == ad.anim) {
                c->offsetX = ad.offX;
                c->offsetY = ad.offY;
            }
        }
    }

    void changed() {
        dirty_ = true;
        leaveArmed_ = false;
        rebuild();
    }

    void drawInfo() {
        std::string l1 = "Animation: none";
        if (anim_ < int(def_.anims.size())) {
            const psych::AnimDef& ad = def_.anims[size_t(anim_)];
            l1 = "Animation: " + ad.anim + "   offset [" + jnum(ad.offX) + ", " + jnum(ad.offY) + "]";
            if (ch_ && !ch_->hasAnim(ad.anim)) l1 += "   (no frames named \"" + ad.prefix + "\" in the image)";
        }
        DrawRectangleRec({8, 8, 856, 56}, fade(BLACK, 0.55f));
        gfx::text(FontId::Ui, l1, 18, 13, 20, kInk);
        gfx::text(FontId::Ui, saveName_ + (dirty_ ? "  (not saved)" : "") + "   mod: " + modLabel(mod_), 18, 38, 16, kDim);
        hints("W/S animation  SPACE play  ARROWS offset (SHIFT x10)  G ghost  RIGHT-DRAG move  WHEEL zoom  R reset  CTRL+S save");
    }

    void drawPanel() {
        panelBox(8, gfx::H - 40);
        float y = 16;
        const float x = kPanelX, w = kPanelW, rh = 30;
        if (ui_.button({x, y, w * 0.5f - 4, rh}, "Character", tab_ == 0)) tab_ = 0;
        if (ui_.button({x + w * 0.5f + 4, y, w * 0.5f - 4, rh}, "Animations", tab_ == 1)) tab_ = 1;
        y += rh + 12;
        if (tab_ == 0) drawCharacterTab(x, y, w, rh);
        else drawAnimTab(x, y, w, rh);
        ui_.overlays();
    }

    void drawCharacterTab(float x, float y, float w, float rh) {
        if (!names_.empty()) {
            if (ui_.dropdown("char", {x, y, w, rh}, "Character", names_, nameIdx_)) load(names_[size_t(nameIdx_)]);
            y += rh + 6;
        }
        ui_.text("save", {x, y, w, rh}, "Save as", saveName_);
        y += rh + 6;
        ui_.text("image", {x, y, w - 92, rh}, "Image", image_);
        if (ui_.button({x + w - 86, y, 86, rh}, "Reload")) {
            def_.images.clear();
            for (std::string part; const char ch : image_ + ",") {
                if (ch == ',') {
                    if (!trim(part).empty()) def_.images.push_back(trim(part));
                    part.clear();
                } else {
                    part += ch;
                }
            }
            changed();
        }
        y += rh + 6;
        if (ui_.text("icon", {x, y, w - 92, rh}, "Health icon", icon_)) {
            def_.icon = trim(icon_);
            dirty_ = true;
        }
        const std::string ip = iconPath(sc_, def_.icon);
        if (Texture2D* t = ip.empty() ? nullptr : rend_.texture(ip)) {
            const float fw = t->width >= t->height * 2 ? t->width / 2.0f : float(t->width);
            DrawTexturePro(*t, {0, 0, fw, float(t->height)}, {x + w - 80, y - 14, 58, 58 * t->height / fw}, {0, 0}, 0, WHITE);
        }
        y += rh + 22;
        double v = def_.scale;
        if (ui_.stepper({x, y, w, rh}, "Scale", v, 0.05, 0.05, 20, "%.2f")) {
            def_.scale = v;
            changed();
        }
        y += rh + 6;
        bool flip = def_.flipX, noAa = !def_.antialiasing;
        if (ui_.check({x, y, w * 0.5f, rh}, "Flip X", flip)) {
            def_.flipX = flip;
            changed();
        }
        if (ui_.check({x + w * 0.5f, y, w * 0.5f, rh}, "No antialiasing", noAa)) {
            def_.antialiasing = !noAa;
            changed();
        }
        y += rh + 6;
        auto posStep = [&](const char* label, float& f) {
            double d = f;
            if (ui_.stepper({x, y, w, rh}, label, d, 1, -10000, 10000, "%.0f")) {
                f = float(d);
                changed();
            }
            y += rh + 6;
        };
        posStep("Position X", def_.posX);
        posStep("Position Y", def_.posY);
        posStep("Camera X", def_.camX);
        posStep("Camera Y", def_.camY);
        double sd = def_.singDuration;
        if (ui_.stepper({x, y, w, rh}, "Sing duration", sd, 0.1, 0, 100, "%.1f")) {
            def_.singDuration = sd;
            dirty_ = true;
        }
        y += rh + 6;
        // Health bar colour: red, green, blue, and the icon's own colour.
        double rgb[3] = {double(def_.barColor >> 16 & 255), double(def_.barColor >> 8 & 255), double(def_.barColor & 255)};
        static const char* names[3] = {"Bar red", "Bar green", "Bar blue"};
        for (int i = 0; i < 3; ++i) {
            if (ui_.stepper({x, y, w - 46, rh}, names[i], rgb[i], 1, 0, 255, "%.0f")) {
                def_.barColor = uint32_t(rgb[0]) << 16 | uint32_t(rgb[1]) << 8 | uint32_t(rgb[2]);
                dirty_ = true;
            }
            y += rh + 4;
        }
        DrawRectangleRounded({x + w - 40, y - 3 * (rh + 4), 40, 3 * rh + 8}, 0.2f, 4, gfx::rgb(def_.barColor));
        if (ui_.button({x, y, w * 0.5f - 4, rh}, "Colour from icon")) {
            uint32_t c = 0;
            if (!ip.empty() && iconColor(ip, c)) {
                def_.barColor = c;
                dirty_ = true;
            } else {
                status_.set("No icon to take a colour from", true);
            }
        }
        bool pl = player_;
        if (ui_.check({x + w * 0.5f + 4, y, w * 0.5f, rh}, "As the player", pl)) {
            player_ = pl;
            rebuild();
        }
        y += rh + 10;
        if (ui_.button({x, y, w * 0.5f - 4, rh + 4}, "Save")) save();
        if (ui_.button({x + w * 0.5f + 4, y, w * 0.5f - 4, rh + 4}, "New character")) newCharacter();
    }

    void drawAnimTab(float x, float y, float w, float rh) {
        // The animations: a click picks and plays one.
        const float listH = 210;
        DrawRectangleRec({x, y, w, listH}, Color{20, 14, 32, 255});
        const int n = int(def_.anims.size());
        const float row = 24;
        const int rows = int(listH / row);
        if (CheckCollisionPointRec(ui_.mouse, {x, y, w, listH}) && ui_.wheel != 0) {
            listScroll_ = clampv(listScroll_ - ui_.wheel * 2, 0.0f, float(std::max(0, n - rows)));
            ui_.used = true;
        }
        const int first = int(listScroll_);
        for (int k = 0; k < rows && first + k < n; ++k) {
            const int i = first + k;
            const Rectangle r{x + 2, y + 2 + k * row, w - 4, row - 2};
            const bool hov = ui_.hover(r);
            if (i == anim_ || hov) DrawRectangleRec(r, i == anim_ ? Color{90, 66, 140, 255} : Color{50, 38, 76, 255});
            const psych::AnimDef& ad = def_.anims[size_t(i)];
            const bool ok = ch_ && ch_->hasAnim(ad.anim);
            gfx::text(FontId::Ui, ad.anim, r.x + 6, r.y + 2, 18, ok ? kInk : Color{255, 130, 130, 255});
            gfx::text(FontId::Ui, "[" + jnum(ad.offX) + ", " + jnum(ad.offY) + "]", r.x + r.width - 8, r.y + 2, 16, kDim, Align::Right);
            if (hov && ui_.click) {
                ui_.used = true;
                selectAnim(i);
            }
        }
        y += listH + 10;
        ui_.text("aname", {x, y, w, rh}, "Name", fName_);
        y += rh + 6;
        ui_.text("aprefix", {x, y, w, rh}, "Prefix", fPrefix_);
        y += rh + 6;
        if (!prefixes_.empty()) {
            if (ui_.dropdown("prefixes", {x, y, w, rh}, "In image", prefixes_, prefixIdx_)) fPrefix_ = prefixes_[size_t(prefixIdx_)];
            y += rh + 6;
        }
        ui_.stepper({x, y, w, rh}, "FPS", fFps_, 1, 1, 240, "%.0f");
        y += rh + 6;
        ui_.check({x, y, w * 0.4f, rh}, "Loop", fLoop_);
        ui_.text("aind", {x + w * 0.4f, y, w * 0.6f, rh}, "Indices", fIndices_);
        y += rh + 10;
        if (ui_.button({x, y, w * 0.5f - 4, rh}, "Add / update")) addOrUpdate();
        if (ui_.button({x + w * 0.5f + 4, y, w * 0.5f - 4, rh}, "Remove")) removeAnim();
        y += rh + 8;
        if (anim_ < n) {
            psych::AnimDef& ad = def_.anims[size_t(anim_)];
            double ox = ad.offX, oy = ad.offY;
            if (ui_.stepper({x, y, w, rh}, "Offset X", ox, 1, -5000, 5000, "%.0f")) {
                ad.offX = float(ox);
                offsetsChanged();
            }
            y += rh + 6;
            if (ui_.stepper({x, y, w, rh}, "Offset Y", oy, 1, -5000, 5000, "%.0f")) {
                ad.offY = float(oy);
                offsetsChanged();
            }
            y += rh + 6;
        }
        bool g = ghostOn_;
        if (ui_.check({x, y, w, rh}, "Ghost (first animation)", g)) {
            ghostOn_ = g;
            if (ghost_) ghost_->visible = g;
        }
    }

    void addOrUpdate() {
        const std::string name = trim(fName_);
        if (name.empty() || trim(fPrefix_).empty()) {
            status_.set("An animation needs a name and a prefix", true);
            return;
        }
        psych::AnimDef ad;
        ad.anim = name;
        ad.prefix = trim(fPrefix_);
        ad.fps = fFps_;
        ad.loop = fLoop_;
        std::string part;
        for (char c : fIndices_ + ",") {
            if (c == ',') {
                if (!trim(part).empty()) ad.indices.push_back(std::atoi(trim(part).c_str()));
                part.clear();
            } else {
                part += c;
            }
        }
        auto it = std::find_if(def_.anims.begin(), def_.anims.end(), [&](const psych::AnimDef& a) { return a.anim == name; });
        if (it != def_.anims.end()) {
            ad.offX = it->offX;
            ad.offY = it->offY;
            *it = ad;
            anim_ = int(it - def_.anims.begin());
        } else {
            def_.anims.push_back(ad);
            anim_ = int(def_.anims.size()) - 1;
        }
        changed();
        if (ch_ && !ch_->hasAnim(name)) status_.set("No frames called \"" + ad.prefix + "\" in the image", true);
    }

    void removeAnim() {
        if (anim_ >= int(def_.anims.size())) return;
        def_.anims.erase(def_.anims.begin() + anim_);
        anim_ = std::max(0, anim_ - 1);
        fillAnimFields();
        changed();
    }

    void newCharacter() {
        def_ = psych::CharacterDef{};
        def_.images = {"characters/BOYFRIEND"};
        def_.icon = "face";
        psych::AnimDef idle;
        idle.anim = "idle";
        idle.prefix = "BF idle dance";
        def_.anims.push_back(idle);
        loadedPath_.clear();
        loadedText_.clear();
        saveName_ = "new-character";
        anim_ = 0;
        syncFields();
        changed();
        resetView();
    }

    void save() {
        const std::string name = fileName(saveName_);
        if (name.empty()) {
            status_.set("Type a name to save as", true);
            return;
        }
        std::string anims = "[\n";
        for (size_t i = 0; i < def_.anims.size(); ++i) {
            const psych::AnimDef& ad = def_.anims[i];
            std::string ind = "[";
            for (size_t k = 0; k < ad.indices.size(); ++k) ind += (k ? ", " : "") + std::to_string(ad.indices[k]);
            ind += "]";
            anims += "\t\t{\n\t\t\t\"anim\": " + jsonStr(ad.anim) + ",\n\t\t\t\"name\": " + jsonStr(ad.prefix) + ",\n\t\t\t\"fps\": " +
                     jnum(ad.fps) + ",\n\t\t\t\"loop\": " + (ad.loop ? "true" : "false") + ",\n\t\t\t\"indices\": " + ind +
                     ",\n\t\t\t\"offsets\": " + pair(ad.offX, ad.offY) + "\n\t\t}" + (i + 1 < def_.anims.size() ? ",\n" : "\n");
        }
        anims += "\t]";
        const uint32_t c = def_.barColor;
        const std::vector<std::pair<std::string, std::string>> fields = {
            {"animations", anims},
            {"image", jsonStr(imageKeys(def_))},
            {"scale", jnum(def_.scale)},
            {"sing_duration", jnum(def_.singDuration)},
            {"healthicon", jsonStr(def_.icon)},
            {"position", pair(def_.posX, def_.posY)},
            {"camera_position", pair(def_.camX, def_.camY)},
            {"flip_x", def_.flipX ? "true" : "false"},
            {"no_antialiasing", def_.antialiasing ? "false" : "true"},
            {"healthbar_colors", "[" + std::to_string(c >> 16 & 255) + ", " + std::to_string(c >> 8 & 255) + ", " + std::to_string(c & 255) + "]"},
            {"_editor_isPlayer", player_ ? "true" : "false"},
        };
        // The same file when it is in this mod; a new one in the mod otherwise.
        const std::string path = mod_.roots[0] + "characters/" + name + ".json";
        std::string err;
        if (!saveText(path, writeObject(fields, loadedText_), err)) {
            status_.set(err, true);
            return;
        }
        scene::readFile(path, loadedText_);
        loadedPath_ = path;
        dirty_ = leaveArmed_ = false;
        if (std::find(names_.begin(), names_.end(), name) == names_.end()) names_.push_back(name);
        status_.set("Saved " + path);
    }
};

// =========================================================== stage editor ==

class StageEditorState : public State {
public:
    void enter() override {
        mod_ = currentMod();
        scene::Scene probe;
        useMod(probe, mod_);
        names_ = listNames(probe, "stages/", ".json");
        auto it = std::find(names_.begin(), names_.end(), "stage");
        load(it != names_.end() ? *it : names_.empty() ? std::string("stage") : names_[0]);
    }
    bool keyReserved(int) const override { return !ui_.focus.empty(); }

    void update(double dt) override {
        App& a = app();
        Input& in = a.input;
        feedUi(ui_);
        if (sc_) runStage(dt);
        view_.handle(in, ui_, overPanel(ui_));
        if (tab_ == 4) dragObjects(in);
        else dragCharacters(in);
        if (!ui_.focus.empty()) return;
        const bool ctrl = in.isDown(KEY_LEFT_CONTROL) || in.isDown(KEY_RIGHT_CONTROL);
        if (in.pressed(KEY_ESCAPE)) {
            if (dirty_ && !leaveArmed_) {
                status_.set("Unsaved changes: ESC again to leave without saving (CTRL+S saves)");
                leaveArmed_ = true;
            } else {
                a.change(makeEditorMenu());
            }
            return;
        }
        if (ctrl && in.pressed(KEY_S)) save();
        if (in.pressed(KEY_R) && !ctrl) resetView();
        if (in.pressed(KEY_C)) showCam_ = !showCam_;
    }

    void draw() override {
        if (sc_) rend_.prepareShaderLayers(*sc_);
        gfx::beginCanvas();
        drawGrid(view_);
        if (sc_) {
            view_.apply(sc_->camGame);
            if (sc_->camGame.filters.empty()) {
                rend_.drawCamera(*sc_, scene::Cam::Game, nullptr, nullptr);
            } else {
                // The stage's camera shaders, as in a song: the camera to a
                // texture, then through them.
                gfx::endCanvas();
                const float cs = gfx::canvasScale();
                const Vector2 origin = gfx::canvasOrigin();
                const int rw = std::max(1, int(gfx::W * cs)), rh = std::max(1, int(gfx::H * cs));
                if (!camRt_.id || camRt_.texture.width != rw || camRt_.texture.height != rh) {
                    if (camRt_.id) UnloadRenderTexture(camRt_);
                    camRt_ = LoadRenderTexture(rw, rh);
                }
                gfx::beginTexture(camRt_);
                ClearBackground(BLANK);
                gfx::setRenderTargetSize(rw, rh);
                gfx::beginCanvas();
                rend_.drawCamera(*sc_, scene::Cam::Game, nullptr, nullptr);
                gfx::endCanvas();
                gfx::setRenderTargetSize(0, 0);
                gfx::endTexture();
                rend_.drawFiltered(*sc_, sc_->camGame.filters, camRt_, Rectangle{origin.x, origin.y, float(rw), float(rh)});
                gfx::beginCanvas();
            }
            static const Color cols[3] = {Color{120, 200, 255, 255}, Color{255, 140, 200, 255}, Color{200, 140, 255, 255}};
            for (int i = 0; i < 3; ++i) {
                if (i == 1 && def_.hideGf) continue;
                const float* p = pos(i);
                cross(view_.toCanvas(p[0], p[1]), 10, cols[i]);
                gfx::text(FontId::Ui, kWho[i], view_.toCanvas(p[0], p[1]).x + 12, view_.toCanvas(p[0], p[1]).y + 4, 16, cols[i]);
                if (const scene::CharacterPtr& c = chars_[i]; c && i == tab_ - 1) {
                    const Vector2 a = view_.toCanvas(c->x, c->y), b = view_.toCanvas(c->x + c->width, c->y + c->height);
                    DrawRectangleLinesEx({a.x, a.y, b.x - a.x, b.y - a.y}, 1.5f, cols[i]);
                }
            }
            if (showCam_) drawCameraFrame();
        }
        DrawRectangleRec({8, 8, 856, 32}, fade(BLACK, 0.55f));
        gfx::text(FontId::Ui, saveName_ + (dirty_ ? "  (not saved)" : "") + "   mod: " + modLabel(mod_) + "   zoom " + jnum(def_.zoom), 18, 13,
                  18, kInk);
        hints("LEFT-DRAG a character to move it   RIGHT-DRAG move the view   WHEEL zoom   C camera   R reset   CTRL+S save");
        drawPanel();
        status_.draw();
        gfx::endCanvas();
    }

    ~StageEditorState() override {
        if (camRt_.id && IsWindowReady()) UnloadRenderTexture(camRt_);
    }

private:
    // The stage runs as in a song (at 100 BPM): tweens, timers, animations,
    // shaders, and its script's onUpdate, onStepHit and onBeatHit; the
    // characters dance on the beat.
    void runStage(double dt) {
        scene::Scene& sc = *sc_;
        const double stepMs = 60000.0 / kPreviewBpm / 4.0;
        const int stepBefore = int(std::floor(game_.clockMs / stepMs));
        game_.clockMs += dt * 1000.0;
        const float sx = sc.camGame.scrollX, sy = sc.camGame.scrollY, z = sc.camGame.zoom;
        sc.update(dt, stepMs, false);
        sc.camGame.scrollX = sx;  // the view is the editor's
        sc.camGame.scrollY = sy;
        sc.camGame.zoom = z;
        if (!host_) return;
        host_->setGlobal("songPosition", game_.clockMs);
        host_->call("onUpdate", {scene::Value(dt)});
        const int stepNow = int(std::floor(game_.clockMs / stepMs));
        for (int st = stepBefore + 1; st <= stepNow && st <= stepBefore + 4; ++st) {
            host_->setGlobal("curStep", double(st));
            host_->setGlobal("curBeat", double(st / 4));
            host_->call("onStepHit");
            if (st % 4 == 0) {
                for (const scene::CharacterPtr& c : chars_)
                    if (c) c->dance();
                host_->call("onBeatHit");
            }
        }
        host_->call("onUpdatePost", {scene::Value(dt)});
        host_->dispatchCompletions();
    }

    static constexpr double kPreviewBpm = 100;
    static constexpr const char* kWho[3] = {"Boyfriend", "Girlfriend", "Opponent"};
    std::unique_ptr<scene::Scene> sc_;
    std::unique_ptr<ScriptHost> host_;
    EditorGame game_;
    RenderTexture2D camRt_{};
    SceneRenderer rend_;
    Ui ui_;
    View view_;
    Status status_;
    ModChoice mod_;
    std::vector<std::string> names_;
    int nameIdx_ = 0, tab_ = 0, camWho_ = 0;
    psych::StageDef def_;
    std::string loadedText_, saveName_;
    std::string who_[3] = {"bf", "gf", "dad"};
    scene::CharacterPtr chars_[3];
    std::vector<scene::SpritePtr> objSprites_;  // per stage object (null for the characters' places)
    int objSel_ = 0;
    bool objDrag_ = false;
    bool dirty_ = false, leaveArmed_ = false, showCam_ = true;
    int drag_ = -1;
    Vector2 dragLast_{};

    float* pos(int i) { return i == 0 ? def_.bf : i == 1 ? def_.gf : def_.dad; }
    float* camOff(int i) { return i == 0 ? def_.camBf : i == 1 ? def_.camGf : def_.camDad; }

    void load(const std::string& name) {
        saveName_ = name;
        auto it = std::find(names_.begin(), names_.end(), name);
        nameIdx_ = it == names_.end() ? 0 : int(it - names_.begin());
        def_ = psych::StageDef{};
        loadedText_.clear();
        scene::Scene probe;
        useMod(probe, mod_);
        const std::string path = probe.files.find("stages/" + name + ".json");
        std::string err;
        if (!path.empty() && (!scene::readFile(path, loadedText_) || !psych::parseStage(loadedText_, def_, err)))
            status_.set("stages/" + name + ".json: " + err, true);
        dirty_ = leaveArmed_ = false;
        buildScene(name);
        resetView();
    }

    // The stage as a song shows it: its JSON objects, the three characters and
    // what its Lua script makes.
    void buildScene(const std::string& name) {
        host_.reset();
        rend_.clear();
        sc_ = std::make_unique<scene::Scene>();
        scene::Scene& sc = *sc_;
        useMod(sc, mod_);
        sc.stage = def_;
        sc.gfMarker = std::make_shared<scene::Sprite>();
        sc.gfMarker->kind = scene::Kind::Marker;
        sc.gfMarker->tag = "gfGroup";
        for (int i = 0; i < 3; ++i) makeChar(i);
        bool placed = false;
        objSprites_.clear();
        for (const psych::StageObject& o : def_.objects) {
            objSprites_.push_back(nullptr);
            const std::string t = toLower(o.type);
            if (t == "gf" || t == "gfgroup") {
                sc.order.push_back(sc.gfMarker);
                if (chars_[1]) sc.order.push_back(chars_[1]);
                placed = true;
                continue;
            }
            if (t == "dad" || t == "dadgroup") {
                if (chars_[2]) sc.order.push_back(chars_[2]);
                continue;
            }
            if (t == "boyfriend" || t == "boyfriendgroup") {
                if (chars_[0]) sc.order.push_back(chars_[0]);
                continue;
            }
            auto s = std::make_shared<scene::Sprite>();
            s->tag = o.name;
            if (t == "square") s->setGraphic(sc.gfx.solid(1, 1, 0xFF000000u | o.color));
            else if (t == "animatedsprite") s->setGraphic(sc.gfx.atlas(o.image));
            else s->setGraphic(sc.gfx.image(o.image));
            for (const psych::AnimDef& ad : o.anims) {
                if (!s->graphic) break;
                scene::Anim an;
                an.name = ad.anim;
                an.frames = ad.indices.empty() ? psych::framesByPrefix(s->graphic->frames, ad.prefix)
                                               : psych::framesByIndices(s->graphic->frames, ad.prefix, ad.indices);
                an.fps = ad.fps;
                an.loop = ad.loop;
                if (!an.frames.empty()) s->addAnim(std::move(an));
            }
            if (!o.firstAnim.empty()) s->play(o.firstAnim, true);
            s->x = o.x;
            s->y = o.y;
            s->scaleX = o.scaleX;
            s->scaleY = o.scaleY;
            if (t == "square") s->setGraphicSize(o.scaleX, o.scaleY), s->scaleX = o.scaleX, s->scaleY = o.scaleY;
            s->updateHitbox();
            s->scrollX = o.scrollX;
            s->scrollY = o.scrollY;
            s->alpha = o.alpha;
            s->angle = o.angle;
            s->color = o.color;
            s->flipX = o.flipX;
            s->flipY = o.flipY;
            s->antialiasing = o.antialiasing;
            s->inScene = true;
            if (!o.name.empty()) sc.objects[o.name] = s;
            sc.order.push_back(s);
            objSprites_.back() = s;
        }
        if (!placed) {
            sc.order.push_back(sc.gfMarker);
            if (chars_[1]) sc.order.push_back(chars_[1]);
            if (chars_[2]) sc.order.push_back(chars_[2]);
            if (chars_[0]) sc.order.push_back(chars_[0]);
        }
        // The stage's script builds its sprites (Psych 0.6 / 0.7 stages are Lua).
        const std::string lua = sc.files.find("stages/" + name + ".lua");
        game_.shaders = app().settings.shaders;
        game_.clockMs = 0;
        if (!lua.empty()) {
            host_ = std::make_unique<ScriptHost>(sc, game_);
            host_->load(lua);
            host_->call("onCreatePost");
        }
        rend_.preload(sc);
    }

    void makeChar(int i) {
        chars_[i].reset();
        scene::Scene& sc = *sc_;
        // Scripts reach them as boyfriend, gf and dad; the scene animates them.
        (i == 0 ? sc.bf : i == 1 ? sc.gf : sc.dad) = nullptr;
        if (i == 1 && def_.hideGf) return;
        psych::CharacterDef cd;
        std::string path, text, err;
        if (loadCharacterDef(sc, who_[i], cd, path, text, err)) chars_[i] = buildCharacter(sc, cd, i == 0, err);
        if (!chars_[i]) return;
        (i == 0 ? sc.bf : i == 1 ? sc.gf : sc.dad) = chars_[i];
        chars_[i]->scrollX = chars_[i]->scrollY = i == 1 ? 0.95f : 1.0f;
        place(i);
    }

    void place(int i) {
        if (!chars_[i]) return;
        const float* p = pos(i);
        chars_[i]->x = p[0] + chars_[i]->posX;
        chars_[i]->y = p[1] + chars_[i]->posY;
    }

    // Where the camera looks at character i, as Psych's moveCamera works it out.
    Vector2 cameraFocus(int i) {
        const scene::CharacterPtr& c = chars_[i];
        const float* off = camOff(i);
        if (!c) return {pos(i)[0] + off[0], pos(i)[1] + off[1]};
        const float mx = c->x + c->width * 0.5f, my = c->y + c->height * 0.5f;
        if (i == 0) return {mx - 100 - c->camX + off[0], my - 100 + c->camY + off[1]};
        if (i == 2) return {mx + 150 + c->camX + off[0], my - 100 + c->camY + off[1]};
        return {mx + c->camX + off[0], my + c->camY + off[1]};
    }

    void drawCameraFrame() {
        const Vector2 f = cameraFocus(camWho_);
        const float hw = gfx::W * 0.5f / float(std::max(0.05, def_.zoom)), hh = gfx::H * 0.5f / float(std::max(0.05, def_.zoom));
        const Vector2 a = view_.toCanvas(f.x - hw, f.y - hh), b = view_.toCanvas(f.x + hw, f.y + hh);
        DrawRectangleLinesEx({a.x, a.y, b.x - a.x, b.y - a.y}, 2, kGold);
        cross(view_.toCanvas(f.x, f.y), 14, kGold);
        gfx::text(FontId::Ui, std::string("Camera on ") + kWho[camWho_], a.x + 6, a.y + 4, 16, kGold);
    }

    void dragCharacters(const Input& in) {
        if (!sc_) return;
        const Vector2 w = view_.toWorld(ui_.mouse);
        if (in.mousePressed(MOUSE_BUTTON_LEFT) && !overPanel(ui_) && ui_.focus.empty()) {
            for (int i : {0, 2, 1}) {  // the front one first
                const scene::CharacterPtr& c = chars_[i];
                if (c && w.x >= c->x && w.x <= c->x + c->width && w.y >= c->y && w.y <= c->y + c->height) {
                    drag_ = i;
                    dragLast_ = w;
                    tab_ = i + 1;
                    camWho_ = i;
                    break;
                }
            }
        }
        if (drag_ >= 0) {
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                drag_ = -1;
                return;
            }
            float* p = pos(drag_);
            const float dx = std::round(w.x - dragLast_.x), dy = std::round(w.y - dragLast_.y);
            if (dx != 0 || dy != 0) {
                p[0] += dx;
                p[1] += dy;
                dragLast_.x += dx;
                dragLast_.y += dy;
                place(drag_);
                dirty_ = true;
                leaveArmed_ = false;
            }
        }
    }

    void resetView() {
        const Vector2 a = cameraFocus(2), b = cameraFocus(0);
        view_.x = (a.x + b.x) * 0.5f;
        view_.y = (a.y + b.y) * 0.5f;
        view_.zoom = float(std::max(0.05, def_.zoom)) * 0.55f;
        view_.x -= (kPanelX - gfx::W) * 0.5f / view_.zoom;
    }

    void drawPanel() {
        panelBox(8, gfx::H - 40);
        float y = 16;
        const float x = kPanelX, w = kPanelW, rh = 30;
        static const char* tabs[5] = {"Stage", "BF", "GF", "Dad", "Objects"};
        for (int i = 0; i < 5; ++i)
            if (ui_.button({x + i * (w / 5), y, w / 5 - 4, rh}, tabs[i], tab_ == i)) tab_ = i;
        y += rh + 12;
        if (tab_ == 0) drawStageTab(x, y, w, rh);
        else if (tab_ == 4) drawObjectsTab(x, y, w, rh);
        else drawCharTab(tab_ - 1, x, y, w, rh);
        ui_.overlays();
    }

    void drawStageTab(float x, float y, float w, float rh) {
        if (!names_.empty()) {
            if (ui_.dropdown("stage", {x, y, w, rh}, "Stage", names_, nameIdx_)) load(names_[size_t(nameIdx_)]);
            y += rh + 6;
        }
        ui_.text("save", {x, y, w, rh}, "Save as", saveName_);
        y += rh + 6;
        if (ui_.stepper({x, y, w, rh}, "Zoom", def_.zoom, 0.05, 0.05, 10, "%.2f")) dirty_ = true;
        y += rh + 6;
        if (ui_.stepper({x, y, w, rh}, "Camera speed", def_.camSpeed, 0.1, 0, 20, "%.1f")) dirty_ = true;
        y += rh + 6;
        if (ui_.check({x, y, w, rh}, "Pixel stage", def_.pixel)) dirty_ = true;
        y += rh + 6;
        if (ui_.check({x, y, w, rh}, "Hide girlfriend", def_.hideGf)) {
            dirty_ = true;
            buildScene(saveName_);
        }
        y += rh + 6;
        ui_.check({x, y, w * 0.5f, rh}, "Camera frame", showCam_);
        std::vector<std::string> who = {"BF", "GF", "Dad"};
        ui_.choice({x + w * 0.5f, y, w * 0.5f, rh}, "", who, camWho_, 0);
        y += rh + 12;
        gfx::text(FontId::Ui, def_.objects.empty() ? "Sprites: from the stage's Lua script" : TextFormat("%d objects in the JSON", int(def_.objects.size())),
                  x, y, 16, kDim);
        y += 26;
        if (ui_.button({x, y, w * 0.5f - 4, rh + 4}, "Save")) save();
        if (ui_.button({x + w * 0.5f + 4, y, w * 0.5f - 4, rh + 4}, "Reload")) load(saveName_);
    }

    // Issue #22: the stage's own objects (Psych 0.7.3 "objects"): pick one
    // (here or by clicking it), drag it, change what it is and how it shows,
    // its layer, add sprites and remove them. Saved with the stage's JSON.
    void drawObjectsTab(float x, float y, float w, float rh) {
        std::vector<std::string> names;
        for (size_t i = 0; i < def_.objects.size(); ++i) names.push_back(objLabel(i));
        if (!names.empty()) {
            objSel_ = std::clamp(objSel_, 0, int(names.size()) - 1);
            ui_.dropdown("obj", {x, y, w, rh}, "Object", names, objSel_);
            y += rh + 8;
        } else {
            gfx::text(FontId::Ui, "No objects in the stage's JSON yet.", x, y, 16, kDim);
            y += 26;
        }
        auto changed = [&] {
            dirty_ = true;
            leaveArmed_ = false;
            applyObject(size_t(objSel_));
        };
        if (!names.empty()) {
            psych::StageObject& o = def_.objects[size_t(objSel_)];
            const std::string t = toLower(o.type);
            if (t == "gf" || t == "gfgroup" || t == "dad" || t == "dadgroup" || t == "boyfriend" || t == "boyfriendgroup") {
                gfx::text(FontId::Ui, "Where this character is drawn (its layer).", x, y, 16, kDim);
                y += 26;
            } else {
                ui_.text("objname", {x, y, w, rh}, "Name", o.name);
                y += rh + 6;
                std::string img = o.image;
                if (ui_.text("objimg", {x, y, w - 92, rh}, "Image", img)) o.image = img;
                if (ui_.button({x + w - 86, y, 86, rh}, "Load")) {
                    dirty_ = true;
                    buildScene(saveName_);
                }
                y += rh + 6;
                auto step = [&](const char* label, float& f, double inc, double lo, double hi, const char* fmt) {
                    double d = f;
                    if (ui_.stepper({x, y, w, rh}, label, d, inc, lo, hi, fmt)) {
                        f = float(d);
                        changed();
                    }
                    y += rh + 6;
                };
                step("X", o.x, 10, -100000, 100000, "%.0f");
                step("Y", o.y, 10, -100000, 100000, "%.0f");
                step("Scale X", o.scaleX, 0.05, -50, 50, "%.2f");
                step("Scale Y", o.scaleY, 0.05, -50, 50, "%.2f");
                step("Scroll X", o.scrollX, 0.05, -10, 10, "%.2f");
                step("Scroll Y", o.scrollY, 0.05, -10, 10, "%.2f");
                step("Alpha", o.alpha, 0.05, 0, 1, "%.2f");
                step("Angle", o.angle, 1, -3600, 3600, "%.0f");
                if (ui_.check({x, y, w / 3, rh}, "Flip X", o.flipX)) changed();
                if (ui_.check({x + w / 3, y, w / 3, rh}, "Flip Y", o.flipY)) changed();
                if (ui_.check({x + 2 * w / 3, y, w / 3, rh}, "Smooth", o.antialiasing)) changed();
                y += rh + 8;
            }
            const float bw = (w - 12) / 4;
            if (ui_.button({x, y, bw, rh}, "Back") && objSel_ > 0) moveObject(objSel_, objSel_ - 1);
            if (ui_.button({x + bw + 4, y, bw, rh}, "Front") && objSel_ + 1 < int(def_.objects.size())) moveObject(objSel_, objSel_ + 1);
            if (ui_.button({x + 2 * (bw + 4), y, bw, rh}, "Copy")) {
                psych::StageObject c = def_.objects[size_t(objSel_)];
                c.name += "_copy";
                c.x += 40;
                c.y += 40;
                def_.objects.insert(def_.objects.begin() + objSel_ + 1, c);
                ++objSel_;
                dirty_ = true;
                buildScene(saveName_);
            }
            if (ui_.button({x + 3 * (bw + 4), y, bw, rh}, "Delete")) {
                def_.objects.erase(def_.objects.begin() + objSel_);
                objSel_ = std::max(0, objSel_ - 1);
                dirty_ = true;
                buildScene(saveName_);
            }
            y += rh + 8;
        }
        if (ui_.button({x, y, w, rh}, "Add a sprite (an image of the mod)")) {
            psych::StageObject o;
            o.type = "sprite";
            o.name = "sprite" + std::to_string(def_.objects.size() + 1);
            o.image = def_.objects.empty() ? "stageback" : def_.objects[size_t(objSel_)].image;
            if (sc_) {  // where the view is looking
                const Vector2 c = view_.toWorld({gfx::W * 0.4f, gfx::H * 0.5f});
                o.x = std::round(c.x);
                o.y = std::round(c.y);
            }
            if (def_.objects.empty()) {  // the characters keep their places among the objects
                for (const char* who : {"gf", "dad", "boyfriend"}) {
                    psych::StageObject m;
                    m.type = who;
                    def_.objects.push_back(m);
                }
                def_.objects.insert(def_.objects.begin(), o);
                objSel_ = 0;
            } else {
                def_.objects.insert(def_.objects.begin() + objSel_ + 1, o);
                ++objSel_;
            }
            dirty_ = true;
            buildScene(saveName_);
        }
        y += rh + 6;
        gfx::text(FontId::Ui, "Click an object in the view to pick it; drag to move it.", x, y, 15, kDim);
    }

    std::string objLabel(size_t i) const {
        const psych::StageObject& o = def_.objects[i];
        const std::string t = toLower(o.type);
        if (t == "gf" || t == "gfgroup") return std::to_string(i) + ": (girlfriend)";
        if (t == "dad" || t == "dadgroup") return std::to_string(i) + ": (opponent)";
        if (t == "boyfriend" || t == "boyfriendgroup") return std::to_string(i) + ": (boyfriend)";
        return std::to_string(i) + ": " + (o.name.empty() ? o.image : o.name);
    }

    // The sprite on screen follows the object's numbers.
    void applyObject(size_t i) {
        if (i >= objSprites_.size() || !objSprites_[i]) return;
        const psych::StageObject& o = def_.objects[i];
        scene::Sprite& s = *objSprites_[i];
        s.x = o.x;
        s.y = o.y;
        s.scaleX = o.scaleX;
        s.scaleY = o.scaleY;
        if (toLower(o.type) == "square") s.setGraphicSize(o.scaleX, o.scaleY);
        s.updateHitbox();
        s.scrollX = o.scrollX;
        s.scrollY = o.scrollY;
        s.alpha = o.alpha;
        s.angle = o.angle;
        s.flipX = o.flipX;
        s.flipY = o.flipY;
        s.antialiasing = o.antialiasing;
    }
    void moveObject(int from, int to) {
        std::swap(def_.objects[size_t(from)], def_.objects[size_t(to)]);
        objSel_ = to;
        dirty_ = true;
        buildScene(saveName_);
    }

    // Objects: a click picks the front one under the mouse; a drag moves it.
    void dragObjects(const Input& in) {
        if (!sc_ || tab_ != 4) return;
        const Vector2 w = view_.toWorld(ui_.mouse);
        if (in.mousePressed(MOUSE_BUTTON_LEFT) && !overPanel(ui_) && ui_.focus.empty()) {
            for (int i = int(objSprites_.size()) - 1; i >= 0; --i) {
                const scene::SpritePtr& s = objSprites_[size_t(i)];
                if (!s) continue;
                const float sx = s->x - s->offsetX, sy = s->y - s->offsetY;
                if (w.x >= sx && w.x <= sx + s->width && w.y >= sy && w.y <= sy + s->height) {
                    objSel_ = i;
                    objDrag_ = true;
                    dragLast_ = w;
                    break;
                }
            }
        }
        if (!objDrag_) return;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            objDrag_ = false;
            return;
        }
        const float dx = std::round(w.x - dragLast_.x), dy = std::round(w.y - dragLast_.y);
        if ((dx != 0 || dy != 0) && objSel_ >= 0 && objSel_ < int(def_.objects.size())) {
            def_.objects[size_t(objSel_)].x += dx;
            def_.objects[size_t(objSel_)].y += dy;
            dragLast_.x += dx;
            dragLast_.y += dy;
            applyObject(size_t(objSel_));
            dirty_ = true;
            leaveArmed_ = false;
        }
    }

    // The objects as Psych 0.7.3 writes them.
    std::string objectsJson() const {
        std::string o = "[";
        for (size_t i = 0; i < def_.objects.size(); ++i) {
            const psych::StageObject& b = def_.objects[i];
            o += i ? ",\n\t\t{" : "\n\t\t{";
            o += "\"type\": " + jsonStr(b.type.empty() ? std::string("sprite") : b.type);
            const std::string t = toLower(b.type);
            if (t != "gf" && t != "gfgroup" && t != "dad" && t != "dadgroup" && t != "boyfriend" && t != "boyfriendgroup") {
                o += ", \"name\": " + jsonStr(b.name) + ", \"image\": " + jsonStr(b.image) + ", \"x\": " + jnum(b.x) +
                     ", \"y\": " + jnum(b.y) + ", \"scale\": " + pair(b.scaleX, b.scaleY) + ", \"scroll\": " + pair(b.scrollX, b.scrollY) +
                     ", \"alpha\": " + jnum(b.alpha) + ", \"angle\": " + jnum(b.angle) + ", \"color\": " +
                     jsonStr(TextFormat("#%06X", unsigned(b.color & 0xFFFFFF))) + ", \"flipX\": " + (b.flipX ? "true" : "false") +
                     ", \"flipY\": " + (b.flipY ? "true" : "false") + ", \"antialiasing\": " + (b.antialiasing ? "true" : "false");
                if (!b.anims.empty()) {
                    o += ", \"animations\": [";
                    for (size_t k = 0; k < b.anims.size(); ++k) {
                        const psych::AnimDef& a = b.anims[k];
                        std::string ind;
                        for (size_t q = 0; q < a.indices.size(); ++q) ind += (q ? ", " : "") + std::to_string(a.indices[q]);
                        o += std::string(k ? ", " : "") + "{\"anim\": " + jsonStr(a.anim) + ", \"name\": " + jsonStr(a.prefix) +
                             ", \"fps\": " + jnum(a.fps) + ", \"loop\": " + (a.loop ? "true" : "false") + ", \"indices\": [" + ind +
                             "], \"offsets\": " + pair(a.offX, a.offY) + "}";
                    }
                    o += "]";
                }
                if (!b.firstAnim.empty()) o += ", \"firstAnimation\": " + jsonStr(b.firstAnim);
            }
            o += "}";
        }
        return o + (def_.objects.empty() ? "]" : "\n\t]");
    }

    void drawCharTab(int i, float x, float y, float w, float rh) {
        gfx::text(FontId::Ui, kWho[i], x, y, 22, kGold);
        y += 34;
        ui_.text("who", {x, y, w - 92, rh}, "Character", who_[i]);
        if (ui_.button({x + w - 86, y, 86, rh}, "Show")) {
            who_[i] = trim(who_[i]);
            buildScene(saveName_);
        }
        y += rh + 6;
        gfx::text(FontId::Ui, "Only shown here: songs choose their characters.", x, y, 15, kDim);
        y += 26;
        float* p = pos(i);
        float* c = camOff(i);
        auto step = [&](const char* label, float& f, bool moves) {
            double d = f;
            if (ui_.stepper({x, y, w, rh}, label, d, 1, -20000, 20000, "%.0f")) {
                f = float(d);
                dirty_ = true;
                if (moves) place(i);
            }
            y += rh + 6;
        };
        step("Position X", p[0], true);
        step("Position Y", p[1], true);
        step("Camera X", c[0], false);
        step("Camera Y", c[1], false);
        camWho_ = i;
    }

    void save() {
        const std::string name = fileName(saveName_);
        if (name.empty()) {
            status_.set("Type a name to save as", true);
            return;
        }
        const std::vector<std::pair<std::string, std::string>> fields = {
            {"directory", jsonStr(def_.directory)},
            {"defaultZoom", jnum(def_.zoom)},
            {"isPixelStage", def_.pixel ? "true" : "false"},
            {"boyfriend", pair(def_.bf[0], def_.bf[1])},
            {"girlfriend", pair(def_.gf[0], def_.gf[1])},
            {"opponent", pair(def_.dad[0], def_.dad[1])},
            {"hide_girlfriend", def_.hideGf ? "true" : "false"},
            {"camera_boyfriend", pair(def_.camBf[0], def_.camBf[1])},
            {"camera_opponent", pair(def_.camDad[0], def_.camDad[1])},
            {"camera_girlfriend", pair(def_.camGf[0], def_.camGf[1])},
            {"camera_speed", jnum(def_.camSpeed)},
        };
        std::vector<std::pair<std::string, std::string>> all = fields;
        if (!def_.objects.empty()) all.push_back({"objects", objectsJson()});
        const std::string path = mod_.roots[0] + "stages/" + name + ".json";
        std::string err;
        if (!saveText(path, writeObject(all, loadedText_), err)) {
            status_.set(err, true);
            return;
        }
        scene::readFile(path, loadedText_);
        dirty_ = leaveArmed_ = false;
        if (std::find(names_.begin(), names_.end(), name) == names_.end()) names_.push_back(name);
        status_.set("Saved " + path);
    }
};

// =========================================================== event editor ==

struct EventTemplate {
    const char* name;
    const char* v1;
    const char* v2;
    const char* body;
};

const EventTemplate kTemplates[] = {
    {"Empty", "", "", "\t-- what the event does\n"},
    {"Camera flash", "Duration (s)", "Colour (FFFFFF)",
     "\tlocal colour = value2 ~= '' and value2 or 'FFFFFF'\n\tcameraFlash('game', colour, tonumber(value1) or 0.5, true)\n"},
    {"Fade the HUD", "Alpha (0 to 1)", "Duration (s)",
     "\tdoTweenAlpha('eventHudFade', 'camHUD', tonumber(value1) or 0, tonumber(value2) or 0.5, 'linear')\n"},
    {"Camera shake", "Intensity (0.01)", "Duration (s)",
     "\tcameraShake('game', tonumber(value1) or 0.01, tonumber(value2) or 0.3)\n\tcameraShake('hud', (tonumber(value1) or 0.01) / 2, tonumber(value2) or 0.3)\n"},
    {"Zoom the camera", "Zoom (0.9)", "Duration (s)",
     "\tdoTweenZoom('eventZoom', 'camGame', tonumber(value1) or 0.9, tonumber(value2) or 0.5, 'quadOut')\n\tsetProperty('defaultCamZoom', tonumber(value1) or 0.9)\n"},
    {"Play a sound", "Sound (sounds/...)", "Volume (0 to 1)", "\tplaySound(value1, tonumber(value2) or 1)\n"},
    {"Scroll speed", "Multiplier", "Duration (s)", "\ttriggerEvent('Change Scroll Speed', value1, value2)\n"},
};

class EventEditorState : public State {
public:
    void enter() override {
        mod_ = currentMod();
        useMod(sc_, mod_);
        refresh();
    }
    bool keyReserved(int) const override { return !ui_.focus.empty(); }

    void update(double) override {
        App& a = app();
        feedUi(ui_);
        if (!ui_.focus.empty()) return;
        if (a.input.pressed(KEY_ESCAPE)) a.change(makeEditorMenu());
    }

    void draw() override {
        App& a = app();
        gfx::beginCanvas();
        drawMenuBackground(a.now, 0x6C8CFF, 0);
        DrawRectangleRec({0, 0, gfx::W, gfx::H}, fade(BLACK, 0.45f));
        gfx::text(FontId::Title, "EVENT EDITOR", 40, 18, 40, kGold, Align::Left, 2.0f);
        gfx::text(FontId::Ui, "Custom events of " + modLabel(mod_) + ": custom_events/<name>.txt (what the chart editor shows) and .lua",
                  40, 70, 18, kIvory);
        drawList();
        drawForm();
        ui_.overlays();
        hints("Click an event to see it   ESC back");
        status_.draw();
        gfx::endCanvas();
    }

private:
    scene::Scene sc_;
    Ui ui_;
    Status status_;
    ModChoice mod_;
    std::vector<std::string> names_;
    int sel_ = -1, template_ = 0;
    float scroll_ = 0;
    std::string name_, desc_, v1_, v2_, script_;

    void refresh() {
        std::set<std::string> all;
        for (const char* ext : {".txt", ".lua"})
            for (const std::string& n : listNames(sc_, "custom_events/", ext)) all.insert(n);
        names_.assign(all.begin(), all.end());
    }

    std::string mine(const std::string& ext) const { return mod_.roots[0] + "custom_events/" + fileName(name_) + ext; }

    void open(int i) {
        sel_ = i;
        name_ = names_[size_t(i)];
        desc_.clear();
        script_.clear();
        std::string text;
        if (const std::string p = sc_.files.find("custom_events/" + name_ + ".txt"); !p.empty() && scene::readFile(p, text)) desc_ = trim(text);
        if (const std::string p = sc_.files.find("custom_events/" + name_ + ".lua"); !p.empty()) scene::readFile(p, script_);
    }

    void drawList() {
        const Rectangle box{40, 104, 400, gfx::H - 160};
        DrawRectangleRounded(box, 0.02f, 6, kPanel);
        const float row = 26;
        const int rows = int((box.height - 8) / row), n = int(names_.size());
        if (CheckCollisionPointRec(ui_.mouse, box) && ui_.wheel != 0) scroll_ = clampv(scroll_ - ui_.wheel * 3, 0.0f, float(std::max(0, n - rows)));
        if (n == 0) gfx::text(FontId::Ui, "No custom events yet", box.x + 12, box.y + 10, 18, kDim);
        const int first = int(scroll_);
        for (int k = 0; k < rows && first + k < n; ++k) {
            const int i = first + k;
            const Rectangle r{box.x + 4, box.y + 4 + k * row, box.width - 8, row - 2};
            const bool hov = ui_.hover(r);
            if (i == sel_ || hov) DrawRectangleRec(r, i == sel_ ? Color{90, 66, 140, 255} : Color{50, 38, 76, 255});
            gfx::text(FontId::Ui, names_[size_t(i)], r.x + 8, r.y + 3, 18, kInk);
            if (hov && ui_.click) {
                ui_.used = true;
                open(i);
            }
        }
    }

    void drawForm() {
        const float x = 470, w = gfx::W - x - 40, rh = 30;
        float y = 104;
        DrawRectangleRounded({x - 10, y - 8, w + 20, gfx::H - 152}, 0.02f, 6, kPanel);
        ui_.text("name", {x, y, w, rh}, "Name", name_);
        y += rh + 8;
        ui_.text("desc", {x, y, w, rh}, "Description", desc_);
        y += rh + 8;
        ui_.text("v1", {x, y, w, rh}, "Value 1", v1_);
        y += rh + 8;
        ui_.text("v2", {x, y, w, rh}, "Value 2", v2_);
        y += rh + 8;
        std::vector<std::string> tnames;
        for (const EventTemplate& t : kTemplates) tnames.push_back(t.name);
        if (ui_.dropdown("tmpl", {x, y, w, rh}, "Does", tnames, template_)) {
            const EventTemplate& t = kTemplates[template_];
            if (v1_.empty() || isTemplateHint(v1_)) v1_ = t.v1;
            if (v2_.empty() || isTemplateHint(v2_)) v2_ = t.v2;
        }
        y += rh + 12;
        if (ui_.button({x, y, 220, rh + 4}, "Create / save")) create();
        if (ui_.button({x + 230, y, 160, rh + 4}, "Open script")) openFile(".lua");
        if (ui_.button({x + 400, y, 160, rh + 4}, "Open folder")) {
            std::error_code ec;
            fs::create_directories(toPath(mod_.roots[0] + "custom_events"), ec);
            if (!openWithSystem(mod_.roots[0] + "custom_events")) status_.set("This system cannot open folders from the game", true);
        }
        y += rh + 18;
        gfx::text(FontId::Ui, "Script:", x, y, 18, kDim);
        y += 24;
        // The script as it is (the first lines).
        int line = 0;
        size_t p = 0;
        const std::string shown = script_.empty() ? scriptFor() : script_;
        while (p < shown.size() && line < 14) {
            size_t e = shown.find('\n', p);
            if (e == std::string::npos) e = shown.size();
            std::string l = shown.substr(p, e - p);
            for (size_t t; (t = l.find('\t')) != std::string::npos;) l.replace(t, 1, "    ");
            gfx::text(FontId::Ui, l, x, y, 16, script_.empty() ? kDim : kInk);
            y += 20;
            ++line;
            p = e + 1;
        }
    }

    static bool isTemplateHint(const std::string& s) {
        for (const EventTemplate& t : kTemplates)
            if (s == t.v1 || s == t.v2) return true;
        return false;
    }

    std::string scriptFor() const {
        const std::string n = trim(name_).empty() ? std::string("My Event") : trim(name_);
        std::string s = "-- " + n + (desc_.empty() ? "" : ": " + desc_) + "\n";
        if (!v1_.empty()) s += "-- Value 1: " + v1_ + "\n";
        if (!v2_.empty()) s += "-- Value 2: " + v2_ + "\n";
        std::string quoted;
        for (char c : n) quoted += c == '\'' ? std::string("\\'") : std::string(1, c);
        s += "function onEvent(name, value1, value2)\n\tif name ~= '" + quoted + "' then return end\n";
        s += kTemplates[template_].body;
        s += "end\n";
        return s;
    }

    void create() {
        const std::string n = fileName(name_);
        if (n.empty()) {
            status_.set("Type the event's name", true);
            return;
        }
        // The description file holds what the chart editor shows for it.
        std::string desc = desc_;
        if (!v1_.empty()) desc += "\nValue 1: " + v1_;
        if (!v2_.empty()) desc += "\nValue 2: " + v2_;
        std::string err;
        if (!saveText(mine(".txt"), desc + "\n", err)) {
            status_.set(err, true);
            return;
        }
        std::error_code ec;
        const bool hasScript = fs::exists(toPath(mine(".lua")), ec);
        if (!hasScript && !saveText(mine(".lua"), scriptFor(), err)) {
            status_.set(err, true);
            return;
        }
        refresh();
        auto it = std::find(names_.begin(), names_.end(), n);
        if (it != names_.end()) open(int(it - names_.begin()));
        status_.set(hasScript ? "Saved the description (the script was there: it is kept)" : "Made " + mine(".txt") + " and its .lua");
    }

    void openFile(const std::string& ext) {
        std::string p = sc_.files.find("custom_events/" + trim(name_) + ext);
        if (p.empty()) {
            status_.set("Create the event first", true);
            return;
        }
        if (!openWithSystem(p)) status_.set("This system cannot open files from the game: " + p, true);
    }
};

}  // namespace

std::unique_ptr<State> makeScriptEditor() { return std::make_unique<ScriptEditorState>(); }
std::unique_ptr<State> makeChartConverter() { return std::make_unique<ChartConverterState>(); }
std::unique_ptr<State> makeEditorMenu() { return std::make_unique<EditorMenuState>(); }
std::unique_ptr<State> makeCharacterEditor() { return std::make_unique<CharacterEditorState>(); }
std::unique_ptr<State> makeStageEditor() { return std::make_unique<StageEditorState>(); }
std::unique_ptr<State> makeEventEditor() { return std::make_unique<EventEditorState>(); }

}  // namespace gh
