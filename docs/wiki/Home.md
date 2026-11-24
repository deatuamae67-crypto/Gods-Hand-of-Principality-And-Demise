# God's Hand wiki

God's Hand of Principality and Demise is a rhythm game engine in C++ that plays Friday Night Funkin' mods made for Psych Engine, JS Engine and Kade, and its own songs. This wiki describes what it does for modders and players, for version **2.3.0**. It lives in the repository (`docs/wiki/`), so it changes with the code.

| Page | What it covers |
|---|---|
| [Options](Options.md) | Every option of the Options menu, tab by tab |
| [Lua API](Lua-API.md) | All 326 functions Lua scripts can call, and the variables they see |
| [Callbacks](Callbacks.md) | The functions the game calls in scripts (onCreate, goodNoteHit, onEvent...) |
| [HScript](HScript.md) | `runHaxeCode` and `.hx` scripts: the language and the classes |
| [Shaders](Shaders.md) | Shaders on sprites (in layers) and cameras, and JS Engine's screen effects |
| [Charts](Charts.md) | The chart formats the game reads, the binary `.ghc` and bigit `.ghb`, the count charts (`.ghn`, `.raid`, iutk), `.thr`, AutoChart, JSON merging and the converters |
| [Editors](Editors.md) | The chart, character, stage, event and script editors, and the chart converter |
| [Mods](Mods.md) | Where mods go, what a mod folder holds, mod packs (and their changes) and shared assets |

## In short

- **Mods**: put Psych Engine mods (folders or `.zip`) in `mods/` next to the game; Freeplay lists every song it finds. See [Mods](Mods.md).
- **Scripts**: Lua 5.4 with Psych Engine's API, plus HScript (`runHaxeCode`, `.hx` files). Scripts written for Psych 0.4 to 0.7.3, JS Engine and Kade modcharts run unchanged in most cases.
- **Editors**: main menu `7` opens the editors; `7` in Freeplay or during a song opens the chart editor. See [Editors](Editors.md).
- **Huge charts**: charts of millions of notes load in seconds and can be turned into `.ghc` files many times smaller (`.ghb` smaller still); split charts merge back into one with no note limit; count charts (`.ghn`, R-Slice's `.raid`, the iutk engine's `Chart.bin`) hold notes by the trillion, counted exactly. See [Charts](Charts.md).
- **The Psych look**: with the Psych Engine interface (on by default), the menus, Freeplay, Story Mode, Options, Credits and the pause menu are Psych's, with the pictures of your own Psych/JS Engine copy. See [Options](Options.md).
- **Older systems**: `GodsHand-windows-vista-7-x86.zip` runs on Windows Vista and 7 (32 or 64-bit) and on graphics cards with OpenGL 2.1.
- **Updates**: the game finds a newer version on GitHub, downloads and checks it, and installs it the next time it starts; it also finds changes in the mod packs you installed (Options → INTERFACE → Auto-update). See [Mods](Mods.md).

## Reporting problems

Open an [issue](https://github.com/deatuamae67-crypto/Gods-Hand-of-Principality-And-Demise/issues): describe what happens, what you expected, and attach the mod or script when you can. Issues are the only way to contribute (see the license in the README).
