// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "thr_chart.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "common.hpp"
#include "json.hpp"
#include "keys.hpp"
#include "miniz.h"
#include "zstd.h"

namespace gh {

namespace {

constexpr uint32_t kMagicNotes = 0x4E4C4254u;     // "TBLN"
constexpr uint32_t kMagicSections = 0x534C4254u;  // "TBLS"
constexpr size_t kHeader = 32, kNoteRec = 16, kSecRec = 40, kFilterBlock = 65536;
constexpr uint32_t kFiltered = 1, kSecMustHit = 1;
constexpr mz_uint kMethodZstd = 93, kMethodLzma = 14;

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
double rdf64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | p[i];
    double d;
    std::memcpy(&d, &v, 8);
    return d;
}
float rdf32(const uint8_t* p) {
    const uint32_t v = rd32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
void wr16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
void wrf64(uint8_t* p, double d) {
    uint64_t v;
    std::memcpy(&v, &d, 8);
    for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i));
}
void wrf32(uint8_t* p, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    wr32(p, v);
}

// Byte planes back to records, the first 8 bytes summed back from their differences.
void unfilter(const uint8_t* src, uint8_t* dst, size_t records, size_t rec) {
    for (size_t done = 0; done < records;) {
        const size_t n = std::min(kFilterBlock, records - done);
        const uint8_t* in = src + done * rec;
        uint8_t* out = dst + done * rec;
        uint64_t prev = 0;
        for (size_t i = 0; i < n; ++i) {
            uint64_t d = 0;
            for (int k = 7; k >= 0; --k) d = d << 8 | in[size_t(k) * n + i];
            prev += d;
            uint8_t* r = out + i * rec;
            for (int k = 0; k < 8; ++k) r[k] = uint8_t(prev >> (8 * k));
            for (size_t k = 8; k < rec; ++k) r[k] = in[k * n + i];
        }
        done += n;
    }
}

// Records to byte planes, the first 8 bytes as differences: what unfilter undoes.
void filter(const uint8_t* src, uint8_t* dst, size_t records, size_t rec) {
    for (size_t done = 0; done < records;) {
        const size_t n = std::min(kFilterBlock, records - done);
        const uint8_t* in = src + done * rec;
        uint8_t* out = dst + done * rec;
        uint64_t prev = 0;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t* r = in + i * rec;
            uint64_t v = 0;
            for (int k = 7; k >= 0; --k) v = v << 8 | r[k];
            const uint64_t d = v - prev;
            prev = v;
            for (int k = 0; k < 8; ++k) out[size_t(k) * n + i] = uint8_t(d >> (8 * k));
            for (size_t k = 8; k < rec; ++k) out[k * n + i] = r[k];
        }
        done += n;
    }
}

