// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "flp_song.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <unordered_map>

#include "common.hpp"
#include "flp_import.hpp"

namespace gh {

namespace {

constexpr size_t kNoteBytes = 24;

std::filesystem::path toPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string pathStr(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

FILE* openFile(const std::string& path) {
#ifdef _WIN32
    return _wfopen(toPath(path).wstring().c_str(), L"rb");  // names in any alphabet
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

uint16_t rd16(const unsigned char* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// The project, read a piece at a time (it can be gigabytes).
class Reader {
public:
    explicit Reader(FILE* f) : f_(f), buf_(size_t(8) << 20) {}
    uint64_t pos() const { return pos_; }
    bool get(void* out, size_t n) {
        auto* o = static_cast<unsigned char*>(out);
        while (n) {
            if (at_ == len_ && !fill()) return false;
            const size_t k = std::min(n, len_ - at_);
            std::memcpy(o, buf_.data() + at_, k);
            at_ += k;
            pos_ += k;
            o += k;
            n -= k;
        }
        return true;
    }
    bool skip(uint64_t n) {
        while (n) {
            if (at_ == len_ && !fill()) return false;
            const size_t k = size_t(std::min<uint64_t>(n, len_ - at_));
            at_ += k;
            pos_ += k;
            n -= k;
        }
        return true;
    }
    int byte() {
        if (at_ == len_ && !fill()) return -1;
        ++pos_;
        return buf_[at_++];
    }

private:
    bool fill() {
        len_ = std::fread(buf_.data(), 1, buf_.size(), f_);
        at_ = 0;
        return len_ > 0;
    }
    FILE* f_;
    std::vector<unsigned char> buf_;
    size_t at_ = 0, len_ = 0;
    uint64_t pos_ = 0;
};

// A note as the conversion needs it (what SNIFF reads of a note record).
struct NoteKey {
    uint32_t time, duration;
    uint16_t channel;
    uint8_t pitch, velocity, porta;
    bool operator==(const NoteKey& o) const {
        return time == o.time && duration == o.duration && channel == o.channel && pitch == o.pitch && velocity == o.velocity &&
               porta == o.porta;
    }
};
struct NoteKeyHash {
    size_t operator()(const NoteKey& k) const {
        uint64_t h = (uint64_t(k.time) << 32 | k.duration) * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t(k.channel) << 24 | uint64_t(k.pitch) << 16 | uint64_t(k.velocity) << 8 | k.porta) + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
        return size_t(h);
    }
};
using PatternNotes = std::unordered_map<NoteKey, uint64_t, NoteKeyHash>;

struct Clip {
    uint32_t position = 0, length = 0;
    int pattern = 0;
    double start = 0;
};

// One cell of the chart: notes at the same tick, side, lane, hold and alt.
struct Cell {
    uint64_t tick;
    uint32_t hold;   // ticks
    uint16_t lane;
    uint8_t side, alt;
    uint64_t count;
    bool sameAs(const Cell& o) const { return tick == o.tick && hold == o.hold && lane == o.lane && side == o.side && alt == o.alt; }
    bool operator<(const Cell& o) const {
        if (tick != o.tick) return tick < o.tick;
        if (side != o.side) return side < o.side;
        if (lane != o.lane) return lane < o.lane;
        if (hold != o.hold) return hold < o.hold;
        return alt < o.alt;
    }
};

std::string flText(const std::vector<unsigned char>& d) {
    // UTF-16 (FL Studio 11 and later) or 8-bit text.
    const bool wide = d.size() >= 2 && d.size() % 2 == 0 && d[1] == 0 ? true : (d.size() >= 2 && d[d.size() - 1] == 0 && d[d.size() - 2] == 0);
    std::string out;
    if (wide) {
        for (size_t i = 0; i + 1 < d.size(); i += 2) {
            const unsigned c = unsigned(d[i]) | unsigned(d[i + 1]) << 8;
            if (c == 0) break;
            if (c < 0x80) out += char(c);
            else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 63)); }
            else { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
        }
    } else {
        for (unsigned char c : d) {
            if (!c) break;
            out += char(c);
        }
    }
    return trim(out);
}

std::string baseName(std::string n) {
    static const char* alt = " (Alt)";
    if (n.size() >= 6 && n.compare(n.size() - 6, 6, alt) == 0) n.resize(n.size() - 6);
    return trim(n);
}
bool isAltName(const std::string& n) { return n.size() >= 6 && n.compare(n.size() - 6, 6, " (Alt)") == 0; }

}  // namespace

bool flpSongToChart(const std::string& path, Chart& out, std::string& err, const FlpSongOptions& opt, FlpSongStats* stats,
                    const std::function<void(double)>& progress) {
    out = Chart();
    FlpSongStats st;
    FILE* f = openFile(path);
    if (!f) { err = "cannot read " + path; return false; }
    std::error_code fec;
    const double fileBytes = double(std::filesystem::file_size(toPath(path), fec));
    Reader in(f);
    unsigned char head[14];
    if (!in.get(head, 8) || std::memcmp(head, "FLhd", 4) != 0) {
        std::fclose(f);
        err = "not an FL Studio project";
        return false;
    }
    const uint32_t headLen = rd32(head + 4);
    std::vector<unsigned char> hdr(headLen);
    if (headLen < 6 || !in.get(hdr.data(), headLen)) {
        std::fclose(f);
        err = "an FL Studio project cut short";
        return false;
    }
    st.ppq = std::max(1, int(rd16(hdr.data() + 4)));
    unsigned char dt[8];
    if (!in.get(dt, 8) || std::memcmp(dt, "FLdt", 4) != 0) {
        std::fclose(f);
        err = "an FL Studio project without data";
        return false;
    }
    const uint64_t dataEnd = in.pos() + rd32(dt + 4);

    std::map<int, PatternNotes> patterns;
    std::vector<Clip> clips;
    std::vector<std::string> channelNames;
    std::vector<bool> channelUsed;
    int pattern = 0, channel = -1;
    bool inMixer = false, havePlaylist = false;
    std::vector<unsigned char> data, chunk;
    double lastProgress = -1;
    while (in.pos() < dataEnd) {
        const int id = in.byte();
        if (id < 0) break;
        uint32_t value = 0;
        bool isData = false;
        uint64_t len = 0;
        if (id == 0xAC) {  // FL Studio 25+: a dword, then a string a byte long
            unsigned char b5[5];
            if (!in.get(b5, 5)) break;
            value = rd32(b5);
            len = b5[4];
            isData = true;
        } else if (id < 64) {
            const int v = in.byte();
            if (v < 0) break;
            value = uint32_t(v);
        } else if (id < 128) {
            unsigned char b2[2];
            if (!in.get(b2, 2)) break;
            value = rd16(b2);
        } else if (id < 192) {
            unsigned char b4[4];
            if (!in.get(b4, 4)) break;
            value = rd32(b4);
        } else {
            int shift = 0;
            for (;;) {
                const int c = in.byte();
                if (c < 0) break;
                len |= uint64_t(c & 0x7F) << shift;
                shift += 7;
                if (!(c & 0x80) || shift > 56) break;
            }
            isData = true;
        }
        if (isData && id == 0xE0) {  // a pattern's notes: counted, not kept one by one
            PatternNotes& pn = patterns[pattern];
            uint64_t left = len;
            const size_t per = size_t(1) << 20;
            chunk.resize(per * kNoteBytes);
            while (left >= kNoteBytes) {
                const size_t n = size_t(std::min<uint64_t>(left / kNoteBytes, per));
                if (!in.get(chunk.data(), n * kNoteBytes)) { left = 0; break; }
                left -= uint64_t(n) * kNoteBytes;
                for (size_t k = 0; k < n; ++k) {
                    const unsigned char* r = chunk.data() + k * kNoteBytes;
                    NoteKey key;
                    key.time = rd32(r);
                    key.channel = rd16(r + 6);
                    key.duration = rd32(r + 8);
                    key.pitch = r[12];
                    key.porta = (r[19] & 0x10) ? 1 : 0;
                    key.velocity = r[21];
                    ++pn[key];
                    if (key.channel < 4096) {
                        if (channelUsed.size() <= key.channel) channelUsed.resize(size_t(key.channel) + 1, false);
                        channelUsed[key.channel] = true;
                    }
                }
                st.noteRecords += n;
                if (progress && fileBytes > 0) {
                    const double p = double(in.pos()) / fileBytes;
                    if (p - lastProgress >= 0.01) { progress(p); lastProgress = p; }
                }
            }
            if (left && !in.skip(left)) break;
            continue;
        }
        if (isData) {
            if (len > (uint64_t(256) << 20)) {  // nothing else this big is needed
                if (!in.skip(len)) break;
                continue;
            }
            data.resize(size_t(len));
            if (len && !in.get(data.data(), size_t(len))) break;
        }
        switch (id) {
            case 0x41: pattern = int(value); break;           // the pattern the next events belong to
            case 0x40:                                         // a channel of the rack
                channel = int(value);
                if (!inMixer) {
                    if (channelNames.size() <= size_t(value)) channelNames.resize(size_t(value) + 1);
                }
                break;
            case 0xE1: inMixer = true; break;                  // the mixer: no channels after it
            case 0x9C: st.bpm = value / 1000.0; break;         // tempo x 1000
            case 0x42: if (st.bpm <= 0) st.bpm = value; break; // tempo of old versions
            case 0xC7: st.version = flText(data); break;
            case 0xC2: out.title = flText(data); break;
            case 0xCB:  // a channel's name as the user set it
            case 0xC9:  // its plugin's
                if (!inMixer && channel >= 0 && size_t(channel) < channelNames.size()) {
                    const std::string n = flText(data);
                    if (!n.empty() && (id == 0xCB || channelNames[size_t(channel)].empty())) channelNames[size_t(channel)] = n;
                }
                break;
            case 0xE9:  // the playlist (the first arrangement's)
                if (!havePlaylist && !data.empty()) {
                    for (size_t size : {size_t(88), size_t(60), size_t(32)}) {
                        if (data.size() % size) continue;
                        bool all = true;
                        for (size_t k = 0; k + size <= data.size() && all; k += size) all = rd16(data.data() + k + 4) == 20480;
                        if (!all) continue;
                        for (size_t k = 0; k + size <= data.size(); k += size) {
                            const unsigned char* r = data.data() + k;
                            const uint16_t item = rd16(r + 6);
                            if (item <= 20480) continue;  // an audio or automation clip
                            Clip c;
                            c.position = rd32(r);
                            c.pattern = int(item - 20480);
                            c.length = rd32(r + 8);
                            c.start = flpClipOffset(r + 24);
                            clips.push_back(c);
                        }
                        havePlaylist = true;
                        break;
                    }
                }
                break;
            default: break;
        }
    }
    std::fclose(f);
    if (progress) progress(1.0);
    if (!(st.bpm > 0)) st.bpm = 120;
    if (patterns.empty()) { err = "no notes in the project"; return false; }

    // Sides and alt by channel: a channel and its "(Alt)" twin are one
    // instrument; the first instrument with notes plays the player's side,
    // the next the opponent's (unless the channels are given).
    const size_t nch = std::max(channelNames.size(), channelUsed.size());
    channelNames.resize(nch);
    channelUsed.resize(nch, false);
    std::vector<uint8_t> side(nch, 1), alt(nch, 0);
    std::vector<std::string> instruments;
    for (size_t c = 0; c < nch; ++c) {
        alt[c] = isAltName(channelNames[c]) ? 1 : 0;
        if (!channelUsed[c]) continue;
        const std::string b = baseName(channelNames[c].empty() ? "Channel " + std::to_string(c) : channelNames[c]);
        auto it = std::find(instruments.begin(), instruments.end(), b);
        size_t k = size_t(it - instruments.begin());
        if (it == instruments.end()) instruments.push_back(b);
        side[c] = k == 1 ? 0 : 1;
    }
    auto listed = [](const std::vector<int>& v, size_t c) { return std::find(v.begin(), v.end(), int(c)) != v.end(); };
    if (!opt.playerChannels.empty() || !opt.opponentChannels.empty())
        for (size_t c = 0; c < nch; ++c) side[c] = listed(opt.opponentChannels, c) ? 0 : listed(opt.playerChannels, c) ? 1 : side[c];
    for (size_t c = 0; c < nch; ++c)
        if (channelUsed[c])
            st.channels.push_back(std::to_string(c) + " " + (channelNames[c].empty() ? std::string("(no name)") : channelNames[c]) +
                                  (side[c] ? " - player" : " - opponent") + (alt[c] ? ", Alt Animation" : ""));

    // Each pattern's notes as cells of its own (time in the pattern), sorted by time.
    struct PCell { uint32_t time, hold; uint16_t lane; uint8_t side, alt; uint64_t count; };
    std::map<int, std::vector<PCell>> pcells;
    const uint32_t step = uint32_t(st.ppq / 4);
    for (auto& [num, notes] : patterns) {
        std::vector<PCell>& v = pcells[num];
        v.reserve(notes.size());
        for (const auto& [k, cnt] : notes) {
            const size_t c = k.channel < nch ? k.channel : 0;
            PCell p;
            p.time = k.time;
            // SNIFF's holds: played softly, or a beat long or more.
            p.hold = (k.velocity < 0x40 || k.duration >= uint32_t(st.ppq)) && k.duration > step ? k.duration - step : 0;
            p.lane = uint16_t(k.pitch % 4);
            p.side = side[c];
            p.alt = (alt[c] || k.porta) ? 1 : 0;
            p.count = cnt;
            v.push_back(p);
        }
        notes = PatternNotes();  // its memory back
        std::sort(v.begin(), v.end(), [](const PCell& a, const PCell& b) { return a.time < b.time; });
    }

    // The playlist plays them: each clip its window of its pattern. A
    // project without a playlist plays its patterns one after another.
    if (clips.empty()) {
        uint64_t at = 0;
        for (auto& [num, v] : pcells) {
            uint32_t end = 0;
            for (const PCell& p : v) end = std::max(end, p.time + 1);
            Clip c;
            c.position = uint32_t(std::min<uint64_t>(at, UINT32_MAX));
            c.pattern = num;
            c.length = (end + uint32_t(st.ppq) * 4 - 1) / (uint32_t(st.ppq) * 4) * uint32_t(st.ppq) * 4;
            clips.push_back(c);
            at += c.length;
        }
    }
    std::vector<Cell> cells;
    for (const Clip& c : clips) {
        const auto it = pcells.find(c.pattern);
        if (it == pcells.end()) continue;
        ++st.clips;
        const std::vector<PCell>& v = it->second;
        const double from = c.start, to = c.start + double(c.length);
        auto first = std::lower_bound(v.begin(), v.end(), from, [](const PCell& p, double t) { return double(p.time) < t; });
        for (auto p = first; p != v.end() && double(p->time) < to; ++p)
            cells.push_back({uint64_t(c.position) + uint64_t(double(p->time) - from), p->hold, p->lane, p->side, p->alt, p->count});
    }
    pcells.clear();
    std::sort(cells.begin(), cells.end());
    size_t w = 0;
    for (size_t r = 0; r < cells.size(); ++r) {
        if (w && cells[w - 1].sameAs(cells[r])) cells[w - 1].count += cells[r].count;
        else cells[w++] = cells[r];
    }
    cells.resize(w);
    st.cells = cells.size();
    if (cells.empty()) { err = "the playlist plays no notes"; return false; }

    // The chart: one note per cell, counting as all of the cell's notes.
    const double msPerTick = 60000.0 / (st.bpm * st.ppq);
    out.keys = 4;
    out.tempo.push_back({0, st.bpm, 0});
    out.speed = clampv(st.bpm / 50.0, 0.5, 6.0);  // SNIFF's default scroll speed
    out.player = "bf";
    out.opponent = "dad";
    out.gf = "gf";
    out.notes.reserve(cells.size());
    const uint64_t secTicks = uint64_t(st.ppq) * 4;
    std::vector<std::array<Count, 2>> perSection;
    for (const Cell& c : cells) {
        Note n;
        n.time = double(c.tick) * msPerTick;
        n.length = float(double(c.hold) * msPerTick);
        n.lane = c.lane;
        n.side = c.side;
        if (c.alt) n.flags |= kFlagAlt;
        out.notes.push_back(n);
        out.setCount(out.notes.back(), Count(c.count));
        (c.side ? st.player : st.opponent) += Count(c.count);
        const size_t s = size_t(c.tick / secTicks);
        if (perSection.size() <= s) perSection.resize(s + 1);
        perSection[s][c.side] += Count(c.count);
    }
    st.notes = st.player + st.opponent;
    // Sections of four beats; the camera on the side with more notes in each.
    const double secMs = double(secTicks) * msPerTick;
    for (size_t s = 0; s < perSection.size(); ++s) {
        const bool mustHit = !(perSection[s][0] > perSection[s][1]);
        out.sections.push_back({double(s) * secMs, mustHit, false, false, 4, st.bpm});
        const uint8_t sd = mustHit ? 1 : 0;
        if (out.focus.empty() || out.focus.back().side != sd) out.focus.push_back({double(s) * secMs, sd});
    }
    if (out.title.empty()) {
        std::string t = pathStr(toPath(path).filename());
        if (endsWithI(t, ".flp")) t.resize(t.size() - 4);
        out.title = t;
    }
    out.rawNoteCount = cells.size();
    out.finalize(st.bpm);
    if (stats) *stats = st;
    return true;
}

}  // namespace gh
