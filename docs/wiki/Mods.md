# Mods

God's Hand plays Friday Night Funkin' mods made for Psych Engine (0.4 to 0.7.3 and 1.0), JS Engine, Kade and the base game, V-Slice songs and PulseForge charts. See also: [Charts](Charts.md), [Lua API](Lua-API.md), [Editors](Editors.md).

## Installing

- **Computers**: put the mod in the `mods/` folder next to the game, as a folder or a `.zip`. A `.zip` is unpacked into a folder of its own when the game starts; the archive moves to `mods/.installed/`.
- **Android**: the game's files are in `Android/data/com.godshand.game/files/`, with `mods/` inside. Freeplay → **IMPORT ZIP** installs a `.zip` picked with the system's file picker.
- **Mod packs**: Freeplay `M` downloads and installs packs from the game's `mods` release (45 packs, 803 songs from JS Engine mods among them). Not all of these mods are by the game's author: they belong to their creators.

Freeplay lists every song the game finds; the mod's name comes from its `pack.json` or `mod.json` (`"name"`), else its folder.

## What a mod folder holds

The Psych Engine layout:

| Folder | What goes there |
|---|---|
| `data/<song>/` | Charts (`<song>-<difficulty>.json`), `events.json`, the song's own scripts |
| `songs/<song>/` | `Inst.ogg` and `Voices.ogg`, or separate voices for each side (`Voices-Player` / `Voices-Opponent`, `Voices-bf` / `Voices-dad`...); `.mp3` and `.wav` work too |
| `characters/` | Character JSON files (Psych or V-Slice) and their scripts |
| `stages/` | Stage JSON files (with Psych 0.7.3 objects) and stage scripts |
| `images/` | Pictures and atlases (Sparrow `.xml`, Packer `.txt`), `icons/`, note skins |
| `sounds/`, `music/`, `fonts/`, `videos/`, `shaders/` | What scripts and stages load by name |
| `scripts/` | Scripts that run in every song of the mod |
| `custom_events/`, `custom_notetypes/` | Events and note types: a `.txt` description and a script |
| `weeks/` | Psych's week files. God's Hand lists every song of the mod in Freeplay; its Story mode has the game's own songs |
| `data/settings.json` | The mod's settings, read with `getModSetting` |

The game also finds songs in other layouts: mods inside mods; full game builds with charts in `assets/preload/data/` or `assets/shared/data/` and audio in `assets/songs/` (Psych 0.4–0.7, H-Slice, Psych EK); a build's own `mods/` folder; `data/charts/<song>/`; charts loose in `data/`; compiled builds inside source trees; PulseForge projects; and folders with only audio (AutoChart charts them). Identical copies of a chart are listed once.

## Shared assets

A character, stage, image, sound, video, shader or font a mod uses but does not have is looked up in the rest of its own pack first, then in every other installed mod. A borrowed stage, character, note type or event brings its own script; the other mod's global scripts (`scripts/`) do not run. Some mod packs hold only assets, for this.

## What runs

- **Lua 5.4** with Psych's API ([Lua API](Lua-API.md), [Callbacks](Callbacks.md)), Kade modchart helpers, and JS Engine's extra functions.
- **HScript** (`runHaxeCode`, `.hx` files): [HScript](HScript.md).
- **Shaders** on sprites and cameras, and JS Engine's screen effects: [Shaders](Shaders.md).
- **Videos**: `startVideo` cutscenes and video sprites (`.mp4`/`.webm` are converted once with ffmpeg when it is installed; `.mpg` plays as it is).
- **Events and note types**: Psych's, JS Engine's, V-Slice's and the mod's own.
- **Base game and Kade builds**: the week 1–6 characters, stages and stage behaviour are rebuilt from the mod's own images.

Scripts run in a sandbox (no `io`, `os.execute` or `debug`; files can only be written inside `mods/`), and a script stuck in a loop is stopped with a warning instead of freezing the game. Options → DEBUG chooses when script errors and warnings show.

## Where the game keeps things

Settings, saves (`saves/`, from `initSaveData`), replays, caches and rendered videos are in the user's folder: `%APPDATA%\GodsHand` (Windows), `~/Library/Application Support/GodsHand` (macOS), `~/.local/share/GodsHand` (Linux), the app's folder on Android. A `portable.txt` next to the game keeps everything beside it instead.
