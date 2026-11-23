// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through
// issues.

// The AutoChart screen (issue #36), laid out as FL Studio: the channel rack
// on the left - each of the song's sounds (instrumental, voices, any stem
// added, the channels of an FL Studio project) with whose notes it gives,
// how its sounds become lanes, how many of them and a mute - the playlist on
// the right with each sound's waveform or notes and a tempo lane whose BPM
// markers can be added, moved in value and removed, and the chart it makes
// under them. GENERATE makes the chart, PLAY CHART plays it (with the shame
// watermark), SAVE keeps it as .ghac beside the song, TRAIN teaches the
// model with every charted song of the installed mods.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app.hpp"
#include "audio.hpp"
#include "autochart.hpp"
#include "autochart_train.hpp"
#include "common.hpp"
#include "count_charts.hpp"
#include "editor_ui.hpp"
#include "flp_import.hpp"
#include "gfx.hpp"
#include "platform.hpp"
#include "raylib.h"

namespace gh {

namespace {

namespace fs = std::filesystem;
using namespace edui;
using gfx::fade;

const Color kGold{255, 222, 64, 255};
const Color kIvory{242, 244, 255, 255};
const Color kBad{255, 130, 130, 255};
const Color kLaneColors[4] = {{194, 75, 153, 255}, {0, 255, 255, 255}, {18, 250, 5, 255}, {249, 57, 63, 255}};
const Color kRoleColors[4] = {{90, 90, 100, 255}, {80, 170, 255, 255}, {255, 110, 110, 255}, {200, 150, 255, 255}};
const char* kDiffNames[4] = {"Easy", "Normal", "Hard", "Insane"};

fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string textOf(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

void hints(const char* s) {
    if (app().touchMode) return;
    DrawRectangleRec({0, gfx::H - 26, gfx::W, 26}, Color{0, 0, 0, 170});
    gfx::text(FontId::Ui, s, gfx::W / 2, gfx::H - 23, 17, Color{220, 214, 235, 255}, Align::Center);
}

struct StudioTrack {
    std::string name;
    std::shared_ptr<const Pcm> pcm;  // audio, whole
    std::vector<FlpNote> flp;        // or an FL Studio channel's notes
    int ppqn = 96;
    AutoRole role = AutoRole::Turns;
    AutoLanes lanes = AutoLanes::Pitch;
    float density = 1.0f;
    bool muted = false;
    std::vector<float> peaks;  // the waveform: loudest sample each 10 ms (0..1)
    bool audio() const { return pcm && !pcm->data.empty(); }
};

std::vector<float> peaksOf(const Pcm& p) {
    std::vector<float> out;
    const size_t step = Mixer::kRate / 100, ch = size_t(std::max(1, p.channels));
    const size_t frames = p.frames();
    out.reserve(frames / step + 1);
    for (size_t f = 0; f < frames; f += step) {
        int m = 0;
        const size_t end = std::min(frames, f + step);
        for (size_t i = f; i < end; i += 4)
            for (size_t c = 0; c < ch; ++c) m = std::max(m, std::abs(int(p.data[i * ch + c])));
        out.push_back(float(m) / 32768.0f);
    }
    return out;
}

class AutoChartStudio : public State {
public:
    AutoChartStudio(std::string songId, int difficulty) : songId_(std::move(songId)), diff_(std::clamp(difficulty, 0, 3)) {}
    ~AutoChartStudio() override {
        train_.stop = true;
        if (loader_.joinable()) loader_.join();
        if (generator_.joinable()) generator_.join();
        if (trainer_.joinable()) trainer_.join();
        app().mixer.stop();
        app().mixer.clear();
    }
    bool wantsMenuMusic() const override { return false; }

    void enter() override {
        App& a = app();
        a.stopMenuMusic();
        a.mixer.stop();
        entry_ = a.library.find(songId_);
        modelPath_ = a.userDir + "autochart-model.txt";
        useModel_ = a.settings.autoChartTrained;
        if (!entry_) {
            status_ = "Song not found.";
            return;
        }
        loading_ = true;
        const SongEntry e = *entry_;
        loader_ = std::thread([this, e] {
            std::vector<StudioTrack> tracks;
            auto add = [&](const std::string& name, std::shared_ptr<const Pcm> pcm, AutoRole role) {
                if (!pcm || pcm->data.empty()) return;
                StudioTrack t;
                t.name = name;
                t.pcm = std::move(pcm);
                t.role = role;
                t.peaks = peaksOf(*t.pcm);
                tracks.push_back(std::move(t));
            };
            auto load = [&](const std::string& path) -> std::shared_ptr<const Pcm> {
                if (path.empty()) return nullptr;
                auto p = std::make_shared<Pcm>();
                std::string err;
                return loadAudioFile(path, *p, err) ? p : nullptr;
            };
            if (e.builtin) {
                Prepared p = app().library.reloadAudio(e);
                const bool voices = p.voiceP || p.voiceO;
                add("Instrumental", p.inst, voices ? AutoRole::Ignore : AutoRole::Turns);
                add("Voices (player)", p.voiceP, AutoRole::Player);
                add("Voices (opponent)", p.voiceO, AutoRole::Opponent);
            } else {
                auto vp = load(e.voicesP), vo = load(e.voicesO), shared = (vp || vo) ? nullptr : load(e.voices);
                add("Instrumental", load(e.inst), (vp || vo || shared) ? AutoRole::Ignore : AutoRole::Turns);
                add("Voices (player)", vp, AutoRole::Player);
                add("Voices (opponent)", vo, AutoRole::Opponent);
                add("Voices", shared, AutoRole::Turns);
            }
            std::lock_guard<std::mutex> lk(mtx_);
            tracks_ = std::move(tracks);
            loading_ = false;
            autoGenerate_ = true;
        });
    }

    void update(double) override {
        App& a = app();
        Input& in = a.input;
        feedUi();
        runActions();
        pollPick();
        if (generator_.joinable() && !generating_.load()) {
            generator_.join();
            std::lock_guard<std::mutex> lk(mtx_);
            if (genOk_) {
                chart_ = std::move(genChart_);
                if (tempo_.empty()) tempo_ = {{genInfo_.firstBeatMs, genInfo_.bpm, 0}};
                status_ = std::to_string(chart_.notes.size()) + " notes (" + (chart_.playerNotes.str()) + " player, " +
                          chart_.opponentNotes.str() + " opponent), " + (genInfo_.trained ? "placed by the trained model" : "from the beat and the sound");
            } else {
                status_ = "No chart: " + genErr_;
            }
        }
        if (trainer_.joinable() && !train_.running.load()) {
            trainer_.join();
            std::lock_guard<std::mutex> lk(train_.mtx);
            status_ = train_.message;
        }
        if (!loading_.load() && autoGenerate_) {
            autoGenerate_ = false;
            generate();
        }
        // --autochart-studio <song> --studio-save: saves the first chart made (screenshots and tests).
        if (a.autoRun.studioSave && !studioSaved_ && !chart_.notes.empty() && !generating_.load()) {
            studioSaved_ = true;
            save();
        }
        if (train_.running.load()) {
            if (in.back()) train_.stop = true;
            return;
        }
        if (in.back()) {
            sfxPlay(Sfx::Cancel);
            a.songListMode = App::SongListMode::AutoChart;
            a.change(makeFreeplay());
            return;
        }
        if (loading_.load()) return;
        if (in.pressed(KEY_SPACE)) togglePlay();
        if (in.pressed(KEY_G)) generate();
        if (in.accept()) playChart();
        if (in.pressed(KEY_S)) save();
        if (in.pressed(KEY_T)) train();
        if (in.left()) {
            diff_ = (diff_ + 3) % 4;
            generate();
        }
        if (in.right()) {
            diff_ = (diff_ + 1) % 4;
            generate();
        }
        if (in.up()) sel_ = std::max(0, sel_ - 1);
        if (in.down()) sel_ = std::min(int(tracks_.size()) - 1, sel_ + 1);
        if (in.pressed(KEY_A)) addAudio();
        if (in.pressed(KEY_F)) addFlp();
        playlistMouse();
        if (a.mixer.playing()) playMs_ = a.mixer.time(a.now) * 1000.0;
    }

    void draw() override {
        App& a = app();
        gfx::beginCanvas();
        DrawRectangleRec({0, 0, gfx::W, gfx::H}, Color{24, 22, 30, 255});
        std::lock_guard<std::mutex> lk(mtx_);
        drawTop();
        drawRack();
        drawPlaylist();
        drawChartLane();
        drawBottom();
        if (train_.running.load()) drawTraining();
        if (loading_.load()) {
            DrawRectangleRec({0, 0, gfx::W, gfx::H}, fade(BLACK, 0.6f));
            gfx::text(FontId::Title, "Loading the song's sounds...", gfx::W / 2, gfx::H / 2 - 20, 30, kIvory, Align::Center, 2.0f);
        }
        hints("SPACE listen   G generate   ENTER play chart   S save   T train   A add audio   F add FLP   LEFT/RIGHT difficulty   ESC back");
        (void)a;
        gfx::endCanvas();
    }

    TouchLayout touchLayout() const override {
        TouchLayout t;
        t.pad = false;
        if (train_.running.load()) {
            t.keys = {{"STOP", KEY_ESCAPE}};
            return t;
        }
        t.keys = {{"GENERATE", KEY_G}, {"PLAY CHART", KEY_ENTER}, {"SAVE", KEY_S}, {"LISTEN", KEY_SPACE}, {"TRAIN", KEY_T}, {"BACK", KEY_ESCAPE}};
        return t;
    }

private:
    // ------------------------------------------------------------ layout --
    static constexpr float kTopH = 64, kRackW = 380, kRowH = 62, kRulerY = 70, kTempoY = 92, kTracksY = 120, kChartY = 556,
                           kChartH = 88, kBottomY = 652;
    static constexpr int kRows = 7;  // track rows that fit
    float laneX() const { return kRackW + 10; }
    float laneW() const { return gfx::W - laneX() - 10; }
    double xToMs(float x) const { return viewMs_ + double(x - laneX()) * msPerPx_; }
    float msToX(double ms) const { return laneX() + float((ms - viewMs_) / msPerPx_); }
    double songMs() const {
        double len = 0;
        for (const StudioTrack& t : tracks_)
            if (t.audio()) len = std::max(len, double(t.pcm->frames()) * 1000.0 / Mixer::kRate);
        return std::max(len, chart_.lengthMs);
    }

    // ------------------------------------------------------------- input --
    void feedUi() {
        App& a = app();
        ui_.in = &a.input;
        ui_.mouse = gfx::toCanvas(GetMousePosition());
        ui_.click = a.input.mousePressed(MOUSE_BUTTON_LEFT) && !(a.touchMode && a.touch.covers(GetMousePosition()));
        ui_.rclick = a.input.mousePressed(MOUSE_BUTTON_RIGHT);
        ui_.wheel = a.input.wheel();
    }
    void runActions() {
        std::vector<std::function<void()>> act;
        act.swap(actions_);
        for (auto& f : act) f();
    }
    void later(std::function<void()> f) { actions_.push_back(std::move(f)); }

    void playlistMouse() {
        const Vector2 m = ui_.mouse;
        const bool overLanes = m.x >= laneX() && m.y >= kRulerY && m.y < kChartY + kChartH;
        const bool ctrl = app().input.isDown(KEY_LEFT_CONTROL) || app().input.isDown(KEY_RIGHT_CONTROL);
        if (overLanes && ui_.wheel != 0) {
            if (ctrl) {  // zoom around the mouse
                const double at = xToMs(m.x);
                msPerPx_ = std::clamp(msPerPx_ * (ui_.wheel > 0 ? 0.8 : 1.25), 0.5, 400.0);
                viewMs_ = at - double(m.x - laneX()) * msPerPx_;
            } else {
                viewMs_ -= double(ui_.wheel) * 80.0 * msPerPx_;
            }
            viewMs_ = std::clamp(viewMs_, -2000.0, std::max(0.0, songMs() - laneW() * msPerPx_ * 0.5));
        }
        if (m.x < kRackW && m.y >= kTracksY && m.y < kChartY && ui_.wheel != 0)
            rackScroll_ = std::clamp(rackScroll_ - int(ui_.wheel), 0, std::max(0, int(tracks_.size()) - kRows));
        // The ruler: a click moves the song there.
        if (ui_.click && !ui_.used && m.x >= laneX() && m.y >= kRulerY && m.y < kTempoY) {
            playMs_ = std::max(0.0, xToMs(m.x));
            if (app().mixer.playing()) app().mixer.seek(playMs_ / 1000.0);
        }
        // The tempo lane: a click adds a BPM change on the nearest beat, a
        // right click removes the one there, the wheel changes its BPM.
        if (m.x >= laneX() && m.y >= kTempoY && m.y < kTracksY) {
            const int hit = markerAt(m.x);
            if (ui_.click && !ui_.used && hit < 0) {
                const double t = xToMs(m.x);
                const Chart g = grid();
                const double beat = std::round(g.beatAt(t));
                const double at = g.msAtBeat(beat);
                std::lock_guard<std::mutex> lk(mtx_);
                if (tempo_.empty()) tempo_.push_back({std::max(0.0, at), 120, 0});
                else tempo_.push_back({std::max(0.0, at), g.bpmAt(at), 0});
                sortTempo();
                status_ = "BPM change added: the wheel over it changes it, a right click removes it.";
            } else if (ui_.rclick && hit > 0) {
                std::lock_guard<std::mutex> lk(mtx_);
                tempo_.erase(tempo_.begin() + hit);
            } else if (hit >= 0 && ui_.wheel != 0) {
                const bool shift = app().input.isDown(KEY_LEFT_SHIFT) || app().input.isDown(KEY_RIGHT_SHIFT);
                std::lock_guard<std::mutex> lk(mtx_);
                tempo_[size_t(hit)].bpm = std::clamp(tempo_[size_t(hit)].bpm + (ui_.wheel > 0 ? 1 : -1) * (shift ? 0.1 : 1.0), 20.0, 999.0);
                ui_.wheel = 0;
            }
        }
    }
    int markerAt(float x) const {
        for (size_t i = 0; i < tempo_.size(); ++i)
            if (std::fabs(msToX(tempo_[i].timeMs) - x) < 8 + 30) return int(i);
        return -1;
    }
    void sortTempo() {
        std::stable_sort(tempo_.begin(), tempo_.end(), [](const TempoPoint& a, const TempoPoint& b) { return a.timeMs < b.timeMs; });
    }
    Chart grid() const {
        Chart g;
        for (const TempoPoint& t : tempo_) g.tempo.push_back({t.timeMs, t.bpm, 0});
        if (g.tempo.empty()) g.tempo.push_back({0, 120, 0});
        const double first = g.tempo.front().timeMs;
        g.finalize(g.tempo.front().bpm);
        if (first > 0) {
            const double shift = g.beatAt(first);
            for (TempoPoint& tp : g.tempo) tp.beat -= shift;
        }
        return g;
    }

    // ----------------------------------------------------------- actions --
    void togglePlay() {
        App& a = app();
        if (a.mixer.playing()) {
            a.mixer.stop();
            return;
        }
        PcmPtr slot[3];
        for (const StudioTrack& t : tracks_) {
            if (!t.audio() || t.muted) continue;
            const int k = t.role == AutoRole::Player ? 1 : t.role == AutoRole::Opponent ? 2 : 0;
            if (!slot[k]) slot[k] = t.pcm;
            else if (!slot[(k + 1) % 3]) slot[(k + 1) % 3] = t.pcm;
        }
        a.mixer.setTracks(slot[0], slot[1], slot[2]);
        a.mixer.setRate(1.0);
        a.mixer.setLoop(false);
        a.mixer.play(std::max(0.0, playMs_) / 1000.0);
    }

    void generate() {
        if (loading_.load() || generating_.load() || tracks_.empty()) return;
        if (generator_.joinable()) generator_.join();
        AutoChartSpec spec;
        std::vector<std::shared_ptr<const Pcm>> keep;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (const StudioTrack& t : tracks_) {
                AutoTrack at;
                at.name = t.name;
                if (t.audio()) {
                    at.samples = t.pcm->data.data();
                    at.frames = t.pcm->frames();
                    at.channels = t.pcm->channels;
                    at.sampleRate = Mixer::kRate;
                    keep.push_back(t.pcm);
                }
                at.flpNotes = t.flp;
                at.ppqn = t.ppqn;
                at.role = t.role;
                at.lanes = t.lanes;
                at.density = t.density;
                spec.tracks.push_back(std::move(at));
            }
            spec.tempo = tempo_;
        }
        spec.difficulty = diff_;
        spec.seed = std::hash<std::string>()(songId_);
        std::shared_ptr<const AutoModel> model;
        if (useModel_) {
            useAutoChartModel(modelPath_, true);
            model = autoChartModel();
        }
        status_ = "Generating...";
        generating_ = true;
        const std::string title = entry_ ? entry_->title : std::string();
        const std::string diffName = std::string(kDiffNames[diff_]) + " (AutoChart)";
        generator_ = std::thread([this, spec, keep, model, title, diffName]() mutable {
            AutoChartSpec s = std::move(spec);
            s.model = model.get();
            Chart c;
            AutoChartInfo info;
            std::string err;
            const bool ok = autoChartSpec(s, c, &info, &err);
            c.title = title;
            c.difficulty = diffName;
            std::lock_guard<std::mutex> lk(mtx_);
            genOk_ = ok;
            genChart_ = std::move(c);
            genInfo_ = info;
            genErr_ = err;
            generating_ = false;
        });
    }

    // The chart as a .ghac in the cache, played over the song.
    void playChart() {
        if (chart_.notes.empty() || !entry_) {
            status_ = "Generate a chart first (G).";
            return;
        }
        App& a = app();
        std::error_code ec;
        fs::create_directories(pathOf(a.userDir + "cache"), ec);
        const std::string path = a.userDir + "cache/autochart-play" + kAutoChartExt;
        std::string err;
        if (!writeGhn(path, chart_, err)) {
            status_ = "Cannot play it: " + err;
            return;
        }
        a.mixer.stop();
        PlayRequest r;
        r.songId = songId_;
        r.diff = entry_->diffs.empty() ? "Normal" : entry_->diffs.front();
        r.chartFile = path;
        sfxPlay(Sfx::Confirm);
        a.change(makePlay(r));
    }

    // Beside the song's charts as <song>-autochart-<difficulty>.ghac: the song
    // list shows it as a difficulty of its own.
    void save() {
        if (chart_.notes.empty() || !entry_) {
            status_ = "Generate a chart first (G).";
            return;
        }
        if (entry_->builtin || entry_->dataDir.empty()) {
            status_ = "Built-in songs keep their own charts: PLAY CHART plays this one.";
            return;
        }
        std::string key = entry_->songKey.empty() ? entry_->title : entry_->songKey;
        for (char& c : key)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
        const std::string path = entry_->dataDir + key + "-autochart-" + toLower(kDiffNames[diff_]) + kAutoChartExt;
        std::string err;
        if (!writeGhn(path, chart_, err)) {
            status_ = "Not saved: " + err;
            return;
        }
        App& a = app();
        a.library.scan(a.base);
        entry_ = a.library.find(songId_);
        status_ = "Saved " + textOf(pathOf(path).filename()) + ": it plays only in God's Hand, with the shame watermark.";
        sfxPlay(Sfx::Confirm);
    }

    void train() {
        if (trainer_.joinable() || train_.running.load()) return;
        App& a = app();
        a.mixer.stop();
        const std::vector<SongEntry> songs = a.library.songs();
        const std::string path = modelPath_;
        train_.running = true;
        trainer_ = std::thread([this, songs, path] { trainAutoChart(songs, path, train_); });
    }

    // Stems and FL Studio projects, from the system's file manager.
    void addAudio() {
        if (platform::touchFirst()) {
            platform::resetImport();
            picking_ = 1;
            platform::pickFiles("Choose audio files", "*.ogg");
            return;
        }
        addAudioFiles(platform::pickFiles("Choose audio files (stems of the song)", "*.ogg"));
    }
    void addFlp() {
        if (platform::touchFirst()) {
            platform::resetImport();
            picking_ = 2;
            platform::pickFiles("Choose an FL Studio project", "*.flp");
            return;
        }
        const auto files = platform::pickFiles("Choose the song's FL Studio project", "*.flp");
        if (!files.empty()) addFlpFile(files.front());
    }
    void pollPick() {
        if (!picking_) return;
        int count = 0;
        const platform::Import st = platform::importStatus(&count);
        if (st != platform::Import::Done && st != platform::Import::Nothing) return;
        const int kind = picking_;
        picking_ = 0;
        platform::resetImport();
        if (st != platform::Import::Done) return;
        const auto files = platform::pickedFiles();
        if (kind == 1) addAudioFiles(files);
        else if (!files.empty()) addFlpFile(files.front());
    }
    void addAudioFiles(const std::vector<std::string>& files) {
        int added = 0;
        for (const std::string& f : files) {
            auto p = std::make_shared<Pcm>();
            std::string err;
            if (!loadAudioFile(f, *p, err)) {
                status_ = textOf(pathOf(f).filename()) + ": " + err;
                continue;
            }
            StudioTrack t;
            t.name = textOf(pathOf(f).stem());
            t.pcm = p;
            t.role = AutoRole::Turns;
            t.peaks = peaksOf(*p);
            std::lock_guard<std::mutex> lk(mtx_);
            tracks_.push_back(std::move(t));
            ++added;
        }
        if (added) status_ = std::to_string(added) + " sound(s) added: give each a role, then GENERATE.";
    }
    void addFlpFile(const std::string& path) {
        FlpProject p;
        std::string err;
        if (!readFlpFile(path, p, err)) {
            status_ = "FL Studio project: " + err;
            return;
        }
        int added = 0;
        std::vector<StudioTrack> fresh;
        for (const FlpChannel& ch : p.channels) {
            std::vector<FlpNote> notes = flpSongNotes(p, ch.index);
            if (notes.empty()) continue;
            StudioTrack t;
            t.name = (ch.name.empty() ? "Channel " + std::to_string(ch.index + 1) : ch.name) + " (FLP)";
            t.flp = std::move(notes);
            t.ppqn = p.ppqn;
            // The FPC channel with SNIFF's keys is a chart already: read it as one.
            int keys = 0;
            for (const FlpNote& n : t.flp) keys += ((n.pitch >= 48 && n.pitch <= 51) || (n.pitch >= 60 && n.pitch <= 63)) ? 1 : 0;
            t.lanes = ch.index == p.chartChannel && keys * 10 >= int(t.flp.size()) * 9 ? AutoLanes::Keys : AutoLanes::Pitch;
            t.role = AutoRole::Turns;
            fresh.push_back(std::move(t));
            ++added;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        for (StudioTrack& t : fresh) tracks_.push_back(std::move(t));
        if (p.bpm > 0 && tempo_.empty()) tempo_ = {{0, p.bpm, 0}};
        else if (p.bpm > 0 && tempo_.size() == 1) tempo_[0] = {0, p.bpm, 0};  // the project's tempo, from its start
        status_ = added ? std::to_string(added) + " channels of " + textOf(pathOf(path).filename()) + " added (" +
                              std::to_string(int(p.bpm)) + " BPM" + (p.playlist.empty() ? "" : ", as its playlist places them") + ")."
                        : "No notes in that project's channels.";
    }

    // -------------------------------------------------------------- draw --
    bool button(Rectangle r, const std::string& label, bool enabled = true, bool on = false) {
        if (!enabled || train_.running.load() || loading_.load()) {
            ui_.box(r);
            gfx::text(FontId::Ui, label, r.x + r.width * 0.5f, r.y + (r.height - 16) * 0.5f, 16, fade(kIvory, 0.35f), Align::Center);
            return false;
        }
        const bool h = ui_.hover(r);
        ui_.box(r, on, h);
        gfx::text(FontId::Ui, label, r.x + r.width * 0.5f, r.y + (r.height - 16) * 0.5f, 16, kIvory, Align::Center);
        if (h && ui_.click) {
            ui_.used = true;
            return true;
        }
        return false;
    }

    void drawTop() {
        DrawRectangleRec({0, 0, gfx::W, kTopH - 4}, Color{40, 36, 52, 255});
        gfx::text(FontId::Title, "AUTOCHART", 16, 10, 30, kGold, Align::Left, 2.0f);
        gfx::text(FontId::Ui, entry_ ? entry_->title : std::string("?"), 16, 40, 16, fade(kIvory, 0.8f));
        float x = 260;
        if (button({x, 12, 110, 34}, app().mixer.playing() ? "STOP (SPACE)" : "LISTEN (SPACE)")) later([this] { togglePlay(); });
        x += 120;
        gfx::text(FontId::Ui, TextFormat("%d:%05.2f", int(playMs_ / 60000), std::fmod(playMs_ / 1000.0, 60.0)), x, 20, 20, kIvory);
        x += 90;
        if (button({x, 12, 34, 34}, "<")) later([this] { diff_ = (diff_ + 3) % 4; generate(); });
        gfx::text(FontId::Ui, kDiffNames[diff_], x + 90, 20, 20, kGold, Align::Center);
        if (button({x + 146, 12, 34, 34}, ">")) later([this] { diff_ = (diff_ + 1) % 4; generate(); });
        x += 196;
        const double bpm = tempo_.empty() ? 0 : tempo_.front().bpm;
        gfx::text(FontId::Ui, tempo_.empty() ? std::string("BPM: auto") : TextFormat("BPM %.2f%s", bpm, tempo_.size() > 1 ? " +" : ""), x, 20, 20, kIvory);
        x += 150;
        if (button({x, 12, 120, 34}, "DETECT BPM")) later([this] {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                tempo_.clear();
            }
            generate();
        });
        x += 130;
        const bool haveModel = [&] {
            std::error_code ec;
            return fs::exists(pathOf(modelPath_), ec);
        }();
        if (button({x, 12, 200, 34}, haveModel ? (useModel_ ? "TRAINED MODEL: ON" : "TRAINED MODEL: OFF") : "NOT TRAINED YET", haveModel,
                   useModel_ && haveModel))
            later([this] {
                useModel_ = !useModel_;
                generate();
            });
    }

    void drawRack() {
        DrawRectangleRec({0, kTopH, kRackW, kChartY - kTopH}, Color{32, 30, 42, 255});
        gfx::text(FontId::Ui, "CHANNEL RACK", 12, kRulerY + 2, 16, fade(kIvory, 0.6f));
        gfx::text(FontId::Ui, "role / lanes / amount", kRackW - 12, kRulerY + 2, 14, fade(kIvory, 0.45f), Align::Right);
        if (tracks_.empty() && !loading_.load()) gfx::text(FontId::Ui, "No sounds: add audio (A) or an FLP (F).", 12, kTracksY + 10, 16, kIvory);
        for (int r = 0; r < kRows; ++r) {
            const int i = rackScroll_ + r;
            if (i >= int(tracks_.size())) break;
            StudioTrack& t = tracks_[size_t(i)];
            const float y = kTracksY + float(r) * kRowH;
            const bool on = i == sel_;
            DrawRectangleRec({4, y + 2, kRackW - 8, kRowH - 4}, on ? Color{58, 50, 80, 255} : Color{44, 40, 58, 255});
            DrawRectangleRec({4, y + 2, 6, kRowH - 4}, kRoleColors[int(t.role)]);
            std::string name = t.name;
            if (name.size() > 30) name = name.substr(0, 28) + "..";
            gfx::text(FontId::Ui, name, 18, y + 6, 17, t.muted ? fade(kIvory, 0.4f) : kIvory);
            if (ui_.click && !ui_.used && CheckCollisionPointRec(ui_.mouse, {4, y + 2, kRackW - 8, 24})) sel_ = i;
            float x = 18;
            const float by = y + 30;
            if (button({x, by, 116, 26}, autoRoleName(t.role))) later([this, i] {
                std::lock_guard<std::mutex> lk(mtx_);
                StudioTrack& tt = tracks_[size_t(i)];
                tt.role = AutoRole((int(tt.role) + 1) % 4);
            });
            x += 122;
            if (button({x, by, 104, 26}, autoLanesName(t.lanes))) later([this, i] {
                std::lock_guard<std::mutex> lk(mtx_);
                StudioTrack& tt = tracks_[size_t(i)];
                const int n = tt.audio() ? 3 : 4;  // SNIFF keys: FL channels only
                tt.lanes = AutoLanes((int(tt.lanes) + 1) % n);
            });
            x += 110;
            if (button({x, by, 24, 26}, "-")) later([this, i] { tracks_[size_t(i)].density = std::max(0.25f, tracks_[size_t(i)].density - 0.25f); });
            gfx::text(FontId::Ui, TextFormat("%.2fx", t.density), x + 52, by + 5, 15, kIvory, Align::Center);
            if (button({x + 80, by, 24, 26}, "+")) later([this, i] { tracks_[size_t(i)].density = std::min(4.0f, tracks_[size_t(i)].density + 0.25f); });
            if (button({kRackW - 40, y + 6, 28, 22}, "M", true, t.muted)) later([this, i] { tracks_[size_t(i)].muted = !tracks_[size_t(i)].muted; });
        }
        const float by = kChartY - 40;
        if (button({8, by, 120, 32}, "+ AUDIO (A)")) later([this] { addAudio(); });
        if (button({134, by, 110, 32}, "+ FLP (F)")) later([this] { addFlp(); });
        if (button({250, by, 122, 32}, "REMOVE", sel_ >= 0 && sel_ < int(tracks_.size()))) later([this] {
            std::lock_guard<std::mutex> lk(mtx_);
            if (sel_ >= 0 && sel_ < int(tracks_.size())) tracks_.erase(tracks_.begin() + sel_);
            sel_ = std::min(sel_, int(tracks_.size()) - 1);
        });
    }

    void drawPlaylist() {
        const float x0 = laneX(), w = laneW();
        DrawRectangleRec({x0, kRulerY, w, kChartY - kRulerY}, Color{30, 34, 40, 255});
        const Chart g = grid();
        // Bars and beats.
        const double t0 = xToMs(x0), t1 = xToMs(x0 + w);
        const double b0 = std::floor(g.beatAt(t0)), b1 = std::ceil(g.beatAt(t1));
        const double pxPerBeat = 60000.0 / std::max(1.0, g.bpmAt(t0)) / msPerPx_;
        const int every = pxPerBeat < 6 ? 16 : pxPerBeat < 14 ? 4 : 1;
        for (double b = b0; b <= b1; b += 1) {
            const long long bi = std::llround(b);
            if (bi % every) continue;
            const float x = msToX(g.msAtBeat(b));
            const bool bar = bi % 4 == 0;
            DrawLineEx({x, kTracksY}, {x, kChartY + kChartH}, 1, bar ? Color{80, 84, 96, 255} : Color{50, 54, 62, 255});
            if (bar && bi % (every * 4 >= 16 ? 16 : 4) == 0)
                gfx::text(FontId::Ui, std::to_string(bi / 4 + 1), x + 3, kRulerY + 2, 14, fade(kIvory, 0.6f));
        }
        DrawRectangleRec({x0, kTempoY, w, kTracksY - kTempoY - 2}, Color{40, 36, 30, 255});
        gfx::text(FontId::Ui, "TEMPO", x0 + 4, kTempoY + 4, 13, fade(kGold, 0.6f));
        for (size_t i = 0; i < tempo_.size(); ++i) {
            const float x = msToX(tempo_[i].timeMs);
            if (x < x0 - 60 || x > x0 + w) continue;
            DrawTriangle({x, kTempoY + 4}, {x - 6, kTempoY + 18}, {x + 6, kTempoY + 18}, kGold);
            gfx::text(FontId::Ui, TextFormat("%.1f", tempo_[i].bpm), x + 8, kTempoY + 4, 15, kGold);
        }
        // Each sound: its waveform, or an FL channel's notes.
        for (int r = 0; r < kRows; ++r) {
            const int i = rackScroll_ + r;
            if (i >= int(tracks_.size())) break;
            const StudioTrack& t = tracks_[size_t(i)];
            const float y = kTracksY + float(r) * kRowH, mid = y + kRowH * 0.5f;
            const Color c = fade(kRoleColors[int(t.role)], t.muted ? 0.3f : 0.9f);
            DrawLineEx({x0, y + kRowH - 1}, {x0 + w, y + kRowH - 1}, 1, Color{20, 22, 28, 255});
            if (t.audio()) {
                for (float x = x0; x < x0 + w; x += 2) {
                    const double ms = xToMs(x);
                    if (ms < 0) continue;
                    const size_t k0 = size_t(ms / 10.0), k1 = std::max(k0 + 1, size_t((ms + msPerPx_ * 2) / 10.0));
                    float m = 0;
                    for (size_t k = k0; k < k1 && k < t.peaks.size(); ++k) m = std::max(m, t.peaks[k]);
                    const float h = m * (kRowH * 0.45f);
                    if (h > 0.5f) DrawLineEx({x, mid - h}, {x, mid + h}, 2, c);
                }
            } else {
                uint32_t lo = 127, hi = 0;
                for (const FlpNote& n : t.flp) {
                    lo = std::min(lo, n.pitch);
                    hi = std::max(hi, n.pitch);
                }
                const double span = std::max(1.0, double(hi - lo));
                for (const FlpNote& n : t.flp) {
                    const double beat = double(n.time) / std::max(1, t.ppqn);
                    const double ms = g.msAtBeat(beat);
                    const float x = msToX(ms);
                    if (x > x0 + w) break;
                    const float xe = msToX(g.msAtBeat(beat + double(std::max<uint32_t>(n.duration, 1)) / std::max(1, t.ppqn)));
                    if (xe < x0) continue;
                    const float ny = y + 4 + float((hi - n.pitch) / span) * (kRowH - 12);
                    DrawRectangleRec({std::max(x, x0), ny, std::max(2.0f, xe - std::max(x, x0)), 4}, c);
                }
            }
        }
        // The song's position.
        const float px = msToX(playMs_);
        if (px >= x0 && px <= x0 + w) DrawLineEx({px, kRulerY}, {px, kChartY + kChartH}, 2, Color{255, 255, 255, 200});
    }

    void drawChartLane() {
        const float x0 = laneX(), w = laneW();
        DrawRectangleRec({x0, kChartY, w, kChartH}, Color{18, 18, 24, 255});
        DrawRectangleRec({0, kChartY, kRackW, kChartH}, Color{32, 30, 42, 255});
        gfx::text(FontId::Ui, "THE CHART", 12, kChartY + 6, 16, fade(kIvory, 0.6f));
        gfx::text(FontId::Ui, "opponent", 12, kChartY + 28, 14, Color{255, 110, 110, 220});
        gfx::text(FontId::Ui, "player", 12, kChartY + 28 + kChartH * 0.5f - 6, 14, Color{80, 170, 255, 220});
        const float rowH = (kChartH - 8) / 8.0f;
        const auto first = std::lower_bound(chart_.notes.begin(), chart_.notes.end(), xToMs(x0) - 500,
                                            [](const Note& n, double t) { return n.time < t; });
        for (auto it = first; it != chart_.notes.end(); ++it) {
            const float x = msToX(it->time);
            if (x > x0 + w) break;
            const int row = (it->side ? 4 : 0) + (it->lane & 3);
            const float y = kChartY + 4 + float(row) * rowH;
            const float len = it->length > 0 ? float(it->length / msPerPx_) : 0.0f;
            if (len > 0) DrawRectangleRec({x, y + rowH * 0.35f, len, rowH * 0.3f}, fade(kLaneColors[it->lane & 3], 0.5f));
            DrawRectangleRec({x - 1, y, 3, rowH - 1}, kLaneColors[it->lane & 3]);
        }
        DrawLineEx({x0, kChartY + kChartH * 0.5f}, {x0 + w, kChartY + kChartH * 0.5f}, 1, Color{60, 60, 70, 255});
    }

    void drawBottom() {
        DrawRectangleRec({0, kBottomY - 6, gfx::W, gfx::H - kBottomY + 6}, Color{40, 36, 52, 255});
        float x = 10;
        const bool can = !chart_.notes.empty();
        if (button({x, kBottomY, 140, 38}, "GENERATE (G)", !tracks_.empty())) later([this] { generate(); });
        x += 148;
        if (button({x, kBottomY, 170, 38}, "PLAY CHART (ENTER)", can)) later([this] { playChart(); });
        x += 178;
        if (button({x, kBottomY, 140, 38}, "SAVE .ghac (S)", can && entry_ && !entry_->builtin)) later([this] { save(); });
        x += 148;
        if (button({x, kBottomY, 120, 38}, "TRAIN (T)")) later([this] { train(); });
        x += 130;
        std::string s = generating_.load() ? std::string("Generating...") : status_;
        if (s.size() > 80) s = s.substr(0, 78) + "..";
        gfx::text(FontId::Ui, s, x, kBottomY + 10, 16, s.rfind("No chart", 0) == 0 || s.rfind("Not saved", 0) == 0 ? kBad : kIvory);
    }

    void drawTraining() {
        DrawRectangleRec({0, 0, gfx::W, gfx::H}, fade(BLACK, 0.7f));
        const Rectangle r{gfx::W / 2 - 330, gfx::H / 2 - 130, 660, 250};
        DrawRectangleRounded(r, 0.06f, 8, Color{36, 32, 48, 255});
        DrawRectangleRoundedLinesEx(r, 0.06f, 8, 2, kGold);
        gfx::text(FontId::Title, "TRAINING AUTOCHART", r.x + r.width / 2, r.y + 16, 28, kGold, Align::Center, 2.0f);
        const int done = train_.done.load(), total = std::max(1, train_.total.load());
        std::string cur;
        {
            std::lock_guard<std::mutex> lk(train_.mtx);
            cur = train_.current;
        }
        gfx::text(FontId::Ui, "Learning from every charted song of your mods: where their notes go for each sound, whose they are,", r.x + 20,
                  r.y + 62, 15, fade(kIvory, 0.8f));
        gfx::text(FontId::Ui, "which lane, how many. Stop at any time: what it learned is kept, and the next run goes on from there.",
                  r.x + 20, r.y + 80, 15, fade(kIvory, 0.8f));
        DrawRectangleRec({r.x + 20, r.y + 112, r.width - 40, 18}, fade(BLACK, 0.5f));
        DrawRectangleRec({r.x + 20, r.y + 112, (r.width - 40) * float(done) / float(total), 18}, kGold);
        gfx::text(FontId::Ui, TextFormat("%d / %d songs   learned %d   skipped %d   note frames %llu", done, train_.total.load(),
                                         train_.learned.load(), train_.skipped.load(), train_.notes.load()),
                  r.x + 20, r.y + 138, 17, kIvory);
        gfx::text(FontId::Ui, cur.empty() ? std::string("Finding the songs...") : "Now: " + cur, r.x + 20, r.y + 164, 17, fade(kIvory, 0.75f));
        gfx::text(FontId::Ui, "ESC stops", r.x + r.width / 2, r.y + 206, 16, fade(kIvory, 0.6f), Align::Center);
    }

    std::string songId_;
    const SongEntry* entry_ = nullptr;
    int diff_ = 1, sel_ = 0, rackScroll_ = 0, picking_ = 0;
    std::vector<StudioTrack> tracks_;
    std::vector<TempoPoint> tempo_;
    Chart chart_, genChart_;
    AutoChartInfo genInfo_;
    bool genOk_ = false, useModel_ = true, autoGenerate_ = false, studioSaved_ = false;
    std::string genErr_, status_, modelPath_;
    double viewMs_ = -500, msPerPx_ = 20, playMs_ = 0;
    Ui ui_;
    std::vector<std::function<void()>> actions_;
    std::mutex mtx_;
    std::thread loader_, generator_, trainer_;
    std::atomic<bool> loading_{false}, generating_{false};
    TrainProgress train_;
};

}  // namespace

std::unique_ptr<State> makeAutoChartStudio(const std::string& songId, int difficulty) {
    return std::make_unique<AutoChartStudio>(songId, difficulty);
}

}  // namespace gh
