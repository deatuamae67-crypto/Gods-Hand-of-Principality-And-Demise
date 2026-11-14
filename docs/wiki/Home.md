# God's Hand wiki

God's Hand of Principality and Demise is a rhythm game engine in C++ that plays Friday Night Funkin' mods made for Psych Engine, JS Engine and Kade, and its own songs. This wiki describes what it does for modders and players, for version **1.9.0**. It lives in the repository (`docs/wiki/`), so it changes with the code.

| Page | What it covers |
|---|---|
| [Options](Options.md) | Every option of the Options menu, tab by tab |
| [Lua API](Lua-API.md) | All 311 functions Lua scripts can call, and the variables they see |
| [Callbacks](Callbacks.md) | The functions the game calls in scripts (onCreate, goodNoteHit, onEvent...) |
| [HScript](HScript.md) | `runHaxeCode` and `.hx` scripts: the language and the classes |
| [Shaders](Shaders.md) | Shaders on sprites (in layers) and cameras, and JS Engine's screen effects |
| [Charts](Charts.md) | The chart formats the game reads, the binary `.ghc` format and its converter |
| [Editors](Editors.md) | The chart, character, stage, event and script editors, and the chart converter |
| [Mods](Mods.md) | Where mods go, what a mod folder holds, mod packs and shared assets |

## In short

- **Mods**: put Psych Engine mods (folders or `.zip`) in `mods/` next to the game; Freeplay lists every song it finds. See [Mods](Mods.md).
- **Scripts**: Lua 5.4 with Psych Engine's API, plus HScript (`runHaxeCode`, `.hx` files). Scripts written for Psych 0.4 to 0.7.3, JS Engine and Kade modcharts run unchanged in most cases.
- **Editors**: main menu `7` opens the editors; `7` in Freeplay or during a song opens the chart editor. See [Editors](Editors.md).
- **Huge charts**: charts of millions of notes load in seconds and can be turned into `.ghc` files many times smaller. See [Charts](Charts.md).

## Reporting problems

Open an [issue](https://github.com/deatuamae67-crypto/Gods-Hand-of-Principality-And-Demise/issues): describe what happens, what you expected, and attach the mod or script when you can. Issues are the only way to contribute (see the license in the README).
