// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through
// issues.

#include "json_merge.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <tuple>

#include "chart_stream.hpp"
#include "common.hpp"
#include "json.hpp"
#include "psych.hpp"

namespace gh {

namespace {

namespace fs = std::filesystem;
fs::path u8p(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// A part, read: its JSON without the notes (the skeleton), the notes, and its
// sections with when they start and end.
struct Loaded {
    std::string skeleton;
    StreamedNotes sn;
    json::Document doc;
    const json::Value* song = nullptr;  // the object holding "notes"
    bool rootSong = false;              // Psych 1.0: the fields are on the root
    bool v1 = false;                    // psych_v1: a note's value does not depend on its section
    int keys = 4;
    double bpm = 120;
    struct Sec {
        bool mustHit = true, gf = false, alt = false, changeBPM = false;
        double bpm = 0, beats = 4;
        int stream = -1;
        double startMs = 0, endMs = 0, bpmInForce = 120;
    };
    std::vector<Sec> secs;
    std::vector<psych::ChartEvent> events;

    size_t noteCount = 0;  // sn.notes.size() while they are in memory

    size_t notesOf(int stream, size_t& end) const {
        if (stream < 0 || size_t(stream) >= sn.rangeStart.size()) {
            end = 0;
            return 0;
        }
        const size_t from = sn.rangeStart[size_t(stream)];
        end = size_t(stream) + 1 < sn.rangeStart.size() ? sn.rangeStart[size_t(stream) + 1] : noteCount;
        return from;
    }
};

bool load(const std::string& path, Loaded& L, std::string& err) {
    if (!streamChartSkeleton(path, L.skeleton, L.sn, err)) return false;
    L.noteCount = L.sn.notes.size();
    if (!L.doc.parse(L.skeleton, &err)) return false;
    const json::Value& root = L.doc.root();
    const json::Value* song = L.doc.get(root, "song");
    L.rootSong = !(song && song->type == json::Type::Object);
    L.song = L.rootSong ? &root : song;
    const std::string fmt(L.doc.strOr(L.doc.get(*L.song, "format"), L.doc.strOr(L.doc.get(root, "format"), "")));
    L.v1 = fmt.rfind("psych_v1", 0) == 0;
    L.bpm = json::Document::num(L.doc.get(*L.song, "bpm"), 120);
    if (!(L.bpm > 0)) L.bpm = 120;
    const int keyCount = int(json::Document::num(L.doc.get(*L.song, "keyCount"), 0));
    L.keys = keyCount > 0 ? keyCount : std::max(4, (L.sn.maxData + 2) / 2);
    const json::Value* notes = L.doc.get(*L.song, "notes");
    if (!notes || notes->type != json::Type::Array) {
        err = "no sections (\"notes\")";
        return false;
    }
    double t = 0, cur = L.bpm;
    for (size_t i = 0; i < L.doc.size(*notes); ++i) {
        const json::Value& s = L.doc.at(*notes, i);
        Loaded::Sec sec;
        sec.mustHit = json::Document::boolean(L.doc.get(s, "mustHitSection"), true);
        sec.gf = json::Document::boolean(L.doc.get(s, "gfSection"), false);
        sec.alt = json::Document::boolean(L.doc.get(s, "altAnim"), false);
        sec.changeBPM = json::Document::boolean(L.doc.get(s, "changeBPM"), false);
        sec.bpm = json::Document::num(L.doc.get(s, "bpm"), 0);
        if (const json::Value* sb = L.doc.get(s, "sectionBeats")) sec.beats = json::Document::num(sb, 4);
        else if (const json::Value* ls = L.doc.get(s, "lengthInSteps")) sec.beats = json::Document::num(ls, 16) / 4.0;
        if (!(sec.beats > 0)) sec.beats = 4;
        if (const json::Value* sn = L.doc.get(s, "sectionNotes"); sn && L.doc.size(*sn) > 0)
            sec.stream = int(json::Document::num(L.doc.get(L.doc.at(*sn, 0), "__gh_stream"), -1));
        if (sec.changeBPM && sec.bpm > 0) cur = sec.bpm;
        sec.bpmInForce = cur;
        sec.startMs = t;
        t += sec.beats * 60000.0 / cur;
        sec.endMs = t;
        L.secs.push_back(sec);
    }
    psych::parseEvents(L.doc, L.events);
    return true;
}

// Numbers as short as they read back exactly.
std::string fmtNum(double v) {
    char b[40];
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        std::snprintf(b, sizeof b, "%.0f", v);
        return b;
    }
    for (int p = 15; p <= 17; ++p) {
        std::snprintf(b, sizeof b, "%.*g", p, v);
        if (std::strtod(b, nullptr) == v) break;
    }
    return b;
}

std::string quote(std::string_view s) {
    std::string o = "\"";
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (u < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", u);
            o += b;
        } else o += c;
    }
    return o + "\"";
}

void writeValue(const json::Document& d, const json::Value& v, std::string& o) {
    switch (v.type) {
        case json::Type::Null: o += "null"; break;
        case json::Type::Bool: o += v.b ? "true" : "false"; break;
        case json::Type::Number: o += fmtNum(v.num); break;
        case json::Type::String: o += quote(d.str(v)); break;
        case json::Type::Array:
            o += '[';
            for (size_t i = 0; i < d.size(v); ++i) {
                if (i) o += ',';
                writeValue(d, d.at(v, i), o);
            }
            o += ']';
            break;
        case json::Type::Object:
            o += '{';
            for (size_t i = 0; i < d.size(v); ++i) {
                if (i) o += ',';
                o += quote(d.keyAt(v, i)) + ':';
                writeValue(d, d.valAt(v, i), o);
            }
            o += '}';
            break;
    }
}

// The merged chart, written as it is made: through a buffer, to a file
// that only takes the chart's name once it is whole.
class Out {
public:
    explicit Out(const std::string& path) : path_(path), tmp_(path + ".part"), f_(std::fopen(tmp_.c_str(), "wb")) {}
    ~Out() {
        if (f_) {
            std::fclose(f_);
            std::error_code ec;
            fs::remove(u8p(tmp_), ec);
        }
    }
    bool ok() const { return f_ && ok_; }
    void put(std::string_view s) {
        buf_.append(s.data(), s.size());
        if (buf_.size() >= (size_t(8) << 20)) flush();
    }
    void flush() {
        if (f_ && !buf_.empty() && std::fwrite(buf_.data(), 1, buf_.size(), f_) != buf_.size()) ok_ = false;
        bytes_ += buf_.size();
        buf_.clear();
    }
    bool finish() {
        flush();
        const bool closed = f_ && std::fclose(f_) == 0;
        f_ = nullptr;
        if (!closed || !ok_) return false;
        std::error_code ec;
        fs::rename(u8p(tmp_), u8p(path_), ec);
        return !ec;
    }
    uint64_t bytes() const { return bytes_; }

private:
    std::string path_, tmp_;
    FILE* f_ = nullptr;
    std::string buf_;
    bool ok_ = true;
    uint64_t bytes_ = 0;
};

bool seekTo(FILE* f, uint64_t off) {
#if defined(_WIN32)
    return _fseeki64(f, (long long)off, SEEK_SET) == 0;
#else
    return fseeko(f, off_t(off), SEEK_SET) == 0;
#endif
}

// A part's notes, on disk while the merged chart is written, so parts of any
// size merge in the memory of one part. Read back a section at a time, in
// order.
class Spill {
public:
    explicit Spill(std::string path) : path_(std::move(path)), f_(std::fopen(path_.c_str(), "w+b")) {}
    ~Spill() {
        if (f_) std::fclose(f_);
        std::error_code ec;
        fs::remove(u8p(path_), ec);
    }
    Spill(const Spill&) = delete;
    Spill& operator=(const Spill&) = delete;
    bool write(const std::vector<StreamedNote>& v) {
        if (!f_) return false;
        const size_t chunk = size_t(1) << 20;
        for (size_t i = 0; i < v.size(); i += chunk) {
            const size_t n = std::min(chunk, v.size() - i);
            if (std::fwrite(v.data() + i, sizeof(StreamedNote), n, f_) != n) return false;
        }
        pos_ = v.size();
        return std::fflush(f_) == 0;
    }
    bool read(size_t from, size_t to, std::vector<StreamedNote>& buf) {
        buf.resize(to - from);
        if (to == from) return true;
        if (pos_ != from && !seekTo(f_, uint64_t(from) * sizeof(StreamedNote))) return false;
        pos_ = to;
        return std::fread(buf.data(), sizeof(StreamedNote), buf.size(), f_) == buf.size();
    }

private:
    std::string path_;
    FILE* f_ = nullptr;
    size_t pos_ = 0;
};

// A note as Psych writes it, its stacked notes each written again (every
// one of them counts).
void writeNote(std::string& o, bool& first, const StreamedNotes& sn, const StreamedNote& n, int data, double shiftMs) {
    std::string one = "[" + fmtNum(n.time + shiftMs) + "," + std::to_string(data) + "," + fmtNum(double(n.length));
    if (n.typeIdx() && n.typeIdx() <= sn.types.size()) one += "," + quote(sn.types[n.typeIdx() - 1]);
    else if (n.alt()) one += ",true";
    one += "]";
    for (unsigned k = 0; k < std::max<unsigned>(1, n.stack); ++k) {
        if (!first) o += ',';
        first = false;
        o += one;
    }
}

void writeSectionHead(std::string& o, const Loaded::Sec& s) {
    o += "],\"mustHitSection\":";
    o += s.mustHit ? "true" : "false";
    o += ",\"gfSection\":";
    o += s.gf ? "true" : "false";
    o += ",\"altAnim\":";
    o += s.alt ? "true" : "false";
    o += ",\"changeBPM\":";
    o += s.changeBPM ? "true" : "false";
    o += ",\"bpm\":" + fmtNum(s.changeBPM ? s.bpm : s.bpmInForce) + ",\"sectionBeats\":" + fmtNum(s.beats) +
         ",\"lengthInSteps\":" + fmtNum(s.beats * 4) + ",\"typeOfSection\":0}";
}

std::string fileStem(const std::string& path) {
    const auto s = u8p(path).stem().u8string();
    return std::string(s.begin(), s.end());
}

}  // namespace

