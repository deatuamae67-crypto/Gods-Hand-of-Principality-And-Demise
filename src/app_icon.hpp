// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The game's icon, drawn by the game itself in its own look: the window's,
// the Dock's, the Windows program file's (src/windows/godshand.ico) and the
// Android launcher's (android/res/mipmap-*), the last two made from
// `godshand --make-icon <dir>` by tools/make_icons.py.
#pragma once

#include <string>

#include "raylib.h"

namespace gh::appicon {

// The parts of the icon: the menus' synthwave sky, striped sun and grid floor
// (Background) and DJ Hand's glossy glove (Foreground).
enum Layer : int { Background = 1, Foreground = 2, Both = 3 };

// Draws the icon in the square `box` of the current render target.
void draw(Rectangle box, int layers);
// The icon as a size x size RGBA picture. `rounded`: a rounded square with
// see-through corners (desktop); otherwise the whole square (Android layers).
Image render(int size, int layers, bool rounded);
// Desktop: the window's icon (Linux, Windows) and the Dock's (macOS).
void applyToWindow();
// godshand --make-icon <dir>: icon-1024.png (rounded), icon-square-1024.png,
// android-background-1024.png and android-foreground-1024.png.
bool exportAll(const std::string& dir);

}  // namespace gh::appicon