class Zip {
public:
    explicit Zip(const std::string& path) { ok_ = mz_zip_reader_init_file(&z_, path.c_str(), 0); }
    ~Zip() {
        if (ok_) mz_zip_reader_end(&z_);
    }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;
    bool ok() const { return ok_; }
    mz_uint count() { return mz_zip_reader_get_num_files(&z_); }
    std::string name(mz_uint i) {
        mz_zip_archive_file_stat st;
        return mz_zip_reader_file_stat(&z_, i, &st) ? std::string(st.m_filename) : std::string();
    }
    int find(const std::string& name) { return mz_zip_reader_locate_file(&z_, name.c_str(), nullptr, 0); }
    // An entry, whole: stored, deflated or packed with Zstandard.
    bool read(int index, std::vector<uint8_t>& out, std::string& err) {
        mz_zip_archive_file_stat st;
        if (index < 0 || !mz_zip_reader_file_stat(&z_, mz_uint(index), &st)) {
            err = "a missing entry";
            return false;
        }
        if (st.m_uncomp_size > (uint64_t(1) << 33)) {
            err = std::string(st.m_filename) + " is too big to read";
            return false;
        }
        out.resize(size_t(st.m_uncomp_size));
        if (st.m_method == 0 || st.m_method == MZ_DEFLATED) {
            if (!mz_zip_reader_extract_to_mem(&z_, mz_uint(index), out.data(), out.size(), 0)) {
                err = std::string(st.m_filename) + ": damaged";
                return false;
            }
            return true;
        }
        if (st.m_method == kMethodZstd) {
            std::vector<uint8_t> packed(size_t(st.m_comp_size));
            if (!mz_zip_reader_extract_to_mem(&z_, mz_uint(index), packed.data(), packed.size(), MZ_ZIP_FLAG_COMPRESSED_DATA)) {
                err = std::string(st.m_filename) + ": damaged";
                return false;
            }
            const size_t got = ZSTD_decompress(out.data(), out.size(), packed.data(), packed.size());
            if (ZSTD_isError(got) || got != out.size()) {
                err = std::string(st.m_filename) + ": damaged Zstandard data";
                return false;
            }
            return true;
        }
        err = std::string(st.m_filename) + (st.m_method == kMethodLzma ? " is packed with LZMA, which God's Hand does not read: pack it with Zstandard or Deflate"
                                                                         : " is packed in a way God's Hand does not know (method " +
                                                                               std::to_string(st.m_method) + ")");
        return false;
    }

private:
    mz_zip_archive z_{};
    bool ok_ = false;
};

}  // namespace

bool isThrChart(const std::string& path) {
    if (!endsWithI(path, kThrExt)) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char m[4] = {};
    const bool zip = std::fread(m, 1, 4, f) == 4 && m[0] == 'P' && m[1] == 'K' && m[2] == 3 && m[3] == 4;
    std::fclose(f);
    return zip;
}

