// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Finding songs in every mod layout (song_scan.cpp), PulseForge charts, audio
// lengths and assets shared between mods.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

#include "chart.hpp"
#include "chart_binary.hpp"
#include "chart_stream.hpp"
#include "count_charts.hpp"
#include "miniz.h"
#include "common.hpp"
#include "flp_import.hpp"
#include "json_merge.hpp"
#include "scene.hpp"
#include "song_scan.hpp"
#include "thr_chart.hpp"

using namespace gh;
namespace fs = std::filesystem;

extern int g_fail, g_pass;
#define SCHECK(cond)                                                             \
    do {                                                                         \
        if (cond) ++g_pass;                                                      \
        else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

namespace {

std::string str(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// A silent 8 kHz mono 8-bit WAV of the given length.
void putWav(const fs::path& p, double seconds) {
    const uint32_t rate = 8000, data = uint32_t(seconds * rate);
    std::string w = "RIFF";
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w += char((v >> (8 * i)) & 0xFF); };
    auto u16 = [&](uint16_t v) { w += char(v & 0xFF); w += char(v >> 8); };
    u32(36 + data);
    w += "WAVEfmt ";
    u32(16);
    u16(1);      // PCM
    u16(1);      // mono
    u32(rate);
    u32(rate);   // bytes per second
    u16(1);
    u16(8);
    w += "data";
    u32(data);
    w.append(data, char(0x80));
    put(p, w);
}

std::string legacyChart(const std::string& song, int notes) {
    std::string s = "{\"song\":{\"song\":\"" + song + "\",\"bpm\":120,\"speed\":2,\"notes\":[{\"mustHitSection\":true,\"sectionNotes\":[";
    for (int i = 0; i < notes; ++i) s += (i ? "," : "") + std::string("[") + std::to_string(500 * i) + "," + std::to_string(i % 4) + ",0]";
    return s + "]}]}}";
}

const SongEntry* byTitle(const SongScan& s, const std::string& title, int nth = 0) {
    for (const SongEntry& e : s.songs)
        if (equalsI(e.title, title) && nth-- == 0) return &e;
    return nullptr;
}

int countTitle(const SongScan& s, const std::string& title) {
    int n = 0;
    for (const SongEntry& e : s.songs) n += equalsI(e.title, title);
    return n;
}

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

const char* kPulseForgeEditor = R"({"format":"pulseforge-editor-chart","version":1,
 "metadata":{"title":"Forge Song","difficulty":"hard","stage":"street","player":"bf","opponent":"pico","girlfriend":"gf"},
 "keyCount":6,"scrollSpeed":2.5,
 "tempos":[{"id":1,"time":0,"bpm":150,"numerator":4,"denominator":4},{"id":2,"time":4000,"bpm":200}],
 "notes":[{"id":2,"time":0,"duration":0,"lane":5,"owner":"player","kind":"normal","payloadId":0},
          {"id":3,"time":100,"duration":400,"lane":1,"owner":"opponent","kind":"normal"},
          {"id":4,"time":200,"duration":0,"lane":2,"owner":"girlfriend","kind":"normal"},
          {"id":5,"time":300,"duration":0,"lane":3,"owner":"player","kind":"Hurt Note"},
          {"id":6,"time":300.4,"duration":0,"lane":3,"owner":"player","kind":"Hurt Note"},
          {"id":7,"time":5000,"duration":5,"lane":0,"owner":"player"}],
 "events":[{"time":1000,"name":"Hey!","value1":"bf","value2":"0.6"},[2000,[["Change Scroll Speed","1.5","0"]]]]})";

void testPulseForgeCharts() {
    Chart c;
    std::string err;
    SCHECK(parseChart(kPulseForgeEditor, "Hard", c, err));
    SCHECK(c.title == "Forge Song");
    SCHECK(c.keys == 6);
    SCHECK(std::fabs(c.speed - 2.5) < 1e-9);
    SCHECK(c.player == "bf" && c.opponent == "pico" && c.gf == "gf" && c.stage == "street");
    SCHECK(c.tempo.size() == 2 && std::fabs(c.bpmAt(4500) - 200) < 1e-9);
    SCHECK(c.notes.size() == 5);  // the two hurt notes 0.4 ms apart are one stacked note
    SCHECK(c.rawNoteCount == 6);
    SCHECK(c.playerNotes == 4 && c.opponentNotes == 2);
    if (c.notes.size() == 5) {
        SCHECK(c.notes[0].side == 1 && c.notes[0].lane == 5);
        SCHECK(c.notes[1].side == 0 && c.notes[1].lane == 1 && std::fabs(c.notes[1].length - 400) < 1e-9);
        SCHECK(c.notes[2].side == 0 && (c.notes[2].flags & kFlagGF));
        SCHECK(c.notes[3].kind == kMine && c.notes[3].stack == 2);
        SCHECK(c.notes[4].length == 0);  // shorter than a step: a tap
    }
    SCHECK(c.events.size() == 2 && c.events[0].name == "Hey!" && c.events[0].v2 == "0.6" &&
           c.events[1].name == "Change Scroll Speed" && c.events[1].v1 == "1.5");

    // Extra keys off: 4 lanes, as for every other chart.
    Chart four;
    SCHECK(parseChart(kPulseForgeEditor, "Hard", four, err, {}, false));
    SCHECK(four.keys == 4 && !four.notes.empty() && four.notes[0].lane == 1);

    // The exported format keeps the scene in "song".
    const char* exported = R"({"format":"pulseforge-chart","version":1,
      "song":{"title":"Fantrack","difficulty":"expert","keyCount":4,"scrollSpeed":1},
      "audio":{"instrumental":"Inst.mp3","vocals":[]},
      "tempos":[{"time":0,"bpm":193.9655,"numerator":4,"denominator":4}],
      "notes":[{"time":725.3,"duration":0,"lane":2,"owner":"player","kind":"normal"},
               {"time":826.6,"duration":0,"lane":1,"owner":"opponent","kind":"normal"}],"events":[]})";
    Chart e;
    SCHECK(parseChart(exported, "Expert", e, err));
    SCHECK(e.title == "Fantrack" && e.keys == 4 && e.notes.size() == 2 && e.playerNotes == 1);

    // Big files are streamed: the same chart read either way.
    const fs::path f = fs::temp_directory_path() / "gh_pulseforge_stream.pfchart.json";
    put(f, kPulseForgeEditor);
    Chart streamed;
    SCHECK(loadChartFile(str(f), "Hard", streamed, err, {}, 1));
    SCHECK(streamed.notes.size() == c.notes.size() && streamed.rawNoteCount == 6 && streamed.keys == 6);
    SCHECK(streamed.playerNotes == 4 && streamed.opponentNotes == 2 && streamed.events.size() == 2);
    if (streamed.notes.size() == 5) {
        SCHECK(streamed.notes[2].side == 0 && (streamed.notes[2].flags & kFlagGF));
        SCHECK(streamed.notes[3].kind == kMine && streamed.notes[3].stack == 2);
    }
    const ChartPeek pk = peekChart(str(f));
    SCHECK(pk.pulseForge && pk.isChart && pk.song == "Forge Song" && pk.difficulty == "hard");
    fs::remove(f);
}

void testAudioSeconds() {
    const fs::path dir = fs::temp_directory_path() / "gh_audio_len";
    fs::remove_all(dir);
    putWav(dir / "a.wav", 3.5);
    SCHECK(std::fabs(audioSeconds(str(dir / "a.wav")) - 3.5) < 0.01);

    // Ogg Vorbis: the rate from the identification header, the length from the last page.
    std::string ogg = "OggS";
    ogg.append(1, '\0');
    ogg.append(1, '\2');
    ogg.append(8, '\0');
    ogg.append(12, '\0');
    ogg += char(1);
    ogg += char(30);
    ogg += "\x01vorbis";
    ogg.append(4, '\0');
    ogg += char(2);
    const uint32_t rate = 44100;
    for (int i = 0; i < 4; ++i) ogg += char((rate >> (8 * i)) & 0xFF);
    ogg.append(2000, 'x');
    ogg += "OggS";
    ogg.append(1, '\0');
    ogg.append(1, '\4');
    const uint64_t granule = uint64_t(rate) * 95;
    for (int i = 0; i < 8; ++i) ogg += char((granule >> (8 * i)) & 0xFF);
    ogg.append(16, '\0');
    put(dir / "b.ogg", ogg);
    SCHECK(std::fabs(audioSeconds(str(dir / "b.ogg")) - 95) < 0.01);

    // MP3: 128 kbps frames after an ID3 tag.
    std::string mp3 = "ID3";
    mp3 += char(4);
    mp3.append(2, '\0');
    mp3 += std::string("\0\0\0\x0A", 4);  // tag of 10 bytes
    mp3.append(10, '\0');
    mp3 += "\xFF\xFB\x90\x00";
    mp3.append(1600000 - mp3.size(), '\0');
    put(dir / "c.mp3", mp3);
    const double secs = audioSeconds(str(dir / "c.mp3"));
    SCHECK(secs > 99 && secs < 101);
    fs::remove_all(dir);
}

