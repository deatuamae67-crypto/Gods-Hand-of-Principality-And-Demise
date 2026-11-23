charts made in sniff
====================

Charts made in God's Hand for issue #36. Each song has one difficulty per
chart format, all of them the same chart:

  GHN   .ghn  God's Hand's count chart
  GHC   .ghc  God's Hand's binary chart (version 4: counts of any size)
  GHB   .ghb  God's Hand's bigit chart (version 2: counts of any size)
  FNC1  .bin  the iutk engine's chart (counts up to 2^64 - 1, no holds)

How the notes are counted: a note of these files stands for every note at
its time on its lane (stacked notes), and the game counts and judges each of
them - combo, score and note counts include them all. Nothing is multiplied:
each count is how many notes are there, so the counts add up to exactly the
totals below.

Audio: the charts come without audio. Put the song's audio in
songs/<the song's folder>/Inst.ogg (and Voices.ogg, if it has one), or
install a mod with the same song: the game uses that mod's audio when the
folder names match. Without audio, a song ends just after its last note.


Amfoternost (Preobrazovanyi) - data/amfoternost
-----------------------------------------------
Made from the song's FL Studio project (FL Studio 26.1.3.5570, 295 BPM, 768
ticks a beat) by God's Hand's SNIFF converter, through the whole playlist:
57 patterns, 168,623,461 note records, 50,056 pattern clips.

  3,560,023,825,730 notes
    1,780,028,092,421 the player's
    1,779,995,733,309 the opponent's

- Lanes: SNIFF's keys C, C#, D and D# are left, down, up and right; the same
  keys of every octave give the same lanes (the pitch modulo 4), so no note
  is left out.
- Sides: DirectWave and DirectWave (Alt) play the player's notes,
  DirectWave #2 and DirectWave #2 (Alt) the opponent's.
- Holds, as SNIFF makes them: a note played softly (velocity under 64), or a
  beat long or more, holds for its length less a step.
- Alt Animation: the notes of the (Alt) channels, and portamento notes.
- Notes on the same lane less than 1 ms apart are one note that counts as
  all of them (the game times notes to the millisecond): 1,559,568 notes in
  the files; the most at one time on one lane is 19,313,960.
- The notes start at 1:44; nearly all of them are between 16:00 and 23:30.

Made with:
  godshand --convert "Amfoternost (Preobrazovanyi).flp#playlist" amfoternost-GHN.ghn
  godshand --convert amfoternost-GHN.ghn amfoternost-GHC.ghc   (and .ghb, .bin)

Not as .thr: a .thr lists every note one by one, and 3.56 trillion notes
would take terabytes. Not as .raid: R-Slice's events stack 65,535 notes at
most, so this chart would be 55,725,072 events (God's Hand reads .raid but
does not write it).


Andromeda Galaxy - data/andromeda-galaxy (the omniverse, 0:36 to 2:05)
Hopeless (Pixelified) - data/hopeless-pixelified (vs. redacted fantrack, 2:22 to 3:40)
-----------------------------------------------------------------------------------
Max-notes charts, 4 keys, the player's side alone:

  115,792,089,237,316,195,423,570,985,008,687,907,853,269,984,665,640,564,039,457,584,007,913,129,639,935
  notes each: 2^256 - 1, the most notes the game counts.

A note every millisecond on each of the 4 lanes, over the song's part
(356,004 notes in the Andromeda Galaxy files, 312,004 in Hopeless's), each
counting an equal share of the total (some one more than others, so that
they add up exactly). Their tempo, 120 BPM, only places the beat lines.

Only .ghn, .ghc and .ghb hold counts this large: FNC1 (.bin) keeps 64-bit
counts, .raid stacks 65,535 notes an event at most, and .thr lists every
note.

Made with:
  godshand --max-notes andromeda-galaxy-GHN.ghn 36000 125000 120 "Andromeda Galaxy"
  godshand --max-notes hopeless-pixelified-GHN.ghn 142000 220000 120 "Hopeless (Pixelified)"
