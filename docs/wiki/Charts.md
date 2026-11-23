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
| God's Hand bigits | `.ghb`, the smallest (below) |
| Count charts | God's Hand's `.ghn`, R-Slice's `.raid` and the iutk engine's `Chart.bin` (FNC1) (below) |
| Threadripper engine | `.thr`, as the strand engine reads it (below) |
| AutoChart | `.ghac`, saved by the AUTOCHART screen; God's Hand only, with a shame watermark (below) |
| Audio only | a folder with a song and no chart: AutoChart makes one for every track of 30 s or more |

Charts can have 1 to 1024 keys a side (the chart editor makes them too): `keyCount` (up to 100), or `mania` (Dave & Bambi, Kade, Andromeda, Psych Extra Keys up to `mania` 17 for 18 keys, Shaggy), or Denpa Engine's `options.mania`. The notes decide which numbering a chart uses; a chart without either has 4 keys, as in Psych. Options → GAMEPLAY → Extra keys OFF plays everything with 4 keys.

- **10 to 18 keys** use the lane widths, arrow directions and keys of the extra keys scripts of JS Engine mods (18 keys: `Q W E R A S D F V N H J K L U I O P`). Options → CONTROLS → **Key bindings** changes the key of every lane, for each key count from 1 to 18 (a Bluetooth keyboard works on Android too).
- **19 to 1024 keys**: the lanes narrow to fit and take the keyboard's keys row by row (`1`–`0`, `Q`–`]`, `A`–`'`, `Z`–`/`: 43 keys); the bot plays the lanes past them for you. The chart editor scrolls sideways through charts wider than its grid (`Ctrl` + wheel, or the bar over the lanes).
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

## Bigit charts (.ghb)

The smallest of the game's formats, made for issue #36's "try bigits". A *bigit* is a digit of a number too big for any machine integer, as bignum libraries call them: the whole chart's notes become one such number. An arithmetic coder narrows an interval note by note, each note taking a share as large as the chance the model gave it, and the file keeps the digits of a number inside the final interval. A note the model expected costs a fraction of a bit; a surprise costs more.

- **The models**, learnt as the notes go by (nothing is stored but the bigits): charts repeat themselves, so after the same six notes as before the note that followed them then is expected again; otherwise the gap to the note before (the same again, a chord, one of the last four gaps, or a new one), the lane from the lanes before it, the sustain, the stack and the note type. A second model mixes predictions over the `.ghc` columns (orders 0 to 3 and a match model, PAQ style). Each block of 524,288 notes keeps whichever made it smaller; blocks are decoded on every core.
- **Nothing is lost**: the game reads a `.ghb` as the chart it was made from, exactly as its `.ghc`.
- **Sizes** (`.ghc` → `.ghb`): Taimuresu Overkill 54.6 KB → **35.0 KB** (15.9 MB as JSON); the spam chart of 3.42 million notes 2.41 MB → **2.08 MB** (201.1 MB as JSON).
- **What it trades**: a model predicts slower than deflate unpacks, so the biggest charts load 0.4 to 0.8 s slower as `.ghb` than as `.ghc`.
- **Making one**: `godshand --convert <chart> <out.ghb>` (any chart the game reads). When a song has both, the `.ghc` plays; a `.ghb` wins over a JSON of the same name.

The file layout is described at the top of `src/bigit_chart.hpp`.

## Threadripper charts (.thr)

The threadripper engine's charts, as buatedz's [strand](https://github.com/buatedz/strand) engine reads them:

- a zip archive (zip64 too) with `<song>-metadata.json` and `notes/<prefix>sections.bin`, `notes-00000.bin`...;
- entries stored, deflated or packed with Zstandard (LZMA-packed ones are refused, saying so);
- 16-byte note records, with their byte planes and time differences undone ("filtered");
- `noteData` on the section's mustHit side, or the player's for `psych_v1`; note types through the same names as JSON charts (Hurt Note: a mine).

Song folders list `.thr` charts, and `--convert` reads them into any other format and writes them (deflated and filtered, each counted note listed, up to 2^31).

KREMK, also asked for in issue #36, could not be found anywhere; a sample file would let the game read it.

## Count charts (.ghn, .raid, iutk)

Some engines store a chart as **cells**: a time, a lane and how many notes are stacked there. God's Hand plays these as its own charts: each cell is one note that counts as all of its notes (one press hits the stack, and score, combo, misses and every total count each note). Counts are exact at any size the game plays: **256-bit numbers**, up to 2^256 − 1 (about 1.16 × 10^77); a note holds up to 4,294,967,295 itself, more go to a table of the chart. A song folder with any of these files lists it as a difficulty (a `.ghn` wins over a `.json` or `.ghc` of the same name).

| Format | Engine | What it holds |
|---|---|---|
| `.bin` (`FNC1`, versions 2, 3, 4) | [iutk engine](https://codeberg.org/IAmCyklus/iutk-engine) (`data/Chart.bin`) | 8 lanes (0–3 the opponent's, 4–7 the player's), times in whole milliseconds, a 64-bit count a cell. Version 4 packs times, lane masks and counts as run-length varint streams with a CRC-32 each. The iutk engine plays at 120 BPM, and so do its charts here. |
| `.raid` (versions 1, 2) | [R-Slice](https://github.com/TofpFL/R-Slice) (RAID Engine) | microsecond events with lengths, lanes, note types and a 16-bit stack, in deflated chunks of ops: literal runs, and patterns from a table repeated, moved in time and lane, grouped and clipped, or burst; the chart's fields in a JSON section; CRC-32s on everything. Charts of up to 419 million events load (more is refused, with the count). |
| `.ghn` (version 1) | God's Hand | everything a chart holds, with **counts of any size** (below) |

### .ghn, God's Hand Notes

Made to go past both: iutk's counts stop at 2^64, `.raid` stacks at 65,535, and neither keeps everything a chart has.

- **Counts of any size**: each note's count is a variable-length number with no limit (2^256 − 1 takes 37 bytes; the game refuses more, saying so, instead of cutting it). The totals of each side and of the file are written as decimal numbers of any length and checked when the chart loads.
- **Everything a chart has**: title, characters, stage, keys a side (2048 lanes), scroll speed, note types, tempo changes, camera focus, sections and Psych events; sustains and note flags; times in nanoseconds (292 years either way).
- **Blocks**: notes in blocks of 65,536 (columns of time steps, lanes, kinds, types, lengths and counts, deflated), each with a CRC-32, decoded on every core at once.
- **Repeats**: a block that is an earlier one moved in time is stored as a repeat of it — up to 2^64 − 1 copies a given shift apart. A spam chart that repeats one pattern is stored once. Copies that land in the same millisecond of a lane fold into one note counting all of them (the game judges a lane a millisecond at a time), so a trillion notes a nanosecond apart load as a million notes, each counting a million.
- **Sizes**: Taimuresu Overkill (1,059,988 notes): JSON 15.9 MB, `.ghc` 54.6 KB, `.ghn` **47.9 KB**, iutk FNC1 3.5 MB. For Hire (4,680 notes): JSON 238 KB, `.ghc` 5.6 KB, `.ghn` 5.3 KB.
- **Making one**: `godshand --convert <chart> <out.ghn>` (any chart the game reads; `.ghc` and iutk's `.bin` too, by the output's extension), or the chart editor's Song tab, **Save as: Count .ghn** (each stack is one note with its count; a chart that would list more than 2^31 notes one by one in JSON is saved this way).

The layouts of all three are described at the top of `src/count_charts.hpp`.

## AutoChart (.ghac)

Options → INTERFACE → **AutoChart** puts **AUTOCHART** on the main menu. Its song list opens a screen laid out as FL Studio:

- the **channel rack**: each of the song's sounds (Inst, Voices, the other stems, or an FL Studio project's channels) with its role (player, opponent, both in turns, off), its lanes (by pitch, drums, in turn, or SNIFF's keys), its amount and a mute;
- the **playlist** with the waveforms and the FL channels' notes, and a **tempo lane**: BPM changes are found in the audio or given, added, changed with the wheel and removed;
- **GENERATE**, **PLAY CHART**, **SAVE** (a `.ghac` beside the song), **TRAIN**, **+ AUDIO** and **+ FLP** (an FL Studio project's channels, where its playlist places them).

**Training** (optional; Options → **AutoChart training** uses it): a model learns from every charted song of the installed mods when a note comes, whose it is, its lane and how many notes a second a song this loud gets, from the instrumental and both voices split into bands and from the beat. Each song is learned once, it is saved as it goes and taken up again, and spam charts are skipped (`godshand --train-autochart` does the same).

AutoChart's charts are saved only as **`.ghac`** (a `.ghn` with its own magic), which no other engine reads: `--convert` refuses to make one from a real chart, and the editors keep them as `.ghac`. Saved AutoCharts are listed as "AutoChart <difficulty>" of their song. Every AutoChart chart plays with **AUTOCHARTED** across the middle of the screen, semi-transparent, for the whole song, whatever the options.

## JSON merging

Options → INTERFACE → **JSON merging** puts **JSON MERGING** on the main menu: a chart split into parts (as players do to get past JS Engine's 16-million-note limit) merged back into one, with no limit on its notes.

- **Picking the parts**: the system's file manager opens to pick them all at once (Windows' dialog, osascript on macOS, zenity or kdialog on Linux, the document picker on Android). The screen lists each part's notes, sections and time span, in name order (`song-1`, `song-2`, ..., `song-10`); SHIFT+UP/DOWN, the buttons or dragging reorder them.
- **Two shapes of split chart**, found by themselves or chosen: **overlay** (each part is the whole song with some of its notes) and **append** (each part goes on after the one before; parts whose notes start again from 0 are moved after the others, with a BPM change where a part's tempo differs).
- **MERGE JSON** shows its progress and can be stopped. The chart is written as it is made, a big part's notes waiting in a temporary file, so any number of notes merges in the memory of one part. Events are kept once, Psych 1.0 charts stay Psych 1.0, and an existing file is never written over.
- From the command line: `godshand --merge <out.json> <part> ... [--merge-mode auto|overlay|append]`.

## Events

Events come from the chart's own `events` list and from `events.json` beside it. Psych's events, JS Engine's (Set/Tween Camera Zoom, Camera Bopping, Camera Twist, Change Note Multiplier, Rainbow Eyesore...) and V-Slice's (FocusCamera, ZoomCamera, SetCameraBop) are built in. Any other event runs the mod's `custom_events/<name>.lua` (see [Callbacks](Callbacks.md#events)).

Dek Engine (Vs Dek V2) builds some events into its executable, so its songs use them with no script. God's Hand plays these, unless the mod has a `custom_events` script of the same name:

| Event | Value 1 | Value 2 |
|---|---|---|
| Camera rotate on | how fast the game camera sways (radians a second, in song time) | how far it sways each way, in degrees |
| Camera rotate off | - | - (the camera goes straight again) |
| Flash | how long a white flash over everything lasts, in seconds | how strong it is (1 = full) |