void testModLayouts() {
    const fs::path base = fs::temp_directory_path() / "gh_scan_layouts";
    fs::remove_all(base);
    const fs::path mods = base / "mods";

    // A Psych Engine build: charts in assets/preload/data/<song>/ and
    // assets/shared/data/<song>/, audio in assets/songs/<song>/.
    const fs::path game = mods / "packA" / "Game Build";
    const fs::path assets = game / "assets";
    put(assets / "songs" / "foo" / "Inst.ogg", "x");
    put(assets / "songs" / "foo" / "Voices.ogg", "x");
    put(assets / "preload" / "data" / "foo" / "foo.json", legacyChart("Foo", 8));
    put(assets / "preload" / "data" / "foo" / "foo-hard.json", legacyChart("Foo", 12));
    put(assets / "preload" / "data" / "foo" / "dialogue.json", R"({"dialogue":[{"portrait":"bf","text":"hi","speed":0.05}]})");
    put(assets / "songs" / "qux" / "Inst.ogg", "x");
    put(assets / "shared" / "data" / "qux" / "qux.json", legacyChart("Qux", 4));
    put(assets / "shared" / "images" / "stageback.png", "png");
    put(assets / "shared" / "data" / "characters" / "bf.json", R"({"animations":[],"image":"characters/BOYFRIEND","scale":1})");
    // data/charts/<song>/ and a chart straight in data/
    put(assets / "songs" / "bar" / "Inst.ogg", "x");
    put(assets / "data" / "charts" / "bar" / "bar.json", legacyChart("Bar", 5));
    put(assets / "data" / "loose-song.json", legacyChart("Loose Song", 6));
    // The build's mods/ folder holds charts of its own and a mod folder.
    put(game / "mods" / "data" / "global" / "global.json", legacyChart("Global", 3));
    put(game / "mods" / "songs" / "global" / "Inst.ogg", "x");
    put(game / "mods" / "MyMod" / "data" / "baz" / "baz.json", legacyChart("Baz", 7));
    put(game / "mods" / "MyMod" / "songs" / "baz" / "Inst.ogg", "x");
    put(game / "mods" / "MyMod" / "characters" / "pico.json", R"({"animations":[]})");
    // An exported copy of the build deep inside: the same chart is offered once.
    const fs::path exportAssets = game / "export" / "release" / "windows" / "bin" / "assets";
    put(exportAssets / "data" / "foo" / "foo.json", legacyChart("Foo", 8));
    put(exportAssets / "songs" / "foo" / "Inst.ogg", "x");
    // ...and a second, different version of a song: both are offered, with their folder.
    put(exportAssets / "data" / "qux" / "qux.json", legacyChart("Qux", 9));
    put(exportAssets / "songs" / "qux" / "Inst.ogg", "x");

    // A PulseForge chart with its audio; editor projects and autosaves.
    const fs::path packB = mods / "packB";
    put(packB / "chart.json", R"({"format":"pulseforge-chart","version":1,"song":{"title":"Fantrack","difficulty":"expert","keyCount":4},
        "tempos":[{"time":0,"bpm":120}],"notes":[{"time":0,"duration":0,"lane":1,"owner":"player","kind":"normal"}],"events":[]})");
    put(packB / "Inst.mp3", "x");
    put(packB / "autosaves" / "victim-easy.autosave.pfchart.json", kPulseForgeEditor);
    put(packB / "autosaves" / "untitled.autosave.pfchart.json",
        R"({"format":"pulseforge-editor-chart","version":1,"metadata":{"title":"Empty"},"keyCount":4,"notes":[],"events":[]})");
    put(packB / "musical-conversions" / "template.pfm.json", R"({"format":"pulseforge-pfm-source-v1","notes":[],"scrollSpeed":1})");
    put(packB / "images" / "icons" / "icon-pico.png", "png");
    // A recovered chart with only events: nothing to play, no song.
    put(packB / "charts" / "_unknown" / "unknown-1.json",
        R"({"song":{"song":"Mystery","bpm":150,"events":[[100,[["Hey!","",""]]]],"notes":[{"mustHitSection":true,"sectionNotes":[]}]}})");
    // A lone section recovered from a bigger chart plays as a chart.
    put(packB / "charts" / "_fragments" / "frag-1.json",
        R"({"lengthInSteps":16,"mustHitSection":false,"sectionNotes":[[100,0,0],[200,5,0]]})");
    // Title screen and freeplay files in data/ are no songs.
    put(assets / "data" / "title" / "offsets.json", R"({"bpm":102,"song":"freakyMenu","x":0})");

    // Songs without charts: AutoChart for songs, not for sound effects.
    const fs::path packC = mods / "packC" / "loose songs";
    putWav(packC / "Long Song_Inst.wav", 31);
    putWav(packC / "Long Song_Voices.wav", 31);
    putWav(packC / "whoosh.wav", 2);
    putWav(packC / "Inst.wav", 40);  // named after its folder

    SongScan s;
    scanModsDir(str(mods), s);
    if (std::getenv("GH_SCAN_DEBUG"))
        for (const SongEntry& e : s.songs) {
            std::printf("%s | %s | %s | inst %s\n", e.id.c_str(), e.title.c_str(), e.subtitle.c_str(), e.inst.c_str());
            for (size_t i = 0; i < e.diffs.size(); ++i) std::printf("   [%s] %s\n", e.diffs[i].c_str(), e.chartPaths[i].c_str());
        }

    const SongEntry* foo = byTitle(s, "Foo");
    SCHECK(foo && !foo->autoChart && foo->diffs.size() == 2);
    SCHECK(countTitle(s, "Foo") == 1);
    if (foo) {
        SCHECK(foo->chartPaths.size() == 2 && foo->chartPaths[0].find("preload") != std::string::npos);
        SCHECK(endsWith(foo->inst, "assets/songs/foo/Inst.ogg") && endsWith(foo->voices, "Voices.ogg"));
        SCHECK(foo->pack == "packA");
        for (const std::string& d : foo->diffs) SCHECK(!equalsI(d, "Dialogue"));
        bool hasShared = false;
        for (const std::string& r : foo->assetRoots) hasShared |= endsWith(r, "assets/shared/");
        SCHECK(hasShared);
    }
    SCHECK(countTitle(s, "Qux") == 2);
    const SongEntry* qux = byTitle(s, "Qux");
    SCHECK(qux && !qux->autoChart && endsWith(qux->inst, "Inst.ogg"));
    if (const SongEntry* q2 = byTitle(s, "Qux", 1)) SCHECK(q2->subtitle != qux->subtitle);
    SCHECK(!byTitle(s, "Characters"));
    const SongEntry* bar = byTitle(s, "Bar");
    SCHECK(bar && !bar->autoChart && endsWith(bar->inst, "assets/songs/bar/Inst.ogg"));
    const SongEntry* loose = byTitle(s, "Loose Song");
    SCHECK(loose && loose->chartPaths.size() == 1 && endsWith(loose->modRoot, "assets/"));
    SCHECK(byTitle(s, "Global") && byTitle(s, "Baz"));
    if (const SongEntry* baz = byTitle(s, "Baz")) SCHECK(endsWith(baz->inst, "MyMod/songs/baz/Inst.ogg"));

    const SongEntry* fan = byTitle(s, "Fantrack");
    SCHECK(fan && fan->diffs.size() == 1 && fan->diffs[0] == "Expert" && endsWith(fan->inst, "packB/Inst.mp3"));
    const SongEntry* victim = byTitle(s, "Forge Song");
    SCHECK(victim && victim->diffs.size() == 1 && victim->diffs[0] == "Hard (autosave)");  // the chart's own difficulty
    SCHECK(!byTitle(s, "Empty") && !byTitle(s, "Template"));
    SCHECK(!byTitle(s, "Mystery") && !byTitle(s, "Title") && !byTitle(s, "Offsets"));
    const SongEntry* frag = byTitle(s, "Frag");
    SCHECK(frag && frag->chartPaths.size() == 1);
    if (frag && !frag->chartPaths.empty()) {
        Chart fc;
        std::string ferr;
        SCHECK(loadChartFile(frag->chartPaths[0], "Normal", fc, ferr));
        SCHECK(fc.notes.size() == 2 && fc.opponentNotes == 1 && fc.playerNotes == 1);
    }

    const SongEntry* longSong = byTitle(s, "Long Song");
    SCHECK(longSong && longSong->autoChart && endsWith(longSong->inst, "Long Song_Inst.wav") &&
           endsWith(longSong->voices, "Long Song_Voices.wav"));
    SCHECK(byTitle(s, "Loose Songs") && byTitle(s, "Loose Songs")->autoChart);
    SCHECK(!byTitle(s, "Whoosh"));

    std::vector<std::string> ids;
    for (const SongEntry& e : s.songs) ids.push_back(e.id);
    std::sort(ids.begin(), ids.end());
    SCHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

    // Asset folders: the song's own mod first, then the other mods.
    SCHECK(s.packRoots.count("packA") && s.packRoots.count("packB"));
    if (fan) {
        const std::vector<std::string> shared = sharedRootsFor(s, *fan);
        SCHECK(!shared.empty() && endsWith(shared.front(), "packB/"));
        bool packA = false;
        for (const std::string& r : shared) packA |= endsWith(r, "MyMod/") || endsWith(r, "assets/shared/");
        SCHECK(packA);
    }

    // The song's own folders first; another mod's files only through find().
    scene::Files files;
    files.setRoots({str(packB) + "/"});
    files.setSharedRoots(fan ? sharedRootsFor(s, *fan) : std::vector<std::string>{});
    SCHECK(endsWith(files.find("images/icons/icon-pico.png"), "packB/images/icons/icon-pico.png"));
    SCHECK(endsWith(files.find("characters/pico.json"), "MyMod/characters/pico.json"));
    SCHECK(endsWith(files.find("images/stageback.png"), "assets/shared/images/stageback.png"));
    SCHECK(files.find("characters/nobody.json").empty());
    SCHECK(files.list("characters", ".json").empty());  // scripts of other mods never run

    // A disabled mod is left out.
    put(mods / "modsList.txt", "packC|0\n");
    SongScan off;
    scanModsDir(str(mods), off);
    SCHECK(!byTitle(off, "Long Song") && byTitle(off, "Foo"));
    fs::remove_all(base);
}

