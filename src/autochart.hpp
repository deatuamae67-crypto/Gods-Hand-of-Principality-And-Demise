// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// AutoChart: a playable FNF-style chart from a song's sounds.
//
// The song's sounds are tracks - its instrumental, its voices, any other
// stem, or the channels of its FL Studio project - and each says whose notes
// it gives (the player's, the opponent's, both in turns every four bars, or
// none) and how its sounds become lanes (by pitch, by drum, in turn, or an
// FL channel's keys as SNIFF reads them). The tempo, with its changes, is
// given or found from the audio.
//
// An audio track's notes: onsets from spectral flux, snapped to the beat
// grid of the difficulty, the strongest kept. With a trained model
// (AutoModel, issue #36), the notes are placed where the charts of the
// player's mods put them for sounds like these: the model learned from the
// instrumental and both voices of every charted song when a note comes and
// whose it is, which lane it takes and how many notes a second a song this
// loud gets.
//
// Every chart AutoChart makes is marked (Chart::autoCharted): it plays with
// the shame watermark and saves only as .ghac, which no other engine reads.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chart.hpp"
#include "flp_import.hpp"

namespace gh {

struct AutoChartInfo {
    double bpm = 0, firstBeatMs = 0;
    size_t onsets = 0;
    bool trained = false;  // the trained model placed the audio tracks' notes
};

enum class AutoRole : uint8_t { Ignore = 0, Player, Opponent, Turns };
// Keys: an FL Studio channel's notes as SNIFF reads them (C3-D#3 the
// player's lanes, C4-D#4 the opponent's; the role does not matter).
enum class AutoLanes : uint8_t { Pitch = 0, Drums, Cycle, Keys };
const char* autoRoleName(AutoRole r);
const char* autoLanesName(AutoLanes l);

// One sound of the song.
struct AutoTrack {
    std::string name;
    // Audio: 16-bit PCM, interleaved...
    const int16_t* samples = nullptr;
    size_t frames = 0;
    int channels = 2, sampleRate = 44100;
    // ...or an FL Studio channel's notes (times in ticks from the song's start).
    std::vector<FlpNote> flpNotes;
    int ppqn = 96;
    AutoRole role = AutoRole::Turns;
    AutoLanes lanes = AutoLanes::Pitch;
    float density = 1.0f;  // 0.25 - 4: fewer or more of its sounds become notes
    bool audio() const { return samples != nullptr && frames > 0; }
};

struct AutoModel;

struct AutoChartSpec {
    std::vector<AutoTrack> tracks;
    std::vector<TempoPoint> tempo;     // time (ms) and BPM of each change; empty: found from the audio
    int difficulty = 1;                // 0 easy, 1 normal, 2 hard, 3 insane
    const AutoModel* model = nullptr;  // trained: it places the audio tracks' notes
    uint64_t seed = 1;
};

bool autoChartSpec(const AutoChartSpec& spec, Chart& out, AutoChartInfo* info = nullptr, std::string* err = nullptr);

// One sound, the sides taking turns every four bars (or, with a trained
// model, the sides it learned). difficulty: 0 easy, 1 normal, 2 hard.
bool autoChart(const int16_t* samples, size_t frames, int channels, int sampleRate, int difficulty, Chart& out,
               AutoChartInfo* info = nullptr, const AutoModel* model = nullptr);

// The tempo of a sound: its BPM and the time of a beat (ms).
bool detectTempo(const int16_t* samples, size_t frames, int channels, int sampleRate, double& bpm, double& beatMs);

// ------------------------------------------------------------- training --
// What AutoChart learns from charted songs (optional; issue #36). The sound
// of each moment - the instrumental's, the player's voice's and the
// opponent's, each as onset strength in four bands and loudness - with where
// it falls in the beat: whether a note of each side comes then (logistic
// regression), its lane (softmax regression) and the notes a second for the
// song's loudness (least squares). Each song is learned once, and each run
// goes on from what was learned before: the more mods, the better it gets.
struct AutoModel {
    static constexpr int kFeatures = 24;
    using Weights = std::array<float, kFeatures + 1>;  // the last: the bias
    std::array<Weights, 2> side{};   // a note of the opponent (0) / the player (1) here
    std::array<Weights, 4> lane{};   // its lane
    std::array<Weights, 2> sideG{};  // AdaGrad's sums of squared gradients
    std::array<Weights, 4> laneG{};
    // Notes a second (both sides) against loudness, over four-bar windows.
    double sx = 0, sy = 0, sxx = 0, sxy = 0, sn = 0;
    uint64_t frames = 0, notes = 0;
    std::vector<std::string> songs;  // learned, by id

    bool trained() const { return notes > 0; }
    bool knows(const std::string& songId) const;
    double nps(double loudness) const;  // notes a second the charts give a stretch this loud
    bool save(const std::string& path, std::string* err = nullptr) const;
    bool load(const std::string& path, std::string* err = nullptr);
};

struct AutoSound {
    const int16_t* samples = nullptr;
    size_t frames = 0;
    int channels = 2, sampleRate = 44100;
    bool ok() const { return samples && frames > 0; }
};

// Learns from one charted song: its instrumental, its voices (the player's
// and the opponent's; one shared track counts as both; either may be
// missing) and its chart. Returns the frames with notes it learned from.
size_t autoLearnSong(AutoModel& m, const Chart& chart, const AutoSound& inst, const AutoSound& voiceP, const AutoSound& voiceO,
                     uint64_t seed = 1);

}  // namespace gh
