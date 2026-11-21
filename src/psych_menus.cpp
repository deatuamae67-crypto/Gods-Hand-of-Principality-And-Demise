// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "psych_menus.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "app.hpp"
#include "gfx.hpp"
#include "psych.hpp"
#include "psych_ui.hpp"
#include "scene.hpp"

namespace gh::pm {

bool on() {
    const Settings& s = app().settings;
    return s.psychUi && s.psychMenus;
}

// ------------------------------------------------------------------ Sheet --

bool Sheet::load(const std::string& png) {
    unload();
    if (png.empty()) return false;
    std::string xml;
    psych::Atlas atlas;
    if (!scene::readFile(png.substr(0, png.size() - 4) + ".xml", xml) || !atlas.parseSparrow(xml)) return false;
    tex_ = LoadTexture(png.c_str());
    if (!tex_.id) return false;
    SetTextureFilter(tex_, TEXTURE_FILTER_BILINEAR);
    for (const psych::AtlasFrame& a : atlas.frames) {
        Frame f;
        f.name = a.name;
        // A frame past the picture's edge (an XML made for a bigger sheet)
        // is cut there, as Flixel cuts it, not wrapped around.
        const float w = std::clamp(a.w, 0.0f, std::max(0.0f, float(tex_.width) - a.x));
        const float h = std::clamp(a.h, 0.0f, std::max(0.0f, float(tex_.height) - a.y));
        f.src = {a.x, a.y, w, h};
        f.fx = a.fx;
        f.fy = a.fy;
        f.fw = a.fw > 0 ? a.fw : (a.rotated ? a.h : a.w);
        f.fh = a.fh > 0 ? a.fh : (a.rotated ? a.w : a.h);
        f.rotated = a.rotated;
        frames_.push_back(f);
    }
    return true;
}

void Sheet::unload() {
    if (tex_.id && IsWindowReady()) UnloadTexture(tex_);
    tex_ = Texture2D{};
    frames_.clear();
}

std::vector<int> Sheet::anim(const std::string& prefix) const {
    std::vector<std::pair<long, int>> found;
    for (size_t i = 0; i < frames_.size(); ++i) {
        const std::string& n = frames_[i].name;
        if (n.size() < prefix.size() || n.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string rest = n.substr(prefix.size());
        // Flixel's addByPrefix: the frame number follows (" instance 1" too).
        std::string digits;
        for (char c : rest)
            if (std::isdigit((unsigned char)c)) digits += c;
        const bool ok = std::all_of(rest.begin(), rest.end(), [](char c) { return std::isdigit((unsigned char)c) || c == ' '; }) ||
                        rest.rfind(" instance", 0) == 0 || rest.empty();
        if (ok) found.push_back({digits.empty() ? 0L : std::atol(digits.c_str()), int(i)});
    }
    std::stable_sort(found.begin(), found.end());
    std::vector<int> out;
    for (const auto& [num, i] : found) out.push_back(i);
    return out;
}

int Sheet::frameAt(const std::vector<int>& anim, double t, bool loop, double fps) const {
    if (anim.empty()) return -1;
    const int n = int(anim.size());
    int k = int(std::max(0.0, t) * fps);
    k = loop ? k % n : std::min(k, n - 1);
    return anim[size_t(k)];
}

Vector2 Sheet::size(int f) const {
    if (f < 0 || f >= int(frames_.size())) return {0, 0};
    return {frames_[size_t(f)].fw, frames_[size_t(f)].fh};
}

void Sheet::draw(int f, Vector2 pos, float scale, Color tint) const {
    if (!tex_.id || f < 0 || f >= int(frames_.size())) return;
    const Frame& fr = frames_[size_t(f)];
    // The stored part sits inside the untrimmed box at (-fx, -fy).
    const float x = pos.x - fr.fx * scale, y = pos.y - fr.fy * scale;
    if (fr.rotated) {  // stored turned 90 degrees clockwise
        const float w = fr.src.height * scale, h = fr.src.width * scale;
        DrawTexturePro(tex_, fr.src, {x + w * 0.5f, y + h * 0.5f, fr.src.width * scale, fr.src.height * scale},
                       {fr.src.width * scale * 0.5f, fr.src.height * scale * 0.5f}, -90.0f, tint);
    } else {
        DrawTexturePro(tex_, fr.src, {x, y, fr.src.width * scale, fr.src.height * scale}, {0, 0}, 0, tint);
    }
}

// ----------------------------------------------------------------- assets --

namespace {

Texture2D loadPicture(const std::string& path) {
    if (path.empty()) return Texture2D{};
    Texture2D t = LoadTexture(path.c_str());
    if (t.id) SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

void drop(Texture2D& t) {
    if (t.id && IsWindowReady()) UnloadTexture(t);
    t = Texture2D{};
}

}  // namespace

Assets& assets() {
    static Assets a;
    const PsychUi& ui = psychUi();
    const std::vector<std::string> roots = ui.active() ? ui.roots() : std::vector<std::string>{};
    if (roots == a.roots) return a;
    // The copy changed (or was found): its pictures again.
    for (Texture2D* t : {&a.menuBG, &a.menuDesat, &a.menuMagenta, &a.tracks}) drop(*t);
    for (auto& [n, t] : a.difficulties) drop(t);
    a.difficulties.clear();
    for (auto& [n, s] : a.items) s.unload();
    a.items.clear();
    for (Sheet* s : {&a.arrows, &a.checkbox, &a.logo, &a.gfTitle, &a.enter}) s->unload();
    a.roots = roots;
    if (roots.empty()) return a;
    scene::Files f;
    f.setRoots(roots);
    a.menuBG = loadPicture(f.find("images/menuBG.png"));
    a.menuDesat = loadPicture(f.find("images/menuDesat.png"));
    a.menuMagenta = loadPicture(f.find("images/menuBGMagenta.png"));
    a.tracks = loadPicture(f.find("images/Menu_Tracks.png"));
    if (!a.tracks.id) a.tracks = loadPicture(f.find("images/storymenu/Menu_Tracks.png"));
    for (const char* n : {"story_mode", "freeplay", "mods", "credits", "options", "awards", "achievements", "donate"}) {
        Sheet s;
        if (s.load(f.find(std::string("images/mainmenu/menu_") + n + ".png"))) a.items[n] = std::move(s);
    }
    a.arrows.load(f.find("images/campaign_menu_UI_assets.png"));
    a.checkbox.load(f.find("images/checkboxanim.png"));
    a.logo.load(f.find("images/logoBumpin.png"));
    a.gfTitle.load(f.find("images/gfDanceTitle.png"));
    a.enter.load(f.find("images/titleEnter.png"));
    for (const char* d : {"easy", "normal", "hard"}) {
        const Texture2D t = loadPicture(f.find(std::string("images/menudifficulties/") + d + ".png"));
        if (t.id) a.difficulties[d] = t;
    }
    return a;
}

// ------------------------------------------------------------ backgrounds --

void background(Bg which, Color tint, float scrollY, float scale, float alpha) {
    Assets& a = assets();
    const Texture2D& t = which == kMenuBG ? a.menuBG : which == kDesat ? a.menuDesat : a.menuMagenta;
    if (t.id) {
        // Psych: setGraphicSize(width * scale), centred on the screen.
        const float w = float(t.width) * scale, h = float(t.height) * scale;
        DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, {(gfx::W - w) * 0.5f, (gfx::H - h) * 0.5f - scrollY, w, h},
                       {0, 0}, 0, gfx::fade(tint, alpha));
        return;
    }
    // No picture: its colours, top to bottom, with the sunburst's soft rays.
    Color top{253, 232, 113, 255}, bottom{220, 119, 39, 255};
    if (which == kDesat) top = {225, 225, 225, 255}, bottom = {140, 140, 140, 255};
    if (which == kMagenta) top = {253, 113, 155, 255}, bottom = {220, 39, 167, 255};
    auto mul = [&](Color c) {
        return Color{(unsigned char)(c.r * tint.r / 255), (unsigned char)(c.g * tint.g / 255), (unsigned char)(c.b * tint.b / 255),
                     (unsigned char)(255 * alpha)};
    };
    const float y0 = -160 - scrollY, h = gfx::H + 320;  // room for the camera's scroll
    gfx::gradientRect({-20, y0, gfx::W + 40, h * 0.62f}, mul(top), mul(gfx::mix(top, bottom, 0.25f)));
    gfx::gradientRect({-20, y0 + h * 0.62f, gfx::W + 40, h * 0.38f}, mul(gfx::mix(top, bottom, 0.25f)), mul(bottom));
    const Vector2 c{gfx::W * 0.5f, gfx::H * 0.62f - scrollY};
    const Color ray = gfx::fade(WHITE, 0.07f * alpha);
    for (int i = 0; i < 16; ++i) {
        const float a0 = float(i) * 2.0f * PI / 16.0f, a1 = a0 + PI / 16.0f;
        DrawTriangle(c, {c.x + std::cos(a1) * 1600, c.y + std::sin(a1) * 1600}, {c.x + std::cos(a0) * 1600, c.y + std::sin(a0) * 1600},
                     ray);
    }
}

// ------------------------------------------------------------------ text --

namespace {

// The stand-in for Alphabet: the game's bold font, white with a black edge.
float fallbackLetters(const std::string& s, float x, float y, float height, float alpha, bool draw, bool boldLook) {
    const float size = height * (boldLook ? 1.25f : 1.15f);
    const float w = gfx::measure(gfx::FontId::Title, s, size).x;
    if (draw)
        gfx::text(gfx::FontId::Title, s, x, y - height * 0.12f, size, gfx::fade(boldLook ? WHITE : Color{20, 20, 20, 255}, alpha),
                  gfx::Align::Left, boldLook ? height * 0.09f : 0.0f, gfx::fade(BLACK, alpha));
    return w;
}

}  // namespace

float bold(const std::string& s, float x, float y, float height, float alpha) {
    const float w = psychUi().alphabet(s, x, y, height, alpha);
    return w >= 0 ? w : fallbackLetters(s, x, y, height, alpha, true, true);
}

float plain(const std::string& s, float x, float y, float height, float alpha) {
    const float w = psychUi().alphabetNormal(s, x, y, height, alpha);
    return w >= 0 ? w : fallbackLetters(s, x, y, height, alpha, true, false);
}

float boldWidth(const std::string& s, float height) {
    const float w = psychUi().alphabetWidth(s, height, true);
    return w >= 0 ? w : fallbackLetters(s, 0, 0, height, 1, false, true);
}

float plainWidth(const std::string& s, float height) {
    const float w = psychUi().alphabetWidth(s, height, false);
    return w >= 0 ? w : fallbackLetters(s, 0, 0, height, 1, false, false);
}

void vcr(const std::string& s, float x, float y, float size, Color c, int align, float border) {
    if (psychUi().text(s, x, y, size, c, align, border)) return;
    const gfx::Align al = align < 0 ? gfx::Align::Left : align > 0 ? gfx::Align::Right : gfx::Align::Center;
    gfx::text(gfx::FontId::Ui, s, align < 0 ? x + 2 : align > 0 ? x - 2 : x, y, size * 0.95f, c, al, border > 0 ? border + 0.5f : 0.0f,
              Color{0, 0, 0, c.a});
}

float vcrWidth(const std::string& s, float size) {
    if (psychUi().font()) return psychUi().textWidth(s, size);
    return gfx::measure(gfx::FontId::Ui, s, size * 0.95f).x;
}

// ------------------------------------------------------------- main menu --

namespace {

const Sheet* itemSheet(const std::string& name) {
    Assets& a = assets();
    auto it = a.items.find(name);
    if (it == a.items.end() && name == "achievements") it = a.items.find("awards");
    return it == a.items.end() ? nullptr : &it->second;
}

std::vector<int> itemAnim(const Sheet& s, const std::string& name, bool selected) {
    // Psych 1.0: "<name> idle" / "<name> selected"; 0.7 and the base game:
    // "<name> basic" / "<name> white".
    std::vector<int> f = s.anim(name + (selected ? " selected" : " idle"));
    if (f.empty()) f = s.anim(name + (selected ? " white" : " basic"));
    return f;
}

}  // namespace

Vector2 menuItemSize(const std::string& name, const std::string& label) {
    if (const Sheet* s = itemSheet(name)) {
        const std::vector<int> f = itemAnim(*s, name, false);
        if (!f.empty()) return s->size(f[0]);
    }
    return {gfx::measure(gfx::FontId::Title, label, 92).x + 40, 130};
}

void menuItem(const std::string& name, const std::string& label, Vector2 c, bool selected, float alpha, double t) {
    if (const Sheet* s = itemSheet(name)) {
        const std::vector<int> f = itemAnim(*s, name, selected);
        const int fr = s->frameAt(f, t);
        if (fr >= 0) {
            const Vector2 sz = s->size(fr);
            s->draw(fr, {c.x - sz.x * 0.5f, c.y - sz.y * 0.5f}, 1.0f, gfx::fade(WHITE, alpha));
            return;
        }
    }
    // The base game's menu lettering: orange to yellow with a dark edge, white when chosen.
    const float size = 92;
    const Vector2 m = gfx::measure(gfx::FontId::Title, label, size);
    const float x = c.x - m.x * 0.5f, y = c.y - m.y * 0.5f;
    const float wob = selected ? std::sin(float(t) * 9.0f) * 2.0f : 0.0f;
    gfx::text(gfx::FontId::Title, label, x, y + wob, size, gfx::fade(selected ? WHITE : Color{255, 196, 56, 255}, alpha),
              gfx::Align::Left, 7.0f, gfx::fade(selected ? Color{40, 20, 80, 255} : Color{70, 22, 8, 255}, alpha));
}

// -------------------------------------------------------------- checkboxes --

void checkbox(Vector2 pos, bool on, double since, float alpha) {
    Assets& a = assets();
    if (a.checkbox.ok()) {
        // Psych's CheckboxThingie at 0.9 size: "checkbox anim" (checking) or
        // "checkbox anim reverse" play once, then "checkbox finish" (checked)
        // or "checkbox0" (unchecked) hold, each with its own offset.
        const std::vector<int> anim = a.checkbox.anim(on ? "checkbox anim0" : "checkbox anim reverse");
        const std::vector<int> rest = a.checkbox.anim(on ? "checkbox finish" : "checkbox0");
        const bool moving = !anim.empty() && since < double(anim.size()) / 24.0;
        const int fr = moving ? a.checkbox.frameAt(anim, since, false) : a.checkbox.frameAt(rest, 0, false);
        const Vector2 off = moving ? (on ? Vector2{34, 25} : Vector2{25, 28}) : (on ? Vector2{3, 12} : Vector2{0, 2});
        if (fr >= 0) {
            a.checkbox.draw(fr, {pos.x - off.x, pos.y - off.y}, 0.9f, gfx::fade(WHITE, alpha));
            return;
        }
    }
    const Rectangle r{pos.x + 15, pos.y - 45, 90, 90};  // beside the text, as Psych's picture sits
    DrawRectangleRounded(r, 0.2f, 6, gfx::fade(WHITE, alpha));
    DrawRectangleRoundedLinesEx(r, 0.2f, 6, 6, gfx::fade(BLACK, alpha));
    if (on) {
        DrawLineEx({r.x + 18, r.y + 52}, {r.x + 42, r.y + 76}, 12, gfx::fade(Color{20, 20, 20, 255}, alpha));
        DrawLineEx({r.x + 40, r.y + 76}, {r.x + 82, r.y + 22}, 12, gfx::fade(Color{20, 20, 20, 255}, alpha));
    }
}

// ------------------------------------------------------------- story mode --

Vector2 storyArrowSize() {
    Assets& a = assets();
    if (a.arrows.ok()) {
        const std::vector<int> f = a.arrows.anim("arrow left");
        if (!f.empty()) return a.arrows.size(f[0]);
    }
    return {50, 76};
}

void storyArrow(bool left, bool pushed, Vector2 pos) {
    Assets& a = assets();
    if (a.arrows.ok()) {
        const std::string n = std::string(pushed ? "arrow push " : "arrow ") + (left ? "left" : "right");
        const std::vector<int> f = a.arrows.anim(n);
        if (!f.empty()) {
            a.arrows.draw(f[0], pos, 1.0f, WHITE);
            return;
        }
    }
    const Vector2 s = storyArrowSize();
    const float k = pushed ? 0.85f : 1.0f;
    const Vector2 c{pos.x + s.x * 0.5f, pos.y + s.y * 0.5f};
    const Vector2 tip{c.x + (left ? -1 : 1) * s.x * 0.45f * k, c.y};
    const Vector2 b1{c.x - (left ? -1 : 1) * s.x * 0.4f * k, c.y - s.y * 0.45f * k}, b2{c.x - (left ? -1 : 1) * s.x * 0.4f * k, c.y + s.y * 0.45f * k};
    if (left) DrawTriangle(tip, b2, b1, WHITE);
    else DrawTriangle(tip, b1, b2, WHITE);
}

void difficulty(const std::string& name, Vector2 center, float alpha, Color fallbackColor) {
    Assets& a = assets();
    std::string key = name;
    for (char& ch : key) ch = char(std::tolower((unsigned char)ch));
    auto it = a.difficulties.find(key);
    if (it != a.difficulties.end()) {
        const Texture2D& t = it->second;
        DrawTexture(t, int(center.x - t.width * 0.5f), int(center.y - t.height * 0.5f), gfx::fade(WHITE, alpha));
        return;
    }
    std::string up = name;
    for (char& ch : up) ch = char(std::toupper((unsigned char)ch));
    const float size = 64;
    const Vector2 m = gfx::measure(gfx::FontId::Title, up, size);
    gfx::text(gfx::FontId::Title, up, center.x - m.x * 0.5f, center.y - m.y * 0.5f, size, gfx::fade(fallbackColor, alpha),
              gfx::Align::Left, 5.0f, gfx::fade(BLACK, alpha));
}

}  // namespace gh::pm