// A small FL Studio project: an FPC channel with a chart in pattern 1
// ("hard"), one note in pattern 2 ("easy"), a melody in pattern 3, and a
// second channel whose notes are not the chart's.
std::string makeFlp() {
    std::string ev;
    auto byte = [&](uint8_t id, uint8_t v) { ev += char(id); ev += char(v); };
    auto word = [&](uint8_t id, uint16_t v) { ev += char(id); ev += char(v & 255); ev += char(v >> 8); };
    auto dword = [&](uint8_t id, uint32_t v) {
        ev += char(id);
        for (int i = 0; i < 4; ++i) ev += char((v >> (8 * i)) & 255);
    };
    auto array = [&](uint8_t id, const std::string& data) {
        ev += char(id);
        size_t n = data.size();
        do {
            uint8_t c = uint8_t(n & 0x7F);
            n >>= 7;
            if (n) c |= 0x80;
            ev += char(c);
        } while (n);
        ev += data;
    };
    auto utf16 = [](const std::string& a) {
        std::string w;
        for (char c : a) { w += c; w += char(0); }
        return w + std::string(2, '\0');
    };
    struct N { uint32_t t; uint16_t ch; uint32_t dur, pitch; uint8_t vel; };
    auto notes = [](const std::vector<N>& v) {
        std::string d;
        for (const N& n : v) {
            std::string r(24, '\0');
            for (int i = 0; i < 4; ++i) r[size_t(i)] = char((n.t >> (8 * i)) & 255);
            r[6] = char(n.ch & 255);
            for (int i = 0; i < 4; ++i) r[size_t(8 + i)] = char((n.dur >> (8 * i)) & 255);
            for (int i = 0; i < 4; ++i) r[size_t(12 + i)] = char((n.pitch >> (8 * i)) & 255);
            r[21] = char(n.vel);
            d += r;
        }
        return d;
    };
    array(0xC7, "20.8.4.2576");
    dword(0x9C, 150000);  // 150 BPM
    array(0xC2, utf16("Forge Test"));
    word(0x40, 0);
    array(0xC9, utf16("FPC"));
    word(0x40, 1);
    array(0xC9, utf16("3x Osc"));
    byte(0x1C, 3);
    word(0x41, 1);
    array(0xC1, utf16("hard"));
    word(0x41, 2);
    array(0xC1, utf16("easy"));
    word(0x41, 1);
    array(0xE0, notes({{0, 0, 24, 54, 100},     // camera on the opponent
                       {0, 0, 24, 60, 100},     // opponent left
                       {96, 0, 24, 61, 100},    // opponent down, one beat in
                       {384, 0, 24, 53, 100},   // section 1: camera on the player
                       {384, 0, 96, 48, 100},   // player left, held a beat
                       {480, 0, 48, 51, 50},    // player right, soft: a hold
                       {768, 0, 24, 58, 100},   // section 2: the player's alt animation
                       {768, 0, 24, 49, 100},   // player down
                       {100, 1, 24, 60, 100}}));  // another channel: not the chart
    word(0x41, 2);
    array(0xE0, notes({{0, 0, 24, 50, 100}}));
    word(0x41, 3);
    array(0xE0, notes({{0, 0, 96, 65, 100}, {96, 0, 96, 67, 100}, {192, 0, 96, 69, 100}, {288, 0, 96, 60, 100},
                       {384, 0, 96, 72, 100}, {480, 0, 96, 71, 100}, {576, 0, 96, 64, 100}}));
    std::string f = "FLhd";
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f += char((v >> (8 * i)) & 255); };
    u32(6);
    f += std::string("\0\0\x02\0", 4);
    f += char(96);
    f += char(0);
    f += "FLdt";
    u32(uint32_t(ev.size()));
    return f + ev;
}

// A song arranged in FL Studio: a sampler channel (its file names it), a
// synth the user renamed, and pattern 1 placed twice on the playlist - the
// second clip cut to its ticks 96 to 288.
std::string makeFlpSong(size_t clipSize) {
    std::string ev;
    auto word = [&](uint8_t id, uint16_t v) { ev += char(id); ev += char(v & 255); ev += char(v >> 8); };
    auto dword = [&](uint8_t id, uint32_t v) {
        ev += char(id);
        for (int i = 0; i < 4; ++i) ev += char((v >> (8 * i)) & 255);
    };
    auto array = [&](uint8_t id, const std::string& data) {
        ev += char(id);
        size_t n = data.size();
        do {
            uint8_t c = uint8_t(n & 0x7F);
            n >>= 7;
            if (n) c |= 0x80;
            ev += char(c);
        } while (n);
        ev += data;
    };
    auto utf16 = [](const std::string& a) {
        std::string w;
        for (char c : a) { w += c; w += char(0); }
        return w + std::string(2, '\0');
    };
    auto le = [](uint32_t v, int n) {
        std::string r;
        for (int i = 0; i < n; ++i) r += char((v >> (8 * i)) & 255);
        return r;
    };
    array(0xC7, clipSize == 60 ? "21.2.3.4004" : "20.8.4.2576");
    dword(0x9C, 120000);
    word(0x40, 0);
    array(0xC4, utf16("C:\\Samples\\Kick 01.wav"));
    word(0x40, 1);
    array(0xC9, utf16("3x Osc"));
    array(0xCB, utf16("Lead"));
    word(0x41, 1);
    std::string notes;
    auto note = [&](uint32_t t, uint16_t ch, uint32_t pitch) {
        std::string r(24, '\0');
        r.replace(0, 4, le(t, 4));
        r.replace(6, 2, le(ch, 2));
        r.replace(8, 4, le(24, 4));
        r.replace(12, 4, le(pitch, 4));
        r[21] = char(100);
        notes += r;
    };
    note(0, 0, 60);
    note(96, 1, 72);
    note(192, 0, 60);
    note(288, 1, 74);
    array(0xE0, notes);
    std::string clips;
    auto clip = [&](uint32_t pos, uint32_t len, float start) {
        std::string c(clipSize, '\0');
        c.replace(0, 4, le(pos, 4));
        c.replace(4, 2, le(20480, 2));
        c.replace(6, 2, le(20480 + 1, 2));
        c.replace(8, 4, le(len, 4));
        c.replace(12, 2, le(499, 2));
        uint32_t bits;
        std::memcpy(&bits, &start, 4);
        c.replace(24, 4, le(bits, 4));
        clips += c;
    };
    clip(0, 384, 0);
    clip(768, 192, 96);
    array(0xE9, clips);
    word(0x40, 2);  // after the mixer: not a channel
    std::string f = "FLhd";
    f += le(6, 4) + std::string("\0\0\x02\0", 4) + char(96) + char(0) + "FLdt" + le(uint32_t(ev.size()), 4);
    return f + ev;
}

void testFlpSong() {
    for (size_t size : {size_t(32), size_t(60)}) {
        FlpProject p;
        std::string err;
        SCHECK(readFlp(makeFlpSong(size), p, err));
        SCHECK(p.channels.size() >= 2 && p.channels[0].name == "Kick 01.wav" && p.channels[1].name == "Lead");
        SCHECK(p.playlist.size() == 2 && p.playlist[1].position == 768 && p.playlist[1].start == 96 && p.playlist[1].pattern == 1);
        const std::vector<FlpNote> kick = flpSongNotes(p, 0), lead = flpSongNotes(p, 1);
        // Kick: 0 and 192 (first clip), 192 -> 768 + 96 (second clip, from tick 96).
        SCHECK(kick.size() == 3 && kick[0].time == 0 && kick[1].time == 192 && kick[2].time == 864);
        // Lead: 96 and 288 (first clip), 96 -> 768 (second); 288 is past the second clip.
        SCHECK(lead.size() == 3 && lead[0].time == 96 && lead[1].time == 288 && lead[2].time == 768 && lead[2].pitch == 72);
        SCHECK(flpSongNotes(p, -1).size() == 6);
    }
}

void testFlStudio() {
    FlpProject p;
    std::string err;
    SCHECK(readFlp(makeFlp(), p, err));
    SCHECK(p.ppqn == 96 && std::fabs(p.bpm - 150) < 1e-9 && p.title == "Forge Test" && p.chartChannel == 0);
    SCHECK(p.patterns.size() == 3 && p.patterns[0].name == "hard" && p.patterns[1].name == "easy");
    SCHECK(flpChartPatterns(p, true) == std::vector<int>({1, 2}));         // the melody is no chart
    SCHECK(flpChartPatterns(p, false) == std::vector<int>({1, 2, 3}));
    SCHECK(flpPatternLabel(p, 1) == "Hard" && flpPatternLabel(p, 3) == "Pattern 3");

    Chart c;
    SCHECK(flpToChart(p, 1, c, err));
    SCHECK(c.keys == 4 && c.notes.size() == 5 && c.playerNotes == 3 && c.opponentNotes == 2);
    if (c.notes.size() == 5) {
        // 96 ticks per beat at 150 BPM: 400 ms a beat.
        SCHECK(c.notes[0].side == 0 && c.notes[0].lane == 0 && std::fabs(c.notes[0].time) < 1e-6 && c.notes[0].length == 0);
        SCHECK(c.notes[1].side == 0 && c.notes[1].lane == 1 && std::fabs(c.notes[1].time - 400) < 1e-6);
        SCHECK(c.notes[2].side == 1 && c.notes[2].lane == 0 && std::fabs(c.notes[2].time - 1600) < 1e-6 &&
               std::fabs(c.notes[2].length - 300) < 1e-6);  // (96 - 24) ticks
        SCHECK(c.notes[3].side == 1 && c.notes[3].lane == 3 && std::fabs(c.notes[3].length - 100) < 1e-6);  // soft: (48 - 24) ticks
        SCHECK(c.notes[4].side == 1 && c.notes[4].lane == 1 && (c.notes[4].flags & kFlagAlt) && std::fabs(c.notes[4].time - 3200) < 1e-6);
    }
    SCHECK(c.sections.size() == 3 && !c.sections[0].mustHit && c.sections[1].mustHit && c.sections[2].mustHit);
    SCHECK(c.focusAt(100) == 0 && c.focusAt(2000) == 1);
    SCHECK(std::fabs(c.bpmAt(0) - 150) < 1e-9 && c.title == "Forge Test");

    std::string file;
    int pat = 0;
    SCHECK(flpChartPath("a b/song.flp#2", file, pat) && file == "a b/song.flp" && pat == 2);
    SCHECK(flpChartPath("song.FSC", file, pat) && pat == -1);
    SCHECK(!flpChartPath("song.json", file, pat));

    // Through the chart loader and the song scan.
    const fs::path base = fs::temp_directory_path() / "gh_flp_scan";
    fs::remove_all(base);
    put(base / "mods" / "flpack" / "Forge Test.flp", makeFlp());
    Chart loaded;
    SCHECK(loadChartFile(str(base / "mods" / "flpack" / "Forge Test.flp") + "#2", "Easy", loaded, err) && loaded.notes.size() == 1);
    SongScan s;
    scanModsDir(str(base / "mods"), s);
    const SongEntry* e = byTitle(s, "Forge Test");
    SCHECK(e && e->diffs.size() == 2 && e->diffs[0] == "Easy (FL Studio)" && e->diffs[1] == "Hard (FL Studio)");
    fs::remove_all(base);
}

}  // namespace

// gh_tests --flp a.flp ...: what an FL Studio file holds, as SNIFF sees it.
int printFlp(int argc, char** argv, int first) {
    for (int i = first; i < argc; ++i) {
        FlpProject p;
        std::string err;
        if (!readFlpFile(argv[i], p, err)) {
            std::printf("%s: %s\n", argv[i], err.c_str());
            continue;
        }
        std::printf("%s: ppqn %d, bpm %.3f, title '%s', chart channel %d, %zu patterns\n", argv[i], p.ppqn, p.bpm,
                    p.title.c_str(), p.chartChannel, p.patterns.size());
        for (const FlpPattern& pat : p.patterns) {
            size_t chart = 0, onChannel = 0;
            for (const FlpNote& n : pat.notes) {
                onChannel += n.channel == p.chartChannel;
                chart += n.channel == p.chartChannel && ((n.pitch >= 48 && n.pitch <= 51) || (n.pitch >= 60 && n.pitch <= 63));
            }
            std::printf("  pattern %d '%s': %zu notes, %zu on the chart channel, %zu in the chart lanes\n", pat.number,
                        pat.name.c_str(), pat.notes.size(), onChannel, chart);
        }
        Chart c;
        if (flpToChart(p, -1, c, err)) std::printf("  chart: %zu notes, %.1f s\n", c.notes.size(), c.lengthMs / 1000.0);
        else std::printf("  no chart: %s\n", err.c_str());
    }
    return 0;
}