bool loadThrChart(const std::string& path, Chart& out, std::string& err) {
    Zip zip(path);
    if (!zip.ok()) {
        err = path + " is not a .thr archive";
        return false;
    }
    int metaIndex = -1;
    for (mz_uint i = 0; i < zip.count(); ++i) {
        const std::string n = zip.name(i);
        if (n.find('/') == std::string::npos && toLower(n).find("-metadata.json") != std::string::npos) metaIndex = int(i);
    }
    if (metaIndex < 0) {
        err = ".thr: no -metadata.json inside";
        return false;
    }
    std::vector<uint8_t> metaBytes;
    if (!zip.read(metaIndex, metaBytes, err)) return false;
    json::Document d;
    if (!d.parse(std::string_view(reinterpret_cast<const char*>(metaBytes.data()), metaBytes.size()))) {
        err = ".thr: a damaged metadata.json";
        return false;
    }
    const json::Value& root = d.root();
    const json::Value* tbl = d.get(root, "tbl");
    const json::Value* parts = tbl ? d.get(*tbl, "parts") : nullptr;
    if (!parts || d.size(*parts) == 0) {
        err = ".thr: it lists no charts";
        return false;
    }
    const json::Value& part = d.at(*parts, 0);
    const std::string prefix(d.strOr(d.get(part, "prefix"), ""));
    int keys = int(json::Document::num(d.get(part, "keyCount"), 4));
    if (keys < 1 || keys > kMaxKeys) keys = 4;
    const long long noteFiles = (long long)json::Document::num(d.get(part, "noteFiles"), 0);
    std::vector<std::string> types;
    if (const json::Value* t = d.get(part, "types"))
        for (size_t i = 0; i < d.size(*t); ++i) {
            const json::Value& v = d.at(*t, i);
            types.push_back(v.type == json::Type::String ? std::string(d.str(v)) : std::string());
        }
    const json::Value* song = d.get(root, "song");
    const json::Value& s = song ? *song : root;  // no "song": every field its default
    const bool v1 = std::string(d.strOr(d.get(s, "format"), "")).rfind("psych_v1", 0) == 0;

    out = Chart();
    out.title = std::string(d.strOr(d.get(s, "song"), ""));
    out.player = std::string(d.strOr(d.get(s, "player1"), ""));
    out.opponent = std::string(d.strOr(d.get(s, "player2"), ""));
    out.gf = std::string(d.strOr(d.get(s, "gfVersion"), d.strOr(d.get(s, "player3"), "")));
    out.stage = std::string(d.strOr(d.get(s, "stage"), ""));
    out.speed = json::Document::num(d.get(s, "speed"), 1.0);
    out.keys = keys;
    double bpm = json::Document::num(d.get(s, "bpm"), 100.0);
    if (!(bpm > 0)) bpm = 100;
    out.tempo.push_back({0, bpm, 0});
    if (d.get(s, "events")) psych::parseEvents(d, out.events);

    // Sections: how many notes each holds, and whose side it is.
    std::vector<uint8_t> sec;
    if (!zip.read(zip.find("notes/" + prefix + "sections.bin"), sec, err)) {
        err = ".thr: its sections: " + err;
        return false;
    }
    if (sec.size() < kHeader || rd32(sec.data()) != kMagicSections) {
        err = ".thr: damaged sections";
        return false;
    }
    const size_t secCount = size_t(std::max(0.0, rdf64(sec.data() + 8)));
    if (kHeader + secCount * kSecRec > sec.size()) {
        err = ".thr: damaged sections";
        return false;
    }
    if (rd32(sec.data() + 24) & kFiltered) {
        std::vector<uint8_t> plain(sec.size());
        std::memcpy(plain.data(), sec.data(), kHeader);
        unfilter(sec.data() + kHeader, plain.data() + kHeader, secCount, kSecRec);
        sec.swap(plain);
    }
    // The tempo map: every section (with notes or not) holds its start and
    // the BPM in force there; a change wherever it differs from the one before.
    for (size_t i = 0; i < secCount; ++i) {
        const uint8_t* p = sec.data() + kHeader + i * kSecRec;
        const double t0 = rdf64(p), b = rdf64(p + 16);
        if (!std::isfinite(t0) || !std::isfinite(b) || !(b > 0)) continue;
        if (b != out.tempo.back().bpm) out.tempo.push_back({std::max(0.0, t0), b, 0});
    }
    size_t section = 0;
    uint64_t secLeft = 0;
    bool must = false;
    bool started = false;
    std::vector<uint8_t> raw, plain;
    for (long long f = 0; f < noteFiles; ++f) {
        char name[64];
        std::snprintf(name, sizeof name, "notes-%05lld.bin", f);
        if (!zip.read(zip.find("notes/" + prefix + name), raw, err)) {
            err = std::string(".thr: ") + name + ": " + err;
            return false;
        }
        if (raw.size() < kHeader || rd32(raw.data()) != kMagicNotes) {
            err = std::string(".thr: damaged ") + name;
            return false;
        }
        const size_t count = size_t(std::max(0.0, rdf64(raw.data() + 8)));
        if (kHeader + count * kNoteRec > raw.size()) {
            err = std::string(".thr: damaged ") + name;
            return false;
        }
        const uint8_t* recs = raw.data() + kHeader;
        if (rd32(raw.data() + 24) & kFiltered) {
            plain.resize(count * kNoteRec);
            unfilter(recs, plain.data(), count, kNoteRec);
            recs = plain.data();
        }
        out.notes.reserve(out.notes.size() + count);
        for (size_t i = 0; i < count; ++i) {
            while (secLeft == 0 && (started ? section + 1 : section) < secCount) {
                if (started) ++section;
                started = true;
                const uint8_t* p = sec.data() + kHeader + section * kSecRec;
                secLeft = uint64_t(rd32(p + 8)) | uint64_t(rd32(p + 32)) << 32;
                must = (rd32(p + 12) & kSecMustHit) != 0;
            }
            if (secLeft) --secLeft;
            const uint8_t* r = recs + i * kNoteRec;
            const int data = rd16(r + 12);
            const bool firstHalf = data % (keys * 2) < keys;
            Note n;
            n.time = rdf64(r);
            n.length = std::max(0.0f, rdf32(r + 8));
            n.lane = uint16_t(data % keys);
            n.side = uint8_t((v1 ? firstHalf : firstHalf == must) ? 1 : 0);
            const uint16_t type = rd16(r + 14);
            if (type < types.size() && !types[type].empty()) applyNoteType(types[type], n, out);
            if (!std::isfinite(n.time)) continue;
            out.notes.push_back(n);
        }
    }
    out.rawNoteCount = Count(uint64_t(out.notes.size()));
    out.finalize(bpm);
    return true;
}

