// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "autochart_train.hpp"

#include <algorithm>
#include <functional>

#include "audio.hpp"
#include "autochart.hpp"
#include "chart.hpp"
#include "chart_stream.hpp"
#include "common.hpp"

namespace gh {

std::vector<const SongEntry*> trainableSongs(const std::vector<SongEntry>& songs) {
    std::vector<const SongEntry*> out;
    for (const SongEntry& e : songs) {
        if (e.builtin || e.autoChart || e.inst.empty() || e.chartPaths.empty()) continue;
        bool any = false;
        for (const std::string& p : e.chartPaths) any = any || !p.empty();
        if (any) out.push_back(&e);
    }
    return out;
}

namespace {

// The difficulty to learn from: Hard, else Normal, else the last one listed.
size_t pickDiff(const SongEntry& e) {
    size_t best = e.diffs.empty() ? 0 : e.diffs.size() - 1;
    for (size_t i = 0; i < e.diffs.size(); ++i)
        if (equalsI(e.diffs[i], "Normal")) best = i;
    for (size_t i = 0; i < e.diffs.size(); ++i)
        if (equalsI(e.diffs[i], "Hard")) best = i;
    return best;
}

AutoSound soundOf(const Pcm& p) { return {p.data.data(), p.frames(), p.channels, Mixer::kRate}; }

}  // namespace

int trainAutoChart(const std::vector<SongEntry>& songs, const std::string& modelPath, TrainProgress& prog) {
    prog.running = true;
    prog.stop = false;
    AutoModel model;
    model.load(modelPath);  // none yet: a new one
    std::vector<const SongEntry*> todo;
    for (const SongEntry* e : trainableSongs(songs))
        if (!model.knows(e->id)) todo.push_back(e);
    prog.total = int(todo.size());
    prog.done = prog.learned = prog.skipped = 0;
    prog.notes = model.notes;
    int learned = 0, sinceSave = 0;
    std::string why;
    for (const SongEntry* e : todo) {
        if (prog.stop.load()) break;
        {
            std::lock_guard<std::mutex> lk(prog.mtx);
            prog.current = e->title;
        }
        const size_t d = pickDiff(*e);
        Chart chart;
        std::string err, meta;
        bool ok = d < e->chartPaths.size() && !e->chartPaths[d].empty();
        if (ok && !e->metaPath.empty()) readTextFile(e->metaPath, meta);
        ok = ok && loadChartFile(e->chartPaths[d], e->diffs.empty() ? "Normal" : e->diffs[d], chart, err, meta, 0, true);
        // Spam charts (more than 25 notes a second) put a note on every
        // moment: there is nothing to learn about where notes go.
        const double secs = std::max(1.0, chart.lengthMs / 1000.0);
        const double nps = double((chart.playerNotes + chart.opponentNotes).sat64()) / secs;
        ok = ok && !chart.notes.empty() && !chart.autoCharted && nps <= 25.0;
        Pcm inst, vp, vo, shared;
        if (ok) ok = loadAudioFile(e->inst, inst, err);
        if (ok) {
            if (!e->voicesP.empty()) loadAudioFile(e->voicesP, vp, err);
            if (!e->voicesO.empty()) loadAudioFile(e->voicesO, vo, err);
            if (vp.data.empty() && vo.data.empty() && !e->voices.empty()) loadAudioFile(e->voices, shared, err);
        }
        if (ok) {
            const AutoSound si = soundOf(inst);
            const AutoSound sp = !vp.data.empty() ? soundOf(vp) : !shared.data.empty() ? soundOf(shared) : AutoSound{};
            const AutoSound so = !vo.data.empty() ? soundOf(vo) : !shared.data.empty() ? soundOf(shared) : AutoSound{};
            const size_t got = autoLearnSong(model, chart, si, sp, so, std::hash<std::string>()(e->id));
            if (got > 0) {
                ++learned;
                ++sinceSave;
                ++prog.learned;
                prog.notes = model.notes;
            } else {
                ++prog.skipped;
            }
        } else {
            ++prog.skipped;
        }
        model.songs.push_back(e->id);  // learned, or not learnable: not tried again
        ++prog.done;
        if (sinceSave >= 5) {
            model.save(modelPath, &why);
            sinceSave = 0;
        }
    }
    const bool saved = model.save(modelPath, &why);
    {
        std::lock_guard<std::mutex> lk(prog.mtx);
        prog.current.clear();
        prog.message = !saved ? why
                       : prog.stop.load() ? "Stopped: what it learned so far is kept."
                                          : "Done: " + std::to_string(learned) + " songs learned.";
    }
    prog.running = false;
    return learned;
}

}  // namespace gh