// Binary charts (.ghc) in a mod: one kept alone, without its JSON (a chart
// too big to keep as JSON), is a chart like any other - not an audio-only
// song for AutoChart; one beside its JSON is offered once, as the .ghc.
void testBinaryChartsInMods() {
    const fs::path base = fs::temp_directory_path() / "gh_scan_ghc";
    fs::remove_all(base);
    const fs::path mod = base / "mods" / "ghcpack";
    put(mod / "songs" / "alone" / "Inst.ogg", "x");
    put(mod / "songs" / "paired" / "Inst.ogg", "x");
    put(base / "alone.json", legacyChart("Alone", 8));
    put(mod / "data" / "paired" / "paired.json", legacyChart("Paired", 8));
    put(mod / "data" / "paired" / "paired-hard.json", legacyChart("Paired", 12));
    std::string err;
    fs::create_directories(mod / "data" / "alone");
    SCHECK(convertChartToBinary(str(base / "alone.json"), str(mod / "data" / "alone" / "alone.ghc"), err));
    SCHECK(convertChartToBinary(str(mod / "data" / "paired" / "paired.json"), str(mod / "data" / "paired" / "paired.ghc"), err));
    SongScan s;
    scanModsDir(str(base / "mods"), s);
    const SongEntry* alone = nullptr;
    const SongEntry* paired = nullptr;
    for (const SongEntry& e : s.songs) {
        if (e.songKey == "alone") alone = &e;
        if (e.songKey == "paired") paired = &e;
    }
    SCHECK(alone && !alone->autoChart && alone->diffs.size() == 1 && alone->diffs[0] == "Normal" &&
           endsWithI(alone->chartPaths[0], ".ghc"));
    SCHECK(paired && paired->diffs.size() == 2 && paired->diffs[0] == "Normal" && endsWithI(paired->chartPaths[0], ".ghc") &&
           paired->diffs[1] == "Hard" && endsWithI(paired->chartPaths[1], "paired-hard.json"));
    if (alone && !alone->chartPaths.empty()) {
        Chart c;
        SCHECK(loadChartFile(alone->chartPaths[0], "Normal", c, err) && c.title == "Alone" && !c.notes.empty());
    }
    fs::remove_all(base);
}

// The iutk engine's charts (FNC1, issue #36), written here as its
// ChartBinary.hx reads them: version 2 (one note each), 3 (cells with 64-bit
// counts) and 4 (run-length varint streams with CRC-32s).
namespace iutk {
std::string le32(uint32_t v) { return std::string{char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; }
std::string varint(uint64_t v) {
    std::string o;
    do {
        const unsigned char b = v & 0x7F;
        v >>= 7;
        o += char(v ? (b | 0x80) : b);
    } while (v);
    return o;
}
// A stream of run-length items: runs of equal values as repeats, the rest literal.
std::string runs(const std::vector<uint64_t>& vals, bool bytes) {
    std::string o;
    auto val = [&](uint64_t v) { o += bytes ? std::string(1, char(v)) : varint(v); };
    size_t i = 0;
    while (i < vals.size()) {
        size_t j = i + 1;
        while (j < vals.size() && vals[j] == vals[i]) ++j;
        if (j - i >= 2) {
            o += varint(((j - i - 2) << 1) | 1);  // one value, (n >> 1) + 2 times
            val(vals[i]);
            i = j;
            continue;
        }
        size_t k = i + 1;  // literal values up to the next repeat
        while (k < vals.size() && !(k + 1 < vals.size() && vals[k + 1] == vals[k])) ++k;
        o += varint((k - i - 1) << 1);  // (n >> 1) + 1 values
        for (size_t m = i; m < k; ++m) val(vals[m]);
        i = k;
    }
    return o;
}
struct Cell {
    int32_t time;
    int lane;
    uint64_t count;
};
std::string v4(const std::vector<Cell>& cells, uint64_t declared) {
    std::vector<uint64_t> d, m, c;
    size_t groups = 0;
    uint64_t prevT = 0, firstT = 0;
    for (size_t i = 0; i < cells.size();) {
        const uint64_t t = uint64_t(int64_t(cells[i].time) + 2147483648LL);
        if (groups == 0) firstT = t;
        else d.push_back(t - prevT - 1);
        prevT = t;
        unsigned mask = 0;
        size_t j = i;
        for (; j < cells.size() && cells[j].time == cells[i].time; ++j) {
            mask |= 1u << cells[j].lane;
            c.push_back(cells[j].count - 1);
        }
        m.push_back(mask);
        ++groups;
        i = j;
    }
    const std::string sd = runs(d, false), sm = runs(m, true), sc = runs(c, false);
    auto crc = [](const std::string& x) {
        return uint32_t(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(x.data()), x.size()));
    };
    return std::string("FNC1") + char(4) + varint(cells.size()) + varint(groups) + varint(declared) + varint(firstT) +
           varint(sd.size()) + varint(sm.size()) + varint(sc.size()) + le32(crc(sd)) + le32(crc(sm)) + le32(crc(sc)) + sd + sm + sc;
}
}  // namespace iutk

void testIutkCharts() {
    const fs::path base = fs::temp_directory_path() / "gh_iutk";
    fs::remove_all(base);
    fs::create_directories(base);
    std::string err;
    // Version 2: one note each; the same time and lane twice is one note of two.
    {
        std::string f = "FNC1";
        f += char(2);
        f += iutk::le32(4);
        for (int32_t t : {500, 500, 750, 1000}) f += iutk::le32(uint32_t(t));
        f += std::string{char(4), char(4), char(1), char(7)};
        put(base / "v2.bin", f);
        Chart c;
        SCHECK(isCountChart(str(base / "v2.bin")) && loadChartFile(str(base / "v2.bin"), "Normal", c, err));
        SCHECK(c.notes.size() == 3 && c.playerNotes == Count(3) && c.opponentNotes == Count(1) && c.rawNoteCount == Count(4));
        SCHECK(c.notes.size() == 3 && c.notes[0].time == 500 && c.notes[0].side == 1 && c.notes[0].lane == 0 &&
               c.countOf(c.notes[0]) == Count(2));
        SCHECK(c.notes.size() == 3 && c.notes[1].side == 0 && c.notes[1].lane == 1 && c.notes[2].lane == 3 && c.keys == 4 &&
               c.bpmAt(0) == 120);
    }
    // Version 3 (what iutk's converter writes), through the game's writer:
    // 64-bit counts, a stack of ten billion notes among them.
    Chart src;
    src.keys = 4;
    auto addNote = [&](double t, int lane, int side, uint64_t count) {
        Note n;
        n.time = t;
        n.lane = uint16_t(lane);
        n.side = uint8_t(side);
        src.notes.push_back(n);
        src.setCount(src.notes.back(), Count(count));
    };
    addNote(0, 0, 0, 1);
    addNote(0, 2, 1, 10000000000ull);
    addNote(250, 3, 1, 7);
    addNote(-40, 1, 0, 3);  // before the song starts: times are signed
    src.finalize(120);
    SCHECK(writeFnc1(str(base / "Chart.bin"), src, err));
    Chart v3;
    SCHECK(loadChartFile(str(base / "Chart.bin"), "Normal", v3, err));
    SCHECK(v3.notes.size() == 4 && v3.playerNotes == Count(10000000007ull) && v3.opponentNotes == Count(4));
    SCHECK(v3.notes.size() == 4 && v3.notes[0].time == -40 && v3.countOf(v3.notes[2]) == Count(10000000000ull) &&
           v3.notes[2].lane == 2);
    // A total that does not add up is refused.
    {
        std::ifstream in(base / "Chart.bin", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bytes[9] = char(bytes[9] + 1);  // the total's low byte
        put(base / "bad3.bin", bytes);
        Chart bad;
        SCHECK(!loadChartFile(str(base / "bad3.bin"), "Normal", bad, err) && err.find("notes") != std::string::npos);
    }
    // Version 4: streams with runs (repeats in M and C), times far apart, a
    // count near 2^64.
    const std::vector<iutk::Cell> cells = {{-100, 1, 1}, {0, 4, 1}, {0, 5, 1}, {0, 7, 2}, {1000, 4, 1}, {1000, 5, 1},
                                           {2000, 6, 18446744073709551000ull}, {2001, 0, 5}};
    uint64_t total = 0;
    for (const auto& cl : cells) total += cl.count;
    put(base / "v4.bin", iutk::v4(cells, total));
    Chart c4;
    SCHECK(loadChartFile(str(base / "v4.bin"), "Normal", c4, err));
    SCHECK(c4.notes.size() == cells.size() && c4.rawNoteCount == Count(total));
    if (c4.notes.size() == cells.size()) {
        SCHECK(c4.notes[0].time == -100 && c4.notes[0].side == 0 && c4.notes[0].lane == 1);
        SCHECK(c4.countOf(c4.notes[6]) == Count(18446744073709551000ull) && c4.notes[6].side == 1 && c4.notes[6].lane == 2);
        SCHECK(c4.notes[7].time == 2001 && c4.countOf(c4.notes[7]) == Count(5));
    }
    // A damaged stream fails its CRC-32.
    {
        std::string f = iutk::v4(cells, total);
        f[f.size() - 1] = char(f[f.size() - 1] ^ 0x01);
        put(base / "bad4.bin", f);
        Chart bad;
        SCHECK(!loadChartFile(str(base / "bad4.bin"), "Normal", bad, err));
    }
    // In a mod: iutk's Chart.bin beside the song's audio is the song's chart.
    const fs::path mod = base / "mods" / "iutkpack";
    put(mod / "songs" / "spam" / "Inst.ogg", "x");
    fs::create_directories(mod / "data" / "spam");
    fs::copy_file(base / "v4.bin", mod / "data" / "spam" / "Chart.bin");
    SongScan s;
    scanModsDir(str(base / "mods"), s);
    const SongEntry* spam = nullptr;
    for (const SongEntry& e : s.songs)
        if (e.songKey == "spam") spam = &e;
    SCHECK(spam && !spam->autoChart && spam->diffs.size() == 1 && spam->diffs[0] == "Normal" &&
           endsWithI(spam->chartPaths[0], "Chart.bin"));
    fs::remove_all(base);
}

// R-Slice's .raid (issue #36), written here op by op as its files are laid
// out: a literal run, a pattern repeated with its lanes moved, a burst and a
// clipped group, in a deflated version-2 chunk, beside a deflated meta.
namespace raidw {
struct Buf {
    std::string b;
    void u8(uint32_t v) { b += char(v); }
    void u32(uint32_t v) { b += iutk::le32(v); }
    void u64(uint64_t v) {
        u32(uint32_t(v));
        u32(uint32_t(v >> 32));
    }
    void var(uint64_t v) { b += iutk::varint(v); }
    void zig(int64_t v) { var((uint64_t(v) << 1) ^ uint64_t(v >> 63)); }
};
struct Ev {
    int64_t us;
    uint32_t lenUs;
    int lane, side, kind, type, stack;
};
// op 0 (absolute) or op 1 (inside a pattern, times and lanes from its origin)
void literal(Buf& o, const std::vector<Ev>& e, bool relative) {
    o.u8(relative ? 1 : 0);
    o.u32(uint32_t(e.size()));
    if (relative) o.var(uint64_t(e[0].us));
    else o.zig(e[0].us);
    for (size_t k = 1; k < e.size(); ++k) o.var(uint64_t(e[k].us - e[k - 1].us));
    for (const Ev& x : e) o.var(x.lenUs);
    for (size_t k = 0; k < e.size(); ++k) o.zig(relative ? 0 : 0);  // pitch (+1 when absolute: -1 + 1 = 0)
    for (const Ev& x : e) relative ? o.zig(x.lane) : o.var(uint64_t(x.lane));
    for (const Ev& x : e) o.var(uint64_t(x.side) | uint64_t(x.kind) << 1);
    for (const Ev& x : e) o.var(uint64_t(x.type));
    for (const Ev& x : e) o.var(uint64_t(x.stack));
}
std::string deflated(const std::string& raw) {
    mz_ulong n = mz_compressBound(mz_ulong(raw.size()));
    std::string out(n, '\0');
    mz_compress2(reinterpret_cast<unsigned char*>(out.data()), &n, reinterpret_cast<const unsigned char*>(raw.data()), mz_ulong(raw.size()), 6);
    out.resize(n);
    return out;
}
uint32_t crc(const std::string& s) { return uint32_t(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(s.data()), s.size())); }
}  // namespace raidw