size_t g_mergeSpillAbove = size_t(1) << 20;

const char* mergeModeName(MergeMode m) {
    switch (m) {
        case MergeMode::Overlay: return "Overlay (each part holds some notes of the whole song)";
        case MergeMode::Append: return "Append (each part follows the one before)";
        default: return "Auto";
    }
}

MergePart inspectPart(const std::string& path) {
    MergePart p;
    p.path = path;
    const auto fn = u8p(path).filename().u8string();
    p.name = fn.empty() ? path : std::string(fn.begin(), fn.end());
    p.bytes = fileSize(path);
    Loaded L;
    if (!load(path, L, p.err)) return p;
    p.sections = L.secs.size();
    p.spanMs = L.secs.empty() ? 0 : L.secs.back().endMs;
    p.song = std::string(L.doc.strOr(L.doc.get(*L.song, "song"), ""));
    bool any = false;
    for (const StreamedNote& n : L.sn.notes) {
        p.notes += std::max<unsigned>(1, n.stack);
        if (!any || n.time < p.firstMs) p.firstMs = n.time;
        if (!any || n.time > p.lastMs) p.lastMs = n.time;
        any = true;
    }
    p.ok = true;
    return p;
}

void orderParts(std::vector<MergePart>& parts) {
    // The number that ends a name ("song-12" -> 12), and the name before it.
    auto key = [](const std::string& name) {
        std::string stem = name.substr(0, name.rfind('.'));
        size_t e = stem.size();
        while (e > 0 && !std::isdigit(static_cast<unsigned char>(stem[e - 1]))) --e;  // "song-2 (copy)" -> "song-2"
        size_t b = e;
        while (b > 0 && std::isdigit(static_cast<unsigned char>(stem[b - 1]))) --b;
        const bool has = b < e;
        const unsigned long long n = has ? std::strtoull(stem.substr(b, e - b).c_str(), nullptr, 10) : 0;
        return std::make_tuple(toLower(stem.substr(0, b)), has ? 1 : 0, n, toLower(stem));
    };
    std::stable_sort(parts.begin(), parts.end(), [&](const MergePart& a, const MergePart& b) { return key(a.name) < key(b.name); });
}