bool writeThrChart(const std::string& path, const Chart& c, std::string& err) {
    if (c.autoCharted) {
        err = "an AutoChart chart plays only in God's Hand: it stays .ghac";
        return false;
    }
    const Count total = c.playerNotes + c.opponentNotes;
    if (!total.fits64() || total.w[0] > (uint64_t(1) << 31)) {
        err = "a .thr lists its notes, and this chart counts " + total.grouped() + ": keep it as .ghn";
        return false;
    }
    const int keys = std::max(1, c.keys);
    const double bpm = c.tempo.empty() ? 120.0 : c.tempo.front().bpm;
    // Types: the plain note (null), then the chart's own.
    std::string types = "[null";
    for (size_t i = 1; i < c.noteTypes.size(); ++i) types += ",\"" + c.noteTypes[i] + "\"";
    types += "]";
    // Every counted note, a record each, by time (psych_v1: under keyCount is the player's).
    const uint64_t listed = total.w[0];
    const size_t perFile = size_t(1) << 22;  // 4,194,304, as strand writes
    const size_t files = size_t((listed + perFile - 1) / perFile);
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file_v2(&zip, (path + ".part").c_str(), 0, MZ_ZIP_FLAG_WRITE_ZIP64)) {
        err = "cannot write " + path;
        return false;
    }
    bool ok = true;
    std::vector<uint8_t> recs, packed;
    size_t noteIdx = 0;
    uint64_t copyIdx = 0;
    // Sections of four beats with the side the camera follows; each knows its
    // notes. A tempo change ends a section there, so every section has one BPM
    // (and the reader gets each change back where it was).
    struct Sec {
        double t0;
        bool must;
        uint64_t notes = 0;
        double beats = 4;
    };
    std::vector<Sec> secs;
    const double endMs = std::max(c.lengthMs, c.tempo.empty() ? 0.0 : c.tempo.back().timeMs) + 1.0;  // the last change too
    for (double t = 0; t <= endMs;) {
        const double bpm = c.bpmAt(t + 0.01);
        double len = 4 * 60000.0 / bpm;
        for (const TempoPoint& tp : c.tempo)
            if (tp.timeMs > t + 0.5 && tp.timeMs < t + len) {
                len = tp.timeMs - t;
                break;
            }
        secs.push_back({t, c.focusAt(t + 1) == 1, 0, len * bpm / 60000.0});
        t += len;
    }
    for (const Note& n : c.notes) {
        size_t si = 0;
        if (!secs.empty()) {
            si = size_t(std::upper_bound(secs.begin(), secs.end(), n.time, [](double t, const Sec& s) { return t < s.t0; }) - secs.begin());
            si = si ? si - 1 : 0;
            secs[si].notes += c.countOf(n).sat64();
        }
    }
    for (size_t f = 0; f < files && ok; ++f) {
        const size_t count = size_t(std::min<uint64_t>(perFile, listed - uint64_t(f) * perFile));
        recs.assign(kHeader + count * kNoteRec, 0);
        for (size_t i = 0; i < count; ++i) {
            while (noteIdx < c.notes.size() && copyIdx >= c.countOf(c.notes[noteIdx]).sat64()) {
                ++noteIdx;
                copyIdx = 0;
            }
            const Note& n = c.notes[noteIdx];
            ++copyIdx;
            uint8_t* r = recs.data() + kHeader + i * kNoteRec;
            wrf64(r, n.time);
            wrf32(r + 8, n.length);
            wr16(r + 12, uint16_t(n.side ? n.lane : n.lane + keys));
            wr16(r + 14, n.type);
        }
        // Filtered: the same records, packed smaller.
        packed.assign(recs.size(), 0);
        wr32(packed.data(), kMagicNotes);
        wrf64(packed.data() + 8, double(count));
        wr32(packed.data() + 24, kFiltered);
        filter(recs.data() + kHeader, packed.data() + kHeader, count, kNoteRec);
        char name[64];
        std::snprintf(name, sizeof name, "notes/notes-%05zu.bin", f);
        ok = mz_zip_writer_add_mem(&zip, name, packed.data(), packed.size(), MZ_DEFAULT_LEVEL);
    }
    if (ok) {
        std::vector<uint8_t> s(kHeader + secs.size() * kSecRec, 0);
        wr32(s.data(), kMagicSections);
        wrf64(s.data() + 8, double(secs.size()));
        for (size_t i = 0; i < secs.size(); ++i) {
            uint8_t* p = s.data() + kHeader + i * kSecRec;
            wrf64(p, secs[i].t0);
            wr32(p + 8, uint32_t(secs[i].notes));
            wr32(p + 12, secs[i].must ? kSecMustHit : 0);
            wrf64(p + 16, c.bpmAt(secs[i].t0 + 0.01));
            wrf64(p + 24, secs[i].beats);
            wr32(p + 32, uint32_t(secs[i].notes >> 32));
        }
        ok = mz_zip_writer_add_mem(&zip, "notes/sections.bin", s.data(), s.size(), MZ_DEFAULT_LEVEL);
    }
    if (ok) {
        auto esc = [](const std::string& v) {
            std::string o = "\"";
            for (char ch : v) {
                if (ch == '"' || ch == '\\') o += '\\';
                if ((unsigned char)ch >= 0x20) o += ch;
            }
            return o + "\"";
        };
        char num[64];
        std::snprintf(num, sizeof num, "%.17g", bpm);
        std::string meta = "{\"song\":{\"song\":" + esc(c.title) + ",\"bpm\":" + num;
        std::snprintf(num, sizeof num, "%.17g", c.speed);
        meta += std::string(",\"speed\":") + num + ",\"player1\":" + esc(c.player.empty() ? "bf" : c.player) + ",\"player2\":" +
                esc(c.opponent.empty() ? "dad" : c.opponent) + ",\"gfVersion\":" + esc(c.gf) + ",\"stage\":" + esc(c.stage) +
                ",\"format\":\"psych_v1\",\"events\":[";
        for (size_t i = 0; i < c.events.size(); ++i) {
            std::snprintf(num, sizeof num, "%.17g", c.events[i].timeMs);
            meta += std::string(i ? "," : "") + "[" + num + ",[[" + esc(c.events[i].name) + "," + esc(c.events[i].v1) + "," +
                    esc(c.events[i].v2) + "]]]";
        }
        meta += "]},\"tbl\":{\"parts\":[{\"prefix\":\"\",\"keyCount\":" + std::to_string(keys) + ",\"types\":" + types +
                ",\"notesPerFile\":" + std::to_string(perFile) + ",\"noteFiles\":" + std::to_string(files) +
                ",\"noteCount\":" + std::to_string(listed) + "}]}}";
        std::string metaName = c.title.empty() ? std::string("chart") : c.title;
        for (char& ch : metaName)
            if (ch == '/' || ch == '\\') ch = '_';
        ok = mz_zip_writer_add_mem(&zip, (metaName + "-metadata.json").c_str(), meta.data(), meta.size(), MZ_DEFAULT_LEVEL);
    }
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    const std::string tmp = path + ".part";
    if (ok) {
        std::remove(path.c_str());
        ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    }
    if (!ok) {
        std::remove(tmp.c_str());
        err = "cannot write " + path;
    }
    return ok;
}

}  // namespace gh
