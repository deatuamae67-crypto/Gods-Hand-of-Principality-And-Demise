# Charts

See also: [Editors](Editors.md), [Mods](Mods.md).

## Formats the game reads

| Format | Files |
|---|---|
| Psych Engine 0.4 to 0.7.3, JS Engine, Kade, the base game | `data/<song>/<song>-<difficulty>.json` (a `song` object with `notes` sections), `events.json` beside it |
| Psych Engine 1.0 | the same, with `"format": "psych_v1"` |
| V-Slice (FNF 0.3+) | `<song>-chart.json` with `<song>-metadata.json` |
| PulseForge | an exported `chart.json`, editor projects (`.pfchart.json`) and their autosaves |
| FL Studio (SNIFF) | `.flp` and `.fsc` projects in a mod: one difficulty per pattern; or dropped on the chart editor |
| God's Hand binary | `.ghc` (below) |
| Audio only | a folder with a song and no chart: AutoChart makes one for every track of 30 s or more |

Charts can have 1 to 100 keys a side: `keyCount` (up to 100), or `mania` (Dave & Bambi, Kade, Andromeda, Psych Extra Keys up to `mania` 17 for 18 keys, Shaggy), or Denpa Engine's `options.mania`. The notes decide which numbering a chart uses; a chart without either has 4 keys, as in Psych. Options → GAMEPLAY → Extra keys OFF plays everything with 4 keys.

- **10 to 18 keys** use the lane widths, arrow directions and keys of the extra keys scripts of JS Engine mods (18 keys: `Q W E R A S D F V N H J K L U I O P`). Options → CONTROLS → **Key bindings** changes the key of every lane, for each key count from 1 to 18 (a Bluetooth keyboard works on Android too).
- **19 to 100 keys**: the lanes narrow to fit and take the keyboard's keys row by row (`1`–`0`, `Q`–`]`, `A`–`'`, `Z`–`/`: 43 keys); the bot plays the lanes past them for you.
- **Leather Engine** charts can give each side its own key count: `keyCount` is the opponent's and `playerKeyCount` the player's. A note's value belongs to the side the section is on when it is under that side's key count, and to the other side otherwise; its lane is the value modulo its side's keys, as in Leather.
- **Scripts that fake extra keys** in a 4-key engine (the "Extra Keys Script" of JS Engine mods, which builds its own arrows) do not run: the game plays the chart's keys itself.

## Huge charts

Charts of millions of notes load in seconds:

- From 8 MB on, a chart is streamed: one thread reads ahead while the sections are parsed on every core, and JSON's structural characters are found 64 bytes at a time with SIMD (SSE2 or NEON). A 200 MB chart loads in about 0.3 s; 1.5 GB / 25 million notes in about 5 s, with bounded memory.
- Big charts are kept in a cache (`cache/charts/` in the user's folder, up to 4 GB), so they load faster the second time.
- Notes stacked on the same millisecond stay merged in memory, but each one counts for hits, combo, score and totals.

## Binary charts (.ghc)

A `.ghc` file holds a Psych chart in a fraction of its JSON size and loads faster. Nothing is lost: the game reads it as the chart it was made from.

- **How it is built**: the song data, sections and events stay as compressed JSON. The notes become columns: times, hold lengths, lanes, stacks and note types. Each time is stored as how much the gap from the note before changed, in nanoseconds; hold lengths are stored in microseconds. Small numbers like these pack very well. The columns are compressed with deflate in blocks of 262,144 notes, and the blocks are unpacked on every core at once. Deflate and not a stronger packer: past this point a smaller file costs more time to unpack than it saves to read.
- **Versions**: version 2 (God's Hand 1.11.0) keeps note values up to 255, for charts of up to 100 keys a side; version 1 files still open.
- **How much smaller**: a spam chart of 3.42 million notes went from 201.1 MB of JSON to 2.4 MB (84.6× smaller, converted in 0.31 s) and loads in 150 ms instead of 240 ms. A small chart of 1,400 notes went from 34.5 KB to 2.6 KB (13.4× smaller).
- **Making one**:
  - main menu `7` → **Chart Converter**: pick a mod, then convert one chart or all of them. Each line shows how much smaller its file got.
  - the chart editor's Song tab, **Save as**: *Psych JSON*, *Binary .ghc* or *Both*. The `.ghc` is written straight from the notes, without writing the JSON first, so a chart too big to keep as JSON can be edited and saved as `.ghc` alone. A chart opens with the choice that matches its files (a `.ghc` alone saves as `.ghc`). Saving as Psych JSON while a `.ghc` is beside it moves the old `.ghc` aside (to `.ghc.bak`, or deletes it when the original is already kept there), because the game would otherwise go on playing the old one.
- **Which file plays**: a `.ghc` alone is a chart like any JSON. When a song has both `<chart>.json` and `<chart>.ghc`, Freeplay lists the song once and plays the `.ghc`. Keep the JSON if other engines should still read the song; they do not know `.ghc`.

The file layout is described at the top of `src/chart_binary.cpp`.

## Events

Events come from the chart's own `events` list and from `events.json` beside it. Psych's events, JS Engine's (Set/Tween Camera Zoom, Camera Bopping, Camera Twist, Change Note Multiplier, Rainbow Eyesore...) and V-Slice's (FocusCamera, ZoomCamera, SetCameraBop) are built in. Any other event runs the mod's `custom_events/<name>.lua` (see [Callbacks](Callbacks.md#events)).

Dek Engine (Vs Dek V2) builds some events into its executable, so its songs use them with no script. God's Hand plays these, unless the mod has a `custom_events` script of the same name:

| Event | Value 1 | Value 2 |
|---|---|---|
| Camera rotate on | how fast the game camera sways (radians a second, in song time) | how far it sways each way, in degrees |
| Camera rotate off | - | - (the camera goes straight again) |
| Flash | how long a white flash over everything lasts, in seconds | how strong it is (1 = full) |