MergeMode detectMergeMode(const std::vector<MergePart>& parts) {
    if (parts.size() < 2) return MergeMode::Overlay;
    bool sameSections = true, inOwn = true, sequential = true;
    for (size_t i = 0; i < parts.size(); ++i) {
        const MergePart& p = parts[i];
        if (p.sections != parts[0].sections) sameSections = false;
        if (p.notes && p.lastMs > p.spanMs + 1.0) inOwn = false;
        if (i > 0 && p.notes && parts[i - 1].notes && p.firstMs < parts[i - 1].lastMs - 1.0) sequential = false;
    }
    if (!inOwn && sequential) return MergeMode::Append;  // slices of the song, its times kept
    if (sameSections) return MergeMode::Overlay;
    return MergeMode::Append;  // each part its own stretch, from 0
}

bool mergeChartParts(const std::vector<std::string>& parts, const std::string& out, MergeMode mode, MergeResult& res,
                     std::string& err, std::atomic<float>* progress, const std::atomic<bool>* cancel) {
    const auto t0 = std::chrono::steady_clock::now();
    res = MergeResult{};
    if (parts.empty()) {
        err = "no parts";
        return false;
    }
    auto step = [&](float f) {
        if (progress) progress->store(f);
    };
    auto cancelled = [&] {
        if (cancel && cancel->load()) {
            err = "cancelled";
            return true;
        }
        return false;
    };
    uint64_t totalBytes = 0, readBytes = 0;
    for (const std::string& p : parts) totalBytes += std::max<uint64_t>(1, fileSize(p));
    // Every part is read, one at a time. A big part's notes (16 bytes each)
    // wait on disk next to the merged chart; the rest of a part (its JSON
    // without the notes) is small.
    std::vector<std::unique_ptr<Loaded>> L;
    std::vector<std::unique_ptr<Spill>> spills(parts.size());
    std::vector<MergePart> info(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
        if (cancelled()) return false;
        L.push_back(std::make_unique<Loaded>());
        Loaded& l = *L.back();
        if (!load(parts[i], l, err)) {
            err = fileStem(parts[i]) + ": " + err;
            return false;
        }
        if (l.v1 != L[0]->v1) {
            err = "the parts are in different chart formats (Psych 1.0 and older)";
            return false;
        }
        MergePart& p = info[i];
        p.sections = l.secs.size();
        p.spanMs = l.secs.empty() ? 0 : l.secs.back().endMs;
        bool any = false;
        for (const StreamedNote& n : l.sn.notes) {
            p.notes += std::max<unsigned>(1, n.stack);
            if (!any || n.time < p.firstMs) p.firstMs = n.time;
            if (!any || n.time > p.lastMs) p.lastMs = n.time;
            any = true;
        }
        if (l.sn.notes.size() > g_mergeSpillAbove) {
            spills[i] = std::make_unique<Spill>(out + ".notes" + std::to_string(i) + ".tmp");
            if (!spills[i]->write(l.sn.notes)) {
                err = "cannot write a temporary file next to " + out + " (is the disk full?)";
                return false;
            }
            std::vector<StreamedNote>().swap(l.sn.notes);
        }
        readBytes += std::max<uint64_t>(1, fileSize(parts[i]));
        step(0.6f * float(double(readBytes) / double(totalBytes)));
    }
    if (mode == MergeMode::Auto) mode = detectMergeMode(info);
    res.mode = mode;
    // The notes [from, to) of part i, from memory or from its spill.
    std::vector<StreamedNote> buf;
    auto range = [&](size_t i, size_t from, size_t to) -> const StreamedNote* {
        if (!spills[i]) return L[i]->sn.notes.data() + from;
        if (!spills[i]->read(from, to, buf)) return nullptr;
        return buf.data();
    };

    Out o(out);
    if (!o.ok()) {
        err = "cannot write " + out;
        return false;
    }
    const Loaded& head = *L[0];
    // The song's fields, from the first part.
    std::string s = head.rootSong ? "{" : "{\"song\":{";
    {
        bool first = true;
        for (size_t i = 0; i < head.doc.size(*head.song); ++i) {
            const std::string_view k = head.doc.keyAt(*head.song, i);
            if (k == "notes" || k == "events") continue;
            if (!first) s += ',';
            first = false;
            s += quote(k) + ':';
            writeValue(head.doc, head.doc.valAt(*head.song, i), s);
        }
        s += std::string(first ? "" : ",") + "\"notes\":[";
    }
    o.put(s);
    s.clear();
    std::vector<psych::ChartEvent> events;
    size_t sectionsOut = 0;
    bool readOk = true;
    auto flip = [](int d, int k) { return d < k ? d + k : d < 2 * k ? d - k : d; };
    auto drain = [&] {
        if (s.size() >= (size_t(4) << 20)) {
            o.put(s);
            s.clear();
        }
    };
    if (mode == MergeMode::Overlay) {
        size_t count = 0;
        for (const auto& l : L) count = std::max(count, l->secs.size());
        for (size_t i = 0; i < count && readOk; ++i) {
            if ((i & 255) == 0 && cancelled()) return false;
            const Loaded::Sec* h = nullptr;
            for (const auto& l : L)
                if (i < l->secs.size()) {
                    h = &l->secs[i];
                    break;
                }
            s += sectionsOut++ ? ",{\"sectionNotes\":[" : "{\"sectionNotes\":[";
            bool first = true;
            for (size_t p = 0; p < L.size(); ++p) {
                const Loaded& l = *L[p];
                if (i >= l.secs.size()) continue;
                const Loaded::Sec& sec = l.secs[i];
                size_t end = 0;
                const size_t from = l.notesOf(sec.stream, end);
                if (from >= end) continue;
                const StreamedNote* notes = range(p, from, end);
                if (!notes) {
                    readOk = false;
                    break;
                }
                for (size_t n = 0; n < end - from; ++n) {
                    int d = notes[n].noteData();
                    if (!l.v1 && sec.mustHit != h->mustHit) d = flip(d, l.keys);  // the same side as before
                    writeNote(s, first, l.sn, notes[n], d, 0);
                    res.notes += std::max<unsigned>(1, notes[n].stack);
                    drain();
                }
            }
            writeSectionHead(s, *h);
            drain();
            step(0.6f + 0.4f * float(i + 1) / float(std::max<size_t>(1, count)));
        }
        // The same event in several parts (each part a copy of the song's events) once.
        std::set<std::tuple<double, std::string, std::string, std::string>> seen;
        for (const auto& l : L)
            for (const psych::ChartEvent& e : l->events)
                if (seen.insert({e.timeMs, e.name, e.v1, e.v2}).second) events.push_back(e);
    } else {
        double end = 0, bpmInForce = head.bpm;
        for (size_t p = 0; p < L.size() && readOk; ++p) {
            const Loaded& l = *L[p];
            // A part whose notes start again from 0 goes where the parts before it end.
            const double shift = (p > 0 && info[p].notes && info[p].firstMs < end - 1.0) ? end : 0.0;
            for (size_t i = 0; i < l.secs.size() && readOk; ++i) {
                if ((i & 255) == 0 && cancelled()) return false;
                Loaded::Sec sec = l.secs[i];
                if (p > 0 && i == 0 && std::fabs(sec.bpmInForce - bpmInForce) > 1e-9) {
                    sec.changeBPM = true;  // the part's own tempo, where it starts
                    sec.bpm = sec.bpmInForce;
                }
                bpmInForce = sec.bpmInForce;
                s += sectionsOut++ ? ",{\"sectionNotes\":[" : "{\"sectionNotes\":[";
                bool first = true;
                size_t e = 0;
                const size_t from = l.notesOf(sec.stream, e);
                if (from < e) {
                    const StreamedNote* notes = range(p, from, e);
                    if (!notes) {
                        readOk = false;
                        break;
                    }
                    for (size_t n = 0; n < e - from; ++n) {
                        writeNote(s, first, l.sn, notes[n], notes[n].noteData(), shift);
                        res.notes += std::max<unsigned>(1, notes[n].stack);
                        drain();
                    }
                }
                writeSectionHead(s, sec);
                drain();
            }
            for (psych::ChartEvent e : l.events) {
                e.timeMs += shift;
                events.push_back(e);
            }
            end += l.secs.empty() ? 0 : l.secs.back().endMs;
            step(0.6f + 0.4f * float(p + 1) / float(L.size()));
        }
    }
    if (!readOk) {
        err = "cannot read back a temporary file next to " + out;
        return false;
    }
    std::stable_sort(events.begin(), events.end(), [](const psych::ChartEvent& a, const psych::ChartEvent& b) { return a.timeMs < b.timeMs; });
    s += "],\"events\":[";
    for (size_t i = 0; i < events.size();) {
        size_t j = i;
        s += std::string(i ? "," : "") + "[" + fmtNum(events[i].timeMs) + ",[";
        for (; j < events.size() && events[j].timeMs == events[i].timeMs; ++j)
            s += std::string(j > i ? "," : "") + "[" + quote(events[j].name) + "," + quote(events[j].v1) + "," + quote(events[j].v2) + "]";
        s += "]]";
        i = j;
        drain();
    }
    s += head.rootSong ? "]}\n" : "]}}\n";
    o.put(s);
    res.sections = sectionsOut;
    res.events = events.size();
    if (!o.finish()) {
        err = "cannot write " + out + " (is the disk full?)";
        return false;
    }
    res.bytes = fileSize(out);
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    step(1.0f);
    return true;
}

int mergeChartFiles(const std::string& out, const std::vector<std::string>& parts, const std::string& mode) {
    if (parts.empty()) {
        std::fprintf(stderr, "usage: godshand --merge <out.json> <part> <part> ... [--merge-mode auto|overlay|append]\n");
        return 2;
    }
    const std::string m = toLower(mode);
    const MergeMode mm = m == "overlay" ? MergeMode::Overlay : m == "append" ? MergeMode::Append : MergeMode::Auto;
    MergeResult res;
    std::string err;
    if (!mergeChartParts(parts, out, mm, res, err)) {
        std::fprintf(stderr, "merge failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s: %llu notes, %zu sections, %zu events, %llu bytes (%s), %.2f s\n", out.c_str(), (unsigned long long)res.notes,
                res.sections, res.events, (unsigned long long)res.bytes, res.mode == MergeMode::Overlay ? "overlay" : "append",
                res.seconds);
    return 0;
}

}  // namespace gh