void testRaidCharts() {
    using namespace raidw;
    const fs::path base = fs::temp_directory_path() / "gh_raid";
    fs::remove_all(base);
    fs::create_directories(base);
    std::string err;
    // Pattern 0: two notes half a second apart, a second long.
    Buf pat;
    pat.u32(1);
    literal(pat, {{0, 0, 0, 1, 0, 0, 1}, {500000, 0, 1, 1, 0, 0, 1}}, true);
    Buf pats;
    pats.u32(1);
    pats.u64(1000000);
    pats.u8(0);
    pats.u32(uint32_t(pat.b.size()));
    pats.b += pat.b;
    pats.u32(crc(pats.b));
    // The chunk: 3 notes, the pattern 3 times one lane right, a burst of 2 a
    // quarter second apart, and a group clipped to 8.0-9.6 s (4 of 8 notes).
    Buf ch;
    ch.u32(4);
    literal(ch, {{0, 0, 0, 1, 0, 0, 1}, {100000, 250000, 2, 0, 0, 0, 5}, {200000, 0, 3, 1, 1, 1, 1}}, false);
    ch.u8(2), ch.u32(0), ch.u32(3), ch.zig(1000000), ch.zig(0), ch.zig(1);
    ch.u8(4), ch.u32(0), ch.u32(2), ch.zig(5000000), ch.zig(0), ch.zig(0), ch.zig(250000);
    ch.u8(3), ch.u32(1), ch.u32(0), ch.u32(4), ch.zig(8000000), ch.zig(0), ch.zig(0), ch.zig(8000000), ch.zig(9600000);
    ch.u64(17);
    const std::string meta =
        "{\"raid\":1,\"title\":\"Raided\",\"keys\":4,\"speed\":2.5,\"noteTypes\":[\"\",\"Hurt Note\"],\"tempo\":[{\"timeMs\":0,\"bpm\":150,\"beat\":0}]}";
    std::string metaPayload = meta + iutk::le32(crc(meta));
    const std::string metaStored = deflated(metaPayload), chunkStored = deflated(ch.b);
    const uint64_t indexOff = 128, metaOff = indexOff + 56, patOff = metaOff + metaStored.size(), chunkOff = patOff + pats.b.size();
    Buf h;
    h.b = "RAID";
    h.u32(2), h.u32(0), h.u32(0), h.u32(0);
    h.u64(17), h.u64(1), h.u64(1), h.u64(indexOff), h.u64(56);
    h.u64(metaOff), h.u64(metaStored.size()), h.u64(metaPayload.size());
    h.u64(patOff), h.u64(0), h.u64(pats.b.size());
    h.u32(0), h.u32(0);
    h.b.resize(128, '\0');
    const uint32_t hc = crc(h.b);
    for (int i = 0; i < 4; ++i) h.b[size_t(108 + i)] = char(hc >> (8 * i));
    Buf idx;
    idx.u64(0), idx.u64(0), idx.u64(9500000), idx.u64(chunkOff);
    idx.u32(uint32_t(chunkStored.size())), idx.u32(uint32_t(ch.b.size())), idx.u32(17), idx.u32(4), idx.u32(crc(ch.b)), idx.u32(1);
    put(base / "song.raid", h.b + idx.b + metaStored + pats.b + chunkStored);
    Chart c;
    SCHECK(countChartKind(str(base / "song.raid")) == CountChartKind::Raid && loadChartFile(str(base / "song.raid"), "Hard", c, err));
    SCHECK(c.title == "Raided" && c.difficulty == "Hard" && c.keys == 4 && c.speed == 2.5 && c.bpmAt(0) == 150 && c.noteTypes.size() == 2);
    SCHECK(c.notes.size() == 17 && c.playerNotes + c.opponentNotes == Count(21));
    if (c.notes.size() == 17) {
        SCHECK(c.notes[1].time == 100 && c.notes[1].side == 0 && c.notes[1].lane == 2 && c.notes[1].length == 250.0f &&
               c.countOf(c.notes[1]) == Count(5));
        SCHECK(c.notes[2].time == 200 && c.notes[2].kind == kMine && c.notes[2].type == 1);
        SCHECK(c.notes[3].time == 1000 && c.notes[3].lane == 1 && c.notes[4].time == 1500 && c.notes[4].lane == 2);  // moved a lane
        SCHECK(c.notes[9].time == 5000 && c.notes[10].time == 5250 && c.notes[10].lane == 0);  // the burst
        SCHECK(c.notes[13].time == 8000 && c.notes[16].time == 9500);  // the group, clipped at 9.6 s
    }
    // A damaged chunk is refused.
    {
        std::string bytes = h.b + idx.b + metaStored + pats.b + chunkStored;
        bytes[bytes.size() - 3] = char(bytes[bytes.size() - 3] ^ 0x20);
        put(base / "bad.raid", bytes);
        Chart bad;
        SCHECK(!loadChartFile(str(base / "bad.raid"), "Hard", bad, err));
    }
    fs::remove_all(base);
}

