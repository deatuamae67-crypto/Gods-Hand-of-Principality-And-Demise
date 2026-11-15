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

Charts can have 1 to 9 keys: `keyCount`, or `mania` (Dave & Bambi, Kade, Andromeda, Psych Extra Keys, Shaggy). The notes decide which numbering a chart uses; a chart without either has 4 keys, as in Psych. Options → GAMEPLAY → Extra keys OFF plays everything with 4 keys.

## Huge charts

Charts of millions of notes load in seconds:

- From 8 MB on, a chart is streamed: one thread reads ahead while the sections are parsed on every core, and JSON's structural characters are found 64 bytes at a time with SIMD (SSE2 or NEON). A 200 MB chart loads in about 0.3 s; 1.5 GB / 25 million notes in about 5 s, with bounded memory.
- Big charts are kept in a cache (`cache/charts/` in the user's folder, up to 4 GB), so they load faster the second time.
- Notes stacked on the same millisecond stay merged in memory, but each one counts for hits, combo, score and totals.

## Binary charts (.ghc)

A `.ghc` file holds a Psych chart in a fraction of its JSON size and loads faster. Nothing is lost: the game reads it as the chart it was made from.

- **How it is built**: the song data, sections and events stay as compressed JSON. The notes become columns: times, hold lengths, lanes, stacks and note types. Each time is stored as how much the gap from the note before changed, in nanoseconds; hold lengths are stored in microseconds. Small numbers like these pack very well. The columns are compressed with deflate in blocks of 262,144 notes, and the blocks are unpacked on every core at once. Deflate and not a stronger packer: past this point a smaller file costs more time to unpack than it saves to read.
- **How much smaller**: a spam chart of 3.42 million notes went from 201.1 MB of JSON to 2.4 MB (84.6× smaller, converted in 0.31 s) and loads in 150 ms instead of 240 ms. A small chart of 1,400 notes went from 34.5 KB to 2.6 KB (13.4× smaller).
- **Making one**:
  - main menu `7` → **Chart Converter**: pick a mod, then convert one chart or all of them. Each line shows how much smaller its file got.
  - the chart editor's Song tab, **Save as**: *Psych JSON*, *Binary .ghc* or *Both*. The `.ghc` is written straight from the notes, without writing the JSON first, so a chart too big to keep as JSON can be edited and saved as `.ghc` alone. A chart opens with the choice that matches its files (a `.ghc` alone saves as `.ghc`). Saving as Psych JSON while a `.ghc` is beside it moves the old `.ghc` aside (to `.ghc.bak`, or deletes it when the original is already kept there), because the game would otherwise go on playing the old one.
- **Which file plays**: a `.ghc` alone is a chart like any JSON. When a song has both `<chart>.json` and `<chart>.ghc`, Freeplay lists the song once and plays the `.ghc`. Keep the JSON if other engines should still read the song; they do not know `.ghc`.

The file layout is described at the top of `src/chart_binary.cpp`.

## Events

Events come from the chart's own `events` list and from `events.json` beside it. Psych's events, JS Engine's (Set/Tween Camera Zoom, Camera Bopping, Camera Twist, Change Note Multiplier, Rainbow Eyesore...) and V-Slice's (FocusCamera, ZoomCamera, SetCameraBop) are built in. Any other event runs the mod's `custom_events/<name>.lua` (see [Callbacks](Callbacks.md#events)).
