#!/usr/bin/env python3
# God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
# All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.
"""Makes the game's icon files from the pictures the game draws itself.

    godshand --make-icon <dir>        (src/app_icon.cpp: DJ Hand's glove
                                       over the menus' synthwave sun)
    python3 tools/make_icons.py <dir> (needs Pillow)

writes
    src/windows/godshand.ico              the Windows program's icon (16 to 256 px)
    android/res/mipmap-*/ic_launcher*.png Android's launcher icon (adaptive
                                          layers, and the plain one for
                                          Android 7)
    android/icon-512.png                  for stores and pages
The window's icon on Linux and the Dock's on macOS are drawn when the game
starts, so they need nothing from here.
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def load(src, name):
    path = os.path.join(src, name)
    if not os.path.isfile(path):
        sys.exit(f"{path} is missing: run godshand --make-icon {src} first")
    return Image.open(path).convert("RGBA")


def sized(img, px):
    return img.resize((px, px), Image.LANCZOS)


def save_png(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    print("  " + os.path.relpath(path, ROOT))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    icon = load(src, "icon-1024.png")
    background = load(src, "android-background-1024.png")
    foreground = load(src, "android-foreground-1024.png")

    ico = os.path.join(ROOT, "src", "windows", "godshand.ico")
    frames = [sized(icon, s) for s in ICO_SIZES]
    frames[-1].save(ico, format="ICO", sizes=[(s, s) for s in ICO_SIZES], append_images=frames[:-1])
    print("  " + os.path.relpath(ico, ROOT))

    for name, k in DENSITIES.items():
        folder = os.path.join(ROOT, "android", "res", "mipmap-" + name)
        layer = round(108 * k)  # adaptive icon layers: 108 dp, a launcher shows the middle 72 dp
        save_png(sized(background, layer), os.path.join(folder, "ic_launcher_background.png"))
        save_png(sized(foreground, layer), os.path.join(folder, "ic_launcher_foreground.png"))
        save_png(sized(icon, round(48 * k)), os.path.join(folder, "ic_launcher.png"))  # Android 7 and older
    save_png(sized(icon, 512), os.path.join(ROOT, "android", "icon-512.png"))


if __name__ == "__main__":
    main()