// God's Hand's own count chart (.ghn, issue #36).
void testGhnCharts() {
    const fs::path base = fs::temp_directory_path() / "gh_ghn";
    fs::remove_all(base);
    fs::create_directories(base);
    std::string err;
    // Everything a chart holds, through a file and back.
    Chart c;
    c.title = "Counted \"song\"";
    c.difficulty = "Insane";
    c.opponent = "dad";
    c.player = "bf";
    c.gf = "gf";
    c.stage = "stage";
    c.keys = 1024;
    c.sideKeys[0] = 7;
    c.speed = 3.25;
    c.noteTypes = {"", "Hurt Note", "Alt Animation"};
    c.tempo = {{0, 128, 0}, {8000, 200, 0}};
    c.sections = {{0, true, false, false, 4, 128}, {1875, false, true, true, 4, 0}};
    c.focus = {{0, 1}, {1875, 0}};
    c.events = {{500, "Hey!", "BF", "0.6"}, {9000, "Change Scroll Speed", "1.5", ""}};
    Count huge;
    huge.w[3] = uint64_t(1) << 8;  // 2^200
    auto add = [&](double t, int lane, int side, float len, int kind, int type, uint8_t flags, const Count& k) {
        Note n;
        n.time = t;
        n.lane = uint16_t(lane);
        n.side = uint8_t(side);
        n.length = len;
        n.kind = uint8_t(kind);
        n.type = uint16_t(type);
        n.flags = flags;
        c.notes.push_back(n);
        c.setCount(c.notes.back(), k);
    };
    add(0.000001, 0, 1, 0, kNormal, 0, 0, Count(1));  // a nanosecond in
    add(250.5, 1023, 1, 1234.75f, kNormal, 2, kFlagAlt, Count(3));
    add(250.5, 6, 0, 0, kMine, 1, 0, Count(1));
    add(9999.999999, 3, 1, 0, kNormal, 0, 0, huge);
    add(12000, 2, 0, 500, kNormal, 0, kFlagGF, Count(5000000000ull));
    c.finalize(128);
    GhnStats st;
    SCHECK(writeGhn(str(base / "song.ghn"), c, err, &st) && st.notes == 5 && st.total == c.playerNotes + c.opponentNotes);
    Chart r;
    SCHECK(countChartKind(str(base / "song.ghn")) == CountChartKind::Ghn && loadChartFile(str(base / "song.ghn"), "Insane", r, err));
    SCHECK(r.title == c.title && r.opponent == "dad" && r.player == "bf" && r.gf == "gf" && r.stage == "stage" && r.keys == 1024 &&
           r.sideKeys[0] == 7 && r.speed == 3.25);
    SCHECK(r.noteTypes == c.noteTypes && r.tempo.size() == 2 && r.bpmAt(9000) == 200 && r.sections.size() == 2 && !r.sections[1].mustHit &&
           r.sections[1].gf && r.focus.size() == 2 && r.events.size() == 2 && r.events[0].name == "Hey!" && r.events[1].v1 == "1.5");
    SCHECK(r.notes.size() == c.notes.size() && r.playerNotes == c.playerNotes && r.opponentNotes == c.opponentNotes);
    for (size_t i = 0; i < std::min(r.notes.size(), c.notes.size()); ++i) {
        const Note& a = c.notes[i];
        const Note& b = r.notes[i];
        SCHECK(std::fabs(a.time - b.time) < 1e-9 && a.lane == b.lane && a.side == b.side && a.length == b.length && a.kind == b.kind &&
               a.type == b.type && (a.flags & ~kFlagBigCount) == (b.flags & ~kFlagBigCount) && c.countOf(a) == r.countOf(b));
    }
    SCHECK(r.playerNotes == huge + Count(4) && r.playerNotes.str() == "1606938044258990275541962092341162602522202993782792835301380");
    // Damage is found: a block's checksum, a total that does not add up.
    {
        std::ifstream in(base / "song.ghn", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string b1 = bytes;
        b1[b1.size() - 2] = char(b1[b1.size() - 2] ^ 0x10);
        put(base / "bad1.ghn", b1);
        Chart bad;
        SCHECK(!loadChartFile(str(base / "bad1.ghn"), "Insane", bad, err));
    }
    // A spam chart repeats itself: stored once and repeated, it is a small file.
    Chart spam;
    spam.keys = 4;
    const int lanes[7] = {0, 1, 2, 3, 2, 1, 0};
    for (int i = 0; i < 7 * 140000; ++i) {
        Note n;
        n.time = 1000.0 + i * 1.5;  // 1.5 ms apart
        n.lane = uint16_t(lanes[i % 7]);
        n.side = 1;
        spam.notes.push_back(n);
    }
    spam.finalize(150);
    SCHECK(writeGhn(str(base / "spam.ghn"), spam, err, &st) && st.repeats >= 1 && st.blocks <= 2 && st.bytes < 300000);
    Chart sr;
    SCHECK(loadChartFile(str(base / "spam.ghn"), "Normal", sr, err) && sr.notes.size() == spam.notes.size() && sr.playerNotes == spam.playerNotes);
    bool same = sr.notes.size() == spam.notes.size();
    for (size_t i = 0; same && i < sr.notes.size(); i += 997) same = std::fabs(sr.notes[i].time - spam.notes[i].time) < 1e-6 && sr.notes[i].lane == spam.notes[i].lane;
    SCHECK(same);
    // A trillion notes a nanosecond apart, as one repeat: they load as a note
    // a millisecond (each counting a million), every one of them counted.
    {
        auto le32 = iutk::le32;
        std::string block{char(0), char(1), char(0), char(0), char(0), char(0)};  // one note: lane 0, the player's, count 1
        const std::string meta = "{\"ghn\":1,\"title\":\"Trillion\",\"keys\":4,\"playerNotes\":\"1000000000001\",\"opponentNotes\":\"0\",\"rawNotes\":\"1000000000001\"}";
        std::string dir;
        dir += char(0);
        dir += iutk::varint(1) + iutk::varint(block.size()) + iutk::varint(0) + le32(raidw::crc(block)) + iutk::varint(0) + iutk::varint(0);
        dir += char(1);
        dir += iutk::varint(0) + iutk::varint(1000000000000ull) + iutk::varint(2) + iutk::varint(0);  // shift 1 ns (zigzag 2), start 0
        std::string head = "GHN1" + le32(1) + le32(0) + le32(2);
        const uint64_t metaOff = 64, dirOff = metaOff + meta.size(), dataOff = dirOff + dir.size();
        head += le32(uint32_t(metaOff)) + le32(0) + le32(0) + le32(uint32_t(meta.size())) + le32(raidw::crc(meta));
        head += le32(uint32_t(dirOff)) + le32(0) + le32(uint32_t(dir.size())) + le32(raidw::crc(dir));
        head += le32(uint32_t(dataOff)) + le32(0);
        head += le32(raidw::crc(head));
        put(base / "trillion.ghn", head + meta + dir + block);
        Chart t;
        SCHECK(loadChartFile(str(base / "trillion.ghn"), "Normal", t, err));
        SCHECK(t.notes.size() == 1000000 && t.playerNotes == Count(1000000000001ull) && t.rawNoteCount == Count(1000000000001ull));
        SCHECK(t.notes.size() == 1000000 && t.countOf(t.notes[1]) == Count(1000000) && t.notes.back().time == 999999);
        // A total that does not match what the notes add up to is refused.
        std::string wrong = meta;
        wrong.replace(wrong.find("1000000000001"), 13, "1000000000002");
        std::string head2 = head.substr(0, 60);
        const std::string c2 = le32(raidw::crc(wrong));
        head2.replace(32, 4, c2);
        head2 += le32(raidw::crc(head2));
        put(base / "wrong.ghn", head2 + wrong + dir + block);
        Chart w;
        SCHECK(!loadChartFile(str(base / "wrong.ghn"), "Normal", w, err) && err.find("add up") != std::string::npos);
    }
    // Blocks A A A B B: a repeat that grows, then a new block and its repeat
    // (the directory and the blocks' first notes stay in step).
    {
        Chart ab;
        ab.title = "ab";
        const size_t kB = 65536;
        for (size_t b = 0; b < 5; ++b)
            for (size_t i = 0; i < kB; ++i) {
                Note n;
                n.time = double(b) * 100000.0 + double(i) * (b < 3 ? 1.0 : 1.5);
                n.lane = uint16_t(b < 3 ? 0 : 1);
                n.side = 1;
                ab.notes.push_back(n);
            }
        ab.finalize(120);
        GhnStats st2;
        SCHECK(writeGhn(str(base / "ab.ghn"), ab, err, &st2) && st2.blocks == 2 && st2.repeats == 2);
        Chart back;
        SCHECK(loadChartFile(str(base / "ab.ghn"), "Normal", back, err) && back.notes.size() == ab.notes.size());
        bool same = back.notes.size() == ab.notes.size();
        for (size_t i = 0; same && i < ab.notes.size(); ++i)
            same = std::fabs(back.notes[i].time - ab.notes[i].time) < 1e-6 && back.notes[i].lane == ab.notes[i].lane;
        SCHECK(same);
        // Saved again over itself (Windows does not rename over a file).
        SCHECK(writeGhn(str(base / "ab.ghn"), ab, err) && !fs::exists(base / "ab.ghn.part"));
    }
    // A count chart to .ghc: each counted note listed, the same chart played.
    {
        Chart small;
        small.keys = 4;
        small.title = "Small";
        small.events = {{100, "Hey!", "BF", "0.6"}};
        auto addS = [&](double t, int lane, int side, const Count& k) {
            Note n;
            n.time = t;
            n.lane = uint16_t(lane);
            n.side = uint8_t(side);
            small.notes.push_back(n);
            small.setCount(small.notes.back(), k);
        };
        addS(0, 0, 1, Count(3));
        addS(500, 2, 0, Count(1));
        addS(2500, 3, 1, Count(600));  // more than one .ghc stack (255)
        small.finalize(120);
        SCHECK(writeGhn(str(base / "small.ghn"), small, err));
        SCHECK(convertChartFile(str(base / "small.ghn"), str(base / "small.ghc")) == 0);
        Chart g;
        SCHECK(loadChartFile(str(base / "small.ghc"), "Normal", g, err) && g.playerNotes == Count(603) && g.opponentNotes == Count(1) &&
               g.events.size() == 1);
        bool sameNotes = g.notes.size() == small.notes.size();
        for (size_t i = 0; sameNotes && i < g.notes.size(); ++i)
            sameNotes = std::fabs(g.notes[i].time - small.notes[i].time) < 1e-6 && g.notes[i].lane == small.notes[i].lane &&
                        g.notes[i].side == small.notes[i].side && g.countOf(g.notes[i]) == small.countOf(small.notes[i]);
        SCHECK(sameNotes);
        // Counts no .ghc can list: refused, nothing written.
        SCHECK(convertChartFile(str(base / "song.ghn"), str(base / "huge.ghc")) != 0 && !fs::exists(base / "huge.ghc"));
    }
    // Extra keys off: a 7-key count chart plays with 4 keys, as its JSON would.
    {
        Chart k7;
        k7.keys = 7;
        auto add7 = [&](double t, int lane, const Count& k) {
            Note n;
            n.time = t;
            n.lane = uint16_t(lane);
            n.side = 1;
            k7.notes.push_back(n);
            k7.setCount(k7.notes.back(), k);
        };
        add7(0, 0, Count(2));
        add7(0, 4, Count(3));  // lane 4 -> 0: one stack of 5
        add7(100, 6, Count(1));
        k7.finalize(120);
        SCHECK(writeGhn(str(base / "k7.ghn"), k7, err));
        Chart on, off;
        SCHECK(loadChartFile(str(base / "k7.ghn"), "Normal", on, err, {}, 0, true) && on.keys == 7 && on.notes.size() == 3);
        SCHECK(loadChartFile(str(base / "k7.ghn"), "Normal", off, err, {}, 0, false) && off.keys == 4 && off.notes.size() == 2 &&
               off.countOf(off.notes[0]) == Count(5) && off.notes[1].lane == 2 && off.playerNotes == Count(6));
    }
    fs::remove_all(base);
}

// Freeplay's icons (issue #36): the chart's "player2", that character's
// "healthicon", then images/icons/icon-<icon>.png or <icon>.png, in the
// song's own folders first and the shared ones after.
void testSongIcons() {
    const fs::path base = fs::temp_directory_path() / "gh_song_icons";
    fs::remove_all(base);
    const fs::path mod = base / "mymod", other = base / "othermod";
    put(mod / "data/duel/duel.json", R"({"song": {"song": "Duel", "player1": "bf", "player2": "rival", "bpm": 120, "notes": []}})");
    put(mod / "characters/rival.json", R"({"image": "characters/rival", "healthicon": "rival-face", "animations": []})");
    put(mod / "images/icons/icon-rival-face.png", "png");
    put(mod / "data/solo/solo.json", R"({"song": {"song": "Solo", "player2": "loner", "notes": []}})");
    put(other / "images/icons/loner.png", "png");  // another mod's, named without "icon-"
    put(mod / "data/none/none.json", R"({"song": {"song": "None", "player2": "nobody", "notes": []}})");
    SongEntry e;
    e.assetRoots = {str(mod)};
    e.chartPaths = {str(mod / "data/duel/duel.json")};
    SCHECK(endsWith(findSongIcon(e, {}), "icons/icon-rival-face.png"));
    e.chartPaths = {str(mod / "data/solo/solo.json")};
    SCHECK(findSongIcon(e, {}).empty());                           // not in its own folders...
    SCHECK(endsWith(findSongIcon(e, {str(other)}), "icons/loner.png"));  // ...but in another mod's
    e.chartPaths = {str(mod / "data/none/none.json")};
    SCHECK(findSongIcon(e, {str(other)}).empty());  // no icon anywhere: the game's own
    e.builtin = true;
    e.chartPaths = {str(mod / "data/duel/duel.json")};
    SCHECK(findSongIcon(e, {}).empty());  // built-in songs keep their own icons
    fs::remove_all(base);
}

// JSON merging (issue #36): split charts back into one.
namespace mergetest {
// A legacy Psych chart: `secs` sections of 4 beats at 120 BPM (2 s each),
// mustHitSection alternating from true; notes as [time, data, length].
std::string chart(const std::string& song, int secs, const std::vector<std::vector<std::string>>& notesBySection,
                  const std::string& events = "[]") {
    std::string o = "{\"song\":{\"song\":\"" + song + "\",\"bpm\":120,\"speed\":2.5,\"player1\":\"bf\",\"player2\":\"dad\",\"notes\":[";
    for (int i = 0; i < secs; ++i) {
        o += std::string(i ? "," : "") + "{\"sectionNotes\":[";
        if (size_t(i) < notesBySection.size())
            for (size_t k = 0; k < notesBySection[size_t(i)].size(); ++k) o += std::string(k ? "," : "") + notesBySection[size_t(i)][k];
        o += "],\"mustHitSection\":" + std::string(i % 2 == 0 ? "true" : "false") + ",\"sectionBeats\":4}";
    }
    return o + "],\"events\":" + events + "}}";
}
// What a chart plays: every note as (time, side, lane, count).
std::vector<std::tuple<double, int, int, uint64_t>> plays(const std::string& path) {
    Chart c;
    std::string err;
    std::vector<std::tuple<double, int, int, uint64_t>> v;
    if (!loadChartFile(path, "Normal", c, err)) return v;
    for (const Note& n : c.notes) v.emplace_back(n.time, n.side, n.lane, c.countOf(n).sat64());
    std::sort(v.begin(), v.end());
    return v;
}
}  // namespace mergetest

// AutoChart's charts (.ghac): marked, read back marked, never written in
// another engine's format, and listed as their song's difficulties.
void testAutoChartFiles() {
    const fs::path base = fs::temp_directory_path() / "gh_ghac";
    fs::remove_all(base);
    fs::create_directories(base);
    std::string err;
    Chart c;
    c.autoCharted = true;
    c.title = "Loose";
    for (int i = 0; i < 16; ++i) {
        Note n;
        n.time = 500.0 + i * 250.0;
        n.lane = uint16_t(i % 4);
        n.side = uint8_t(i % 2);
        c.notes.push_back(n);
    }
    c.finalize(120);
    // A song of loose audio in songs/, with an AutoChart saved beside it.
    putWav(base / "songs" / "Loose.wav", 6);
    SCHECK(writeGhn(str(base / "songs" / "Loose-autochart-hard.ghac"), c, err));
    SCHECK(isAutoChartFile(str(base / "songs" / "Loose-autochart-hard.ghac")) && isCountChart(str(base / "songs" / "Loose-autochart-hard.ghac")));
    Chart back;
    SCHECK(loadChartFile(str(base / "songs" / "Loose-autochart-hard.ghac"), "Hard", back, err) && back.autoCharted && back.notes.size() == 16);
    // Never another engine's format.
    SCHECK(!writeFnc1(str(base / "x.bin"), back, err) && !fs::exists(base / "x.bin"));
    SCHECK(convertChartFile(str(base / "songs" / "Loose-autochart-hard.ghac"), str(base / "x.ghc")) != 0 && !fs::exists(base / "x.ghc"));
    SCHECK(convertChartFile(str(base / "songs" / "Loose-autochart-hard.ghac"), str(base / "x.ghn")) != 0 && !fs::exists(base / "x.ghn"));
    SCHECK(convertChartFile(str(base / "songs" / "Loose-autochart-hard.ghac"), str(base / "y.ghac")) == 0 && isAutoChartFile(str(base / "y.ghac")));
    // A real chart is not saved as .ghac, and a .ghn is not marked.
    Chart real = back;
    real.autoCharted = false;
    SCHECK(writeGhn(str(base / "real.ghn"), real, err) && !isAutoChartFile(str(base / "real.ghn")));
    SCHECK(convertChartFile(str(base / "real.ghn"), str(base / "real.ghac")) != 0 && !fs::exists(base / "real.ghac"));
    // The scan: the loose song keeps its AutoChart difficulties and gains the saved one.
    SongScan sc;
    scanSongsDir(str(base / "songs"), sc);
    const SongEntry* e = byTitle(sc, "Loose");
    SCHECK(e && e->autoChart && e->diffs.size() == 4 && e->diffs.back() == "AutoChart Hard" && endsWith(e->chartPaths.back(), ".ghac"));
    // In a song's folder, a saved AutoChart is one of its charts.
    put(base / "songs" / "Folder" / "folder.json", R"({"song":{"song":"Folder","bpm":120,"notes":[{"sectionNotes":[[0,0,0]],"mustHitSection":true}]}})");
    putWav(base / "songs" / "Folder" / "Inst.wav", 4);
    SCHECK(writeGhn(str(base / "songs" / "Folder" / "folder-autochart-easy.ghac"), c, err));
    SongScan sc2;
    scanSongsDir(str(base / "songs"), sc2);
    const SongEntry* f = byTitle(sc2, "Folder");
    SCHECK(f && std::find(f->diffs.begin(), f->diffs.end(), "AutoChart Easy") != f->diffs.end());
    fs::remove_all(base);
}

// The threadripper engine's .thr, as strand reads it.
namespace thrw {
std::string le(uint64_t v, int n) {
    std::string r;
    for (int i = 0; i < n; ++i) r += char((v >> (8 * i)) & 255);
    return r;
}
std::string f64(double d) {
    uint64_t v;
    std::memcpy(&v, &d, 8);
    return le(v, 8);
}
std::string f32(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return le(v, 4);
}
// A Zstandard frame holding `data` as it is (one raw block).
std::string zstdRaw(const std::string& data) {
    std::string z = le(0xFD2FB528u, 4);
    z += char(0xA0);  // single segment, 4-byte content size
    z += le(data.size(), 4);
    z += le(1u | uint32_t(data.size()) << 3, 3);  // the last block, raw
    return z + data;
}
// A zip of the given entries, each stored with its method's bytes as given.
struct Entry {
    std::string name, data;
    uint16_t method;
    size_t size;  // uncompressed
    uint32_t crc;
};
std::string zip(const std::vector<Entry>& es) {
    std::string out, cd;
    for (const Entry& e : es) {
        const size_t at = out.size();
        out += le(0x04034b50u, 4) + le(20, 2) + le(0, 2) + le(e.method, 2) + le(0, 4) + le(e.crc, 4) + le(e.data.size(), 4) +
               le(e.size, 4) + le(e.name.size(), 2) + le(0, 2) + e.name + e.data;
        cd += le(0x02014b50u, 4) + le(20, 2) + le(20, 2) + le(0, 2) + le(e.method, 2) + le(0, 4) + le(e.crc, 4) + le(e.data.size(), 4) +
              le(e.size, 4) + le(e.name.size(), 2) + le(0, 2) + le(0, 2) + le(0, 2) + le(0, 2) + le(0, 4) + le(at, 4) + e.name;
    }
    const size_t cdAt = out.size();
    out += cd + le(0x06054b50u, 4) + le(0, 2) + le(0, 2) + le(es.size(), 2) + le(es.size(), 2) + le(cd.size(), 4) + le(cdAt, 4) + le(0, 2);
    return out;
}
Entry entry(const std::string& name, const std::string& data, uint16_t method) {
    const uint32_t crc = uint32_t(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size()));
    return {name, method == 93 ? zstdRaw(data) : data, method, data.size(), crc};
}
}  // namespace thrw

