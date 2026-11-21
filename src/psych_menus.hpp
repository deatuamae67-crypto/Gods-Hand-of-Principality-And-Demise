// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The Psych Engine look across the whole engine (issue #36): the menus laid
// out as Psych's (MainMenuState, FreeplayState, StoryMenuState, OptionsState,
// CreditsState, PauseSubState), with the pictures, letters and font of the
// player's own Psych Engine / JS Engine copy (the one the Psych Engine
// interface found). None of them ship with the game: without a copy, or for
// a picture a copy lacks, the game draws a stand-in in the same place, so the
// layout never falls back to the game's own menus.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "raylib.h"

namespace gh::pm {

// Options: Psych Engine interface, and its "Psych menus" (the menus too).
bool on();

// A Sparrow sheet (picture + XML) with its animations by prefix.
class Sheet {
public:
    bool load(const std::string& png);  // the .xml beside it
    void unload();
    bool ok() const { return tex_.id != 0 && !frames_.empty(); }
    // The frames of an animation: those named prefix + number, in order.
    std::vector<int> anim(const std::string& prefix) const;
    // Frame `i` of `anim` at 24 fps from `t` seconds (looping or held on the last).
    int frameAt(const std::vector<int>& anim, double t, bool loop = true, double fps = 24) const;
    // Draws frame `f` with its untrimmed box's top-left corner at pos.
    void draw(int f, Vector2 pos, float scale, Color tint) const;
    Vector2 size(int f) const;  // its untrimmed size

private:
    struct Frame {
        std::string name;
        Rectangle src{};
        float fx = 0, fy = 0, fw = 0, fh = 0;
        bool rotated = false;
    };
    Texture2D tex_{};
    std::vector<Frame> frames_;
};

// The menus' pictures, read again when the Psych copy changes.
struct Assets {
    Texture2D menuBG{}, menuDesat{}, menuMagenta{}, tracks{};
    std::map<std::string, Sheet> items;  // mainmenu/menu_<name>
    Sheet arrows, checkbox, logo, gfTitle, enter;
    std::map<std::string, Texture2D> difficulties;  // menudifficulties/<name>.png
    std::vector<std::string> roots;
};
Assets& assets();

// The backgrounds: menuBG (yellow), menuDesat tinted, menuBGMagenta. Sized
// as Psych sizes them (`scale` x the picture, centred), moved up by `scrollY`.
enum Bg { kMenuBG, kDesat, kMagenta };
void background(Bg which, Color tint = WHITE, float scrollY = 0, float scale = 1.0f, float alpha = 1.0f);

// Alphabet's bold letters (the menus' items) or the plain ones (option
// names); without the copy's alphabet, the game's bold font in its look.
// Returns the width drawn. `height`: a capital letter's.
float bold(const std::string& s, float x, float y, float height, float alpha = 1.0f);
float plain(const std::string& s, float x, float y, float height, float alpha = 1.0f);
float boldWidth(const std::string& s, float height);
float plainWidth(const std::string& s, float height);

// vcr.ttf as a FlxText of that size (align -1 left, 0 centre, 1 right) with
// its outline; the game's font without the copy's.
void vcr(const std::string& s, float x, float y, float size, Color c = WHITE, int align = -1, float border = 1.25f);
float vcrWidth(const std::string& s, float size);

// A main menu item: mainmenu/menu_<name> ("<name> basic"/"idle", "<name>
// white"/"selected") centred on c, or `label` in the same orange-and-yellow
// look when the copy has no picture for it. Its size, for the layout.
void menuItem(const std::string& name, const std::string& label, Vector2 c, bool selected, float alpha, double t);
Vector2 menuItemSize(const std::string& name, const std::string& label);

// Psych's options checkbox (checkboxanim), its top-left corner at pos.
void checkbox(Vector2 pos, bool on, double sinceChange, float alpha);
// Story mode: the arrows of campaign_menu_UI_assets and a difficulty's picture.
void storyArrow(bool left, bool pushed, Vector2 pos);
Vector2 storyArrowSize();
void difficulty(const std::string& name, Vector2 center, float alpha, Color fallbackColor);

}  // namespace gh::pm
