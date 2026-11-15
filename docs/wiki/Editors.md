# Editors

Main menu `7` opens the editors, as Psych Engine's Master Editor Menu. First pick the mod they work on (files are saved into it), then the editor. Every editor keeps the fields of a file it does not know, and ESC asks twice before leaving unsaved changes. See also: [Charts](Charts.md), [Lua API](Lua-API.md).

| Editor | What it edits |
|---|---|
| [Chart Editor](#chart-editor) | A song's chart (Freeplay `7` or `7` during a song also open it) |
| [Character Editor](#character-editor) | `characters/<name>.json` |
| [Stage Editor](#stage-editor) | `stages/<name>.json` |
| [Event Editor](#event-editor) | `custom_events/<name>.txt` and `.lua` |
| [Script Editor](#script-editor) | The mod's Lua and HScript scripts and its shaders |
| [Chart Converter](#chart-converter) | The mod's charts to binary `.ghc` files |

In the editors' panels, the `-` and `+` buttons and the mouse wheel step a number (SHIFT: 10 steps). A click on the number types a value in: ENTER or a click elsewhere keeps it, ESC drops it.

## Chart Editor

A Psych / JS Engine style grid: the events lane, the opponent's lanes, then the player's, with time flowing down past a fixed line. It saves Psych JSON that God's Hand, Psych Engine and JS Engine all read, keeping the original file as `.bak`. It stays smooth on charts of millions of notes: it draws only what is on screen, at most one note per pixel row, texture by texture.

### Keys

| Key | Does |
|---|---|
| `SPACE` | Play / pause |
| Wheel, `UP` / `DOWN`, `W` / `S` | Move by one snap |
| `A` / `D`, `PAGE UP` / `PAGE DOWN` | Previous / next section (`SHIFT`: 4 sections) |
| `HOME` / `END` | Start / end of the song |
| `LEFT` / `RIGHT` | Snap (1/4 to 1/192 of a bar) |
| `Z` / `X` | Zoom out / in |
| Click | Place a note, or select one (a second click on a selected note removes it; `CTRL`+click adds to the selection) |
| `SHIFT` + drag | Box selection |
| `CTRL+A` | Select the section's notes |
| `CTRL+C` / `CTRL+X` / `CTRL+V` | Copy / cut / paste the selection (or the whole section) |
| `DELETE` | Delete the selection (or clear the section) |
| `Q` / `E` | Shorter / longer holds for the selection |
| `T` | Next note type for the selection |
| `M` | Mirror the selection's lanes |
| `TAB` | Camera on the player or the opponent for this section |
| `[` / `]` | Section BPM -1 / +1 (`SHIFT`: 0.1) |
| `-` / `=` | Scroll speed |
| `CTRL+Z` or `F3` / `CTRL+Y` | Undo / redo (undo keeps only what changed, so it is instant on huge charts) |
| `CTRL+S` or `F2` | Save |
| `ENTER` | Save and play the song |
| `ESC` | Back to Freeplay |

### Tabs

- **Song**: title, characters, stage, BPM, scroll speed, keys; Save, Save and play; **Reload audio** and **Reload chart** (from disk, without leaving; with unsaved changes, press it twice); clear all notes or events; **Save as** Psych JSON, Binary .ghc or Both ([binary charts](Charts.md#binary-charts-ghc): the `.ghc` is written straight from the notes, so a chart too big for JSON can be kept as `.ghc` alone); drop an FL Studio `.flp` / `.fsc` on the window to import it (SNIFF: C3–D#3 player, C4–D#4 opponent, F3/F#3 camera).
- **Section**: beats and time signature (7/16, 5/4...; optionally for every section after this one), section BPM, must-hit (camera), GF section, alt animation; copy, cut, paste, clear, swap sides, mirror lanes; **Sections back** + *Copy notes from that section* (any distance back); **Sections _ to _** + *Copy that range of sections* (JS Engine: their notes and events go to the clipboard, and paste puts them at the line).
- **Note**: the selected note's time, hold length and stacked notes, typed exactly; stack the selection; fill the section with one note per snap on a lane.
- **Events**: the events at the line (step through those sharing one time), their name from a list (Psych's, JS Engine's and the mod's own) and two values; add and remove. A click on the events lane adds or selects one; right click removes it.
- **Charting**: playback rate; music and voices volume, mute voices; **hit sounds** for the player and the opponent, each with its own volume; metronome; snap and zoom; **Exact placement (section starts)** (a click only picks a note in that very cell, so a note can go at a section's start right after one at the end of the section before); **Chart preview** (a small window bottom left with the chart as it will be played: strums and notes at the song's scroll speed).
- **EZ Spam**: JS Engine's Note Spamming: spam count and multiplier (EZ Spam Mode: each note placed becomes that many), stretch, shift and duplicate the section's notes, stairs and other patterns.

The editor's hit sounds follow the notes: each note plays on its own sample of the song, and a fast spam becomes a tone at the rate of its notes (16 notes a step at 520 BPM sound at 555 Hz) instead of a louder, crackling pile.

## Character Editor

A Psych character: its animations (with the image's own names), their offsets (arrow keys, over a ghost of the idle animation), image, scale, flip, anti-aliasing, health icon and bar colour (Psych's "Get Icon Color"), position, camera point and sing duration. Saves `characters/<name>.json`.

## Stage Editor

A Psych stage, drawn as in the game: its JSON objects, its Lua script's sprites and its shaders all run in the editor.

- **Stage**: zoom, camera speed, pixel stage, hide girlfriend; where the three characters stand, their layer, and where the camera looks at each. Drag a character with the left mouse button; drag the view with the right; the wheel zooms; `C` shows or hides the camera's frame; `R` resets the view.
- **Objects**: the stage JSON's objects (Psych 0.7.3). Click one in the view to pick it, then drag it to move it. Edit its position, scale, scroll factor, flip, alpha, angle, anti-aliasing and layer. Add a sprite from an image of the mod, copy one, or delete one.
- `CTRL+S` saves `stages/<name>.json`; *Save as* names a new stage.

## Event Editor

New events for charts from templates (flash, HUD fade, shake, zoom, sound, scroll speed): `custom_events/<name>.txt` (what the chart editor shows about it) and `<name>.lua` (what it does).

## Script Editor

The mod's scripts (`.lua`, `.hx`, `.hxs`, `.hscript`) and shaders (`.frag`, `.vert`) in a file list, edited in the game, with line numbers, colours for keywords, strings and comments, and undo (`CTRL+Z`). A click picks a file or places the cursor; the arrows, `HOME`, `END`, `PAGE UP` and `PAGE DOWN` move it; `TAB` puts in four spaces. `CTRL+S` saves and keeps the file as it was in `.bak`. `CTRL+N` makes a new Lua script. `CTRL+PAGE UP` / `CTRL+PAGE DOWN` switch files.

## Chart Converter

Lists the mod's charts with their sizes. `ENTER` converts the one selected to `.ghc` and `A` converts them all, on a thread of its own. The JSON stays, for other engines; the game loads the `.ghc`. Each line shows how much smaller the file got and how long it took. See [Charts](Charts.md#binary-charts-ghc).