void testThrCharts() {
    using namespace thrw;
    const fs::path base = fs::temp_directory_path() / "gh_thr";
    fs::remove_all(base);
    fs::create_directories(base);
    std::string err;
    // Written and read back: sides, lanes, holds, types (a Hurt Note is a mine), stacks listed.
    Chart c;
    c.title = "Thr Song";
    c.keys = 4;
    c.speed = 2.7;
    c.events = {{250, "Hey!", "BF", "0.6"}};
    auto add = [&](double t, int lane, int side, float len, const std::string& type, uint32_t count) {
        Note n;
        n.time = t;
        n.lane = uint16_t(lane);
        n.side = uint8_t(side);
        n.length = len;
        if (!type.empty()) applyNoteType(type, n, c);
        c.notes.push_back(n);
        c.setCount(c.notes.back(), Count(count));
    };
    add(0, 0, 1, 0, "", 1);
    add(500, 3, 0, 750, "", 1);
    add(1000, 2, 1, 0, "Hurt Note", 1);
    add(1500, 1, 1, 0, "", 70000);  // past one filter block of 65,536
    c.finalize(150);
    SCHECK(writeThrChart(str(base / "a.thr"), c, err) && isThrChart(str(base / "a.thr")) && countChartKind(str(base / "a.thr")) == CountChartKind::Thr);
    Chart r;
    SCHECK(loadChartFile(str(base / "a.thr"), "Normal", r, err) && r.title == "Thr Song" && r.keys == 4 && std::fabs(r.speed - 2.7) < 1e-9);
    SCHECK(r.notes.size() == 4 && r.playerNotes == Count(70002) && r.opponentNotes == Count(1) && r.events.size() == 1 &&
           std::fabs(r.bpmAt(0) - 150) < 1e-9);
    if (r.notes.size() == 4) {
        SCHECK(r.notes[1].side == 0 && r.notes[1].lane == 3 && std::fabs(r.notes[1].length - 750) < 1e-3);
        SCHECK(r.notes[2].kind == kMine && r.noteTypes[r.notes[2].type] == "Hurt Note");
        SCHECK(r.notes[3].lane == 1 && r.countOf(r.notes[3]) == Count(70000));
    }
    // strand's own layout, packed with Zstandard: noteData under keyCount on
    // the side of the section's mustHit (not psych_v1).
    auto sectionRec = [](uint64_t notes, bool must) {
        std::string p(40, '\0');
        p.replace(8, 4, le(notes & 0xFFFFFFFFu, 4));
        p.replace(12, 4, le(must ? 1 : 0, 4));
        p.replace(32, 4, le(notes >> 32, 4));
        return p;
    };
    std::string sections = le(0x534C4254u, 4) + std::string(4, '\0') + f64(2) + std::string(16, '\0');
    sections += sectionRec(2, true) + sectionRec(2, false);
    auto noteRec = [](double t, int data) { return f64(t) + f32(0) + le(uint32_t(data), 2) + le(0, 2); };
    std::string notes = le(0x4E4C4254u, 4) + std::string(4, '\0') + f64(4) + std::string(16, '\0');
    notes += noteRec(0, 0) + noteRec(100, 5) + noteRec(2000, 2) + noteRec(2100, 7);
    const std::string meta = R"({"song":{"song":"Strand","bpm":120,"speed":1.5},"tbl":{"parts":[{"prefix":"","keyCount":4,"types":[null],"notesPerFile":4194304,"noteFiles":1,"noteCount":4}]}})";
    put(base / "b.thr", zip({entry("strand-metadata.json", meta, 93), entry("notes/sections.bin", sections, 93),
                             entry("notes/notes-00000.bin", notes, 0)}));
    Chart b;
    SCHECK(loadChartFile(str(base / "b.thr"), "Normal", b, err) && b.notes.size() == 4 && b.title == "Strand");
    if (b.notes.size() == 4) {
        SCHECK(b.notes[0].side == 1 && b.notes[0].lane == 0 && b.notes[1].side == 0 && b.notes[1].lane == 1);  // mustHit: 0-3 the player's
        SCHECK(b.notes[2].side == 0 && b.notes[2].lane == 2 && b.notes[3].side == 1 && b.notes[3].lane == 3);  // not: the opponent's
    }
    // LZMA (method 14) is named in the error.
    put(base / "c.thr", zip({entry("x-metadata.json", meta, 0), entry("notes/sections.bin", sections, 14), entry("notes/notes-00000.bin", notes, 0)}));
    Chart lz;
    SCHECK(!loadChartFile(str(base / "c.thr"), "Normal", lz, err) && err.find("LZMA") != std::string::npos);
    // In a song's folder, and through the converter.
    put(base / "songs" / "Thr Song" / "Inst.ogg", "x");
    fs::copy_file(base / "a.thr", base / "songs" / "Thr Song" / "thr song.thr");
    SongScan sc;
    scanSongsDir(str(base / "songs"), sc);
    const SongEntry* e = byTitle(sc, "Thr Song");
    SCHECK(e && !e->autoChart && e->chartPaths.size() == 1 && endsWith(e->chartPaths[0], ".thr"));
    SCHECK(convertChartFile(str(base / "a.thr"), str(base / "a.ghn")) == 0 && convertChartFile(str(base / "a.ghn"), str(base / "back.thr")) == 0);
    Chart back;
    SCHECK(loadChartFile(str(base / "back.thr"), "Normal", back, err) && back.playerNotes == r.playerNotes && back.notes.size() == r.notes.size());
    fs::remove_all(base);
}

void testJsonMerge() {
    using namespace mergetest;
    const fs::path base = fs::temp_directory_path() / "gh_merge";
    fs::remove_all(base);
    fs::create_directories(base);
    std::string err;
    // Overlay: the same 4 sections in both parts, the first part's notes in
    // sections 0-1, the second's in 2-3 (one stacked three times).
    put(base / "song-2.json", chart("song", 4, {{}, {}, {"[4000,0,0]", "[4500,5,0]"}, {"[6000,1,0]", "[6000,1,0]", "[6000,1,0]"}},
                                    "[[100,[[\"Hey!\",\"BF\",\"0.6\"]]]]"));
    put(base / "song-1.json", chart("song", 4, {{"[0,0,0]", "[500,4,250]"}, {"[2000,2,0]"}}, "[[100,[[\"Hey!\",\"BF\",\"0.6\"]]]]"));
    put(base / "song-10.json", chart("song", 4, {}));
    std::vector<MergePart> parts = {inspectPart(str(base / "song-10.json")), inspectPart(str(base / "song-2.json")),
                                    inspectPart(str(base / "song-1.json"))};
    orderParts(parts);
    SCHECK(parts[0].name == "song-1.json" && parts[1].name == "song-2.json" && parts[2].name == "song-10.json");
    SCHECK(parts[0].ok && parts[0].sections == 4 && parts[0].notes == 3 && parts[1].notes == 5 && parts[0].spanMs == 8000);
    SCHECK(detectMergeMode(parts) == MergeMode::Overlay);
    MergeResult res;
    SCHECK(mergeChartParts({str(base / "song-1.json"), str(base / "song-2.json")}, str(base / "merged.json"), MergeMode::Auto, res, err));
    SCHECK(res.mode == MergeMode::Overlay && res.notes == 8 && res.sections == 4 && res.events == 1);
    auto both = plays(str(base / "song-1.json"));
    for (const auto& n : plays(str(base / "song-2.json"))) both.push_back(n);
    std::sort(both.begin(), both.end());
    SCHECK(plays(str(base / "merged.json")) == both);  // every note where and whose it was
    Chart m;
    SCHECK(loadChartFile(str(base / "merged.json"), "Normal", m, err) && m.events.size() == 1 && m.playerNotes + m.opponentNotes == Count(8));
    // Append, the second part's notes from 0 again: they go after the first part.
    put(base / "a-1.json", chart("a", 2, {{"[0,0,0]"}, {"[2500,1,0]"}}));
    put(base / "a-2.json", chart("a", 3, {{"[0,2,0]"}, {}, {"[4100,3,0]"}}, "[[50,[[\"Hey!\",\"GF\",\"\"]]]]"));
    std::vector<MergePart> ap = {inspectPart(str(base / "a-1.json")), inspectPart(str(base / "a-2.json"))};
    SCHECK(detectMergeMode(ap) == MergeMode::Append);
    SCHECK(mergeChartParts({str(base / "a-1.json"), str(base / "a-2.json")}, str(base / "a.json"), MergeMode::Auto, res, err));
    SCHECK(res.mode == MergeMode::Append && res.sections == 5 && res.notes == 4);
    auto ap2 = plays(str(base / "a.json"));
    SCHECK(ap2.size() == 4 && std::get<0>(ap2[2]) == 4000 && std::get<0>(ap2[3]) == 8100);  // moved by 4 s (2 sections)
    Chart a;
    SCHECK(loadChartFile(str(base / "a.json"), "Normal", a, err) && a.events.size() == 1 && a.events[0].timeMs == 4050);
    // Append, slices of a song that kept their times: nothing moves.
    put(base / "b-1.json", chart("b", 2, {{"[0,0,0]"}, {"[2500,1,0]"}}));
    put(base / "b-2.json", chart("b", 2, {{"[4000,2,0]"}, {"[6100,3,0]"}}));
    std::vector<MergePart> bp = {inspectPart(str(base / "b-1.json")), inspectPart(str(base / "b-2.json"))};
    SCHECK(detectMergeMode(bp) == MergeMode::Append);
    SCHECK(mergeChartParts({str(base / "b-1.json"), str(base / "b-2.json")}, str(base / "b.json"), MergeMode::Auto, res, err));
    auto bp2 = plays(str(base / "b.json"));
    SCHECK(bp2.size() == 4 && std::get<0>(bp2[2]) == 4000 && std::get<0>(bp2[3]) == 6100 && std::get<1>(bp2[3]) == 0);
    // Psych 1.0 charts keep their shape.
    put(base / "v1-1.json", "{\"song\":\"v\",\"format\":\"psych_v1\",\"bpm\":100,\"notes\":[{\"sectionNotes\":[[0,0,0]],\"mustHitSection\":true,\"sectionBeats\":4}],\"events\":[]}");
    put(base / "v1-2.json", "{\"song\":\"v\",\"format\":\"psych_v1\",\"bpm\":100,\"notes\":[{\"sectionNotes\":[[1200,5,0]],\"mustHitSection\":true,\"sectionBeats\":4}],\"events\":[]}");
    SCHECK(mergeChartParts({str(base / "v1-1.json"), str(base / "v1-2.json")}, str(base / "v1.json"), MergeMode::Overlay, res, err));
    {
        std::ifstream in(base / "v1.json");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        SCHECK(text.rfind("{\"song\":\"v\",\"format\":\"psych_v1\"", 0) == 0 && res.notes == 2);
    }
    // Parts whose notes wait on disk merge the same.
    g_mergeSpillAbove = 1;
    SCHECK(mergeChartParts({str(base / "song-1.json"), str(base / "song-2.json")}, str(base / "spilled.json"), MergeMode::Auto, res, err));
    SCHECK(res.notes == 8 && plays(str(base / "spilled.json")) == both);
    SCHECK(mergeChartParts({str(base / "a-1.json"), str(base / "a-2.json")}, str(base / "spilled.json"), MergeMode::Auto, res, err));
    SCHECK(res.notes == 4 && plays(str(base / "spilled.json")) == ap2);
    g_mergeSpillAbove = size_t(1) << 20;
    SCHECK(!fs::exists(base / "spilled.json.notes0.tmp") && !fs::exists(base / "spilled.json.part"));
    // Cancelled: no file is left.
    std::atomic<bool> stop{true};
    SCHECK(!mergeChartParts({str(base / "song-1.json"), str(base / "song-2.json")}, str(base / "c.json"), MergeMode::Auto, res, err,
                            nullptr, &stop) &&
           !fs::exists(base / "c.json") && !fs::exists(base / "c.json.part"));
    // A part that is not a chart is named in the error.
    put(base / "bad-3.json", "{\"song\":{\"song\":\"x\"}}");
    SCHECK(!mergeChartParts({str(base / "song-1.json"), str(base / "bad-3.json")}, str(base / "x.json"), MergeMode::Auto, res, err) &&
           err.find("bad-3") != std::string::npos);
    fs::remove_all(base);
}

void runScanTests() {
    testBinaryChartsInMods();
    testFlStudio();
    testFlpSong();
    testPulseForgeCharts();
    testSongIcons();
    testIutkCharts();
    testRaidCharts();
    testGhnCharts();
    testJsonMerge();
    testThrCharts();
    testAutoChartFiles();
    testAudioSeconds();
    testModLayouts();
}
