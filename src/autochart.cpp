// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "autochart.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include "common.hpp"

namespace gh {

namespace {

constexpr int kN = 1024, kHop = 256, kRate = 22050;
constexpr double kHopMs = 1000.0 * kHop / kRate;
constexpr double kAttackMs = 29.0;  // the flux peaks before the attack is heard (measured on clicks)

void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * float(kPi) / float(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1, 0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    const size_t k = size_t(clampv(p, 0.0, 1.0) * double(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}

// ------------------------------------------------------------ analysis --

// A sound, frame by frame (11.6 ms): its onset envelope, onset strength in
// four bands (under 150 Hz, to 600, to 2500, above: each band's strongest
// rise), loudness and centroid.
struct Analysis {
    size_t nf = 0;
    std::vector<float> o;                     // onset envelope, slow trend removed
    std::vector<std::array<float, 4>> band;  // band flux
    std::vector<float> energy, centroid;
    // Means over the sound, for features that do not depend on its volume.
    float meanO = 1, meanE = 1;
    std::array<float, 4> meanBand{1, 1, 1, 1};
    double timeOf(size_t f) const { return double(f) * kHopMs + kHopMs * 0.5 + kAttackMs; }
};

Analysis analyze(const int16_t* samples, size_t frames, int channels, int sampleRate) {
    Analysis a;
    if (!samples || frames == 0 || channels < 1 || sampleRate < 8000) return a;
    const double step = double(sampleRate) / kRate;
    const size_t n = size_t(double(frames) / step);
    std::vector<float> mono(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t f = std::min(frames - 1, size_t(double(i) * step));
        float s = 0;
        for (int c = 0; c < channels; ++c) s += float(samples[f * size_t(channels) + size_t(c)]);
        mono[i] = s / (32768.0f * float(channels));
    }
    a.nf = n > size_t(kN) ? (n - size_t(kN)) / kHop + 1 : 0;
    if (a.nf == 0) return a;
    std::vector<float> window(kN);
    for (int i = 0; i < kN; ++i) window[size_t(i)] = 0.5f - 0.5f * std::cos(2.0f * float(kPi) * float(i) / float(kN - 1));
    std::vector<double> env(a.nf, 0);
    a.band.assign(a.nf, {0, 0, 0, 0});
    a.energy.assign(a.nf, 0);
    a.centroid.assign(a.nf, 0);
    std::vector<float> prev(kN / 2, 0), mag(kN / 2);
    std::vector<std::complex<float>> buf(kN);
    const double binHz = double(kRate) / kN;
    for (size_t f = 0; f < a.nf; ++f) {
        for (int i = 0; i < kN; ++i) buf[size_t(i)] = {mono[f * kHop + size_t(i)] * window[size_t(i)], 0.0f};
        fft(buf);
        double flux = 0, e = 0, cw = 0, cs = 0;
        std::array<float, 4> bf{0, 0, 0, 0};
        for (int k = 1; k < kN / 2; ++k) {
            const float m = std::log1p(100.0f * std::abs(buf[size_t(k)]));
            mag[size_t(k)] = m;
            const double hz = k * binHz;
            const float rise = std::max(0.0f, m - prev[size_t(k)]);
            const double w = hz < 150 ? 1.6 : hz < 2500 ? 1.0 : 0.7;  // kicks count a bit more
            flux += w * rise;
            const int bk = hz < 150 ? 0 : hz < 600 ? 1 : hz < 2500 ? 2 : 3;
            bf[size_t(bk)] = std::max(bf[size_t(bk)], rise);  // the band's strongest rise: bands of any width compare
            e += m;
            cw += m * hz;
            cs += m;
        }
        prev.swap(mag);
        env[f] = flux;
        a.band[f] = bf;
        a.energy[f] = float(e);
        a.centroid[f] = cs > 0 ? float(cw / cs) : 0.0f;
    }
    // The onset envelope without its slow trend.
    a.o.assign(a.nf, 0);
    const int w = 24;
    double acc = 0;
    for (size_t i = 0; i < a.nf; ++i) {
        acc += env[i];
        if (i >= size_t(2 * w + 1)) acc -= env[i - 2 * w - 1];
        const size_t c = i >= size_t(w) ? i - w : 0;
        const double mean = acc / double(std::min(i + 1, size_t(2 * w + 1)));
        a.o[c] = float(std::max(0.0, env[c] - mean));
    }
    double so = 0, se = 0;
    std::array<double, 4> sb{0, 0, 0, 0};
    for (size_t f = 0; f < a.nf; ++f) {
        so += a.o[f];
        se += a.energy[f];
        for (int k = 0; k < 4; ++k) sb[size_t(k)] += a.band[f][size_t(k)];
    }
    const double nfd = double(a.nf);
    a.meanO = float(std::max(1e-6, so / nfd));
    a.meanE = float(std::max(1e-6, se / nfd));
    for (int k = 0; k < 4; ++k) a.meanBand[size_t(k)] = float(std::max(1e-6, sb[size_t(k)] / nfd));
    return a;
}

struct Onset {
    double t;
    float strength, centroid, energy;
    std::array<float, 4> band;
    size_t frame;
};

std::vector<Onset> pickOnsets(const Analysis& a) {
    std::vector<Onset> out;
    if (a.nf < 8) return out;
    for (size_t i = 3; i + 3 < a.nf; ++i) {
        double mean = 0;
        int cnt = 0;
        for (size_t j = i >= 10 ? i - 10 : 0; j <= std::min(a.nf - 1, i + 10); ++j, ++cnt) mean += a.o[j];
        mean /= std::max(1, cnt);
        bool peak = a.o[i] > 0;
        for (size_t j = i - 3; j <= i + 3 && peak; ++j)
            if (j != i && a.o[j] > a.o[i]) peak = false;
        if (peak && a.o[i] > mean * 1.35 + 1e-6) out.push_back({a.timeOf(i), a.o[i], a.centroid[i], a.energy[i], a.band[i], i});
    }
    return out;
}

// Tempo from the onset envelope's autocorrelation (70-200 BPM, a gentle
// preference for ~130), its phase from the grid that collects the most
// onset energy, both refined by a least-squares fit of strong onsets to the
// eighth-note grid (a 0.1 % tempo error drifts ~0.1 s over a song).
bool tempoOf(const Analysis& a, const std::vector<Onset>& onsets, double& bpm, double& phase) {
    const size_t nf = a.nf;
    if (nf < 64) return false;
    const std::vector<float>& o = a.o;
    double bestLag = 0, bestScore = -1;
    const double minLag = 60000.0 / 200.0 / kHopMs, maxLag = 60000.0 / 70.0 / kHopMs;
    std::vector<double> ac(size_t(maxLag) + 3, 0);
    for (size_t lag = size_t(minLag); lag <= size_t(maxLag) + 1 && lag < nf; ++lag) {
        double s = 0;
        for (size_t i = lag; i < nf; ++i) s += double(o[i]) * o[i - lag];
        ac[lag] = s / double(nf - lag);
    }
    for (size_t lag = size_t(minLag) + 1; lag + 1 < ac.size() && lag <= size_t(maxLag); ++lag) {
        const double b = 60000.0 / (double(lag) * kHopMs);
        const double pref = std::exp(-0.5 * std::pow(std::log2(b / 130.0) / 0.6, 2.0));
        // Lags whose double is strong too: true beat periods repeat.
        const double sc = (ac[lag] + 0.5 * (2 * lag < ac.size() ? ac[2 * lag] : 0)) * (0.6 + 0.4 * pref);
        if (sc > bestScore) {
            bestScore = sc;
            bestLag = double(lag);
        }
    }
    if (bestLag <= 0) return false;
    {
        const size_t l = size_t(bestLag);
        if (l > 0 && l + 1 < ac.size()) {
            const double x = ac[l - 1], y = ac[l], z = ac[l + 1], den = x - 2 * y + z;
            if (std::fabs(den) > 1e-12) bestLag += clampv(0.5 * (x - z) / den, -0.5, 0.5);
        }
    }
    bpm = 60000.0 / (bestLag * kHopMs);
    while (bpm < 85) bpm *= 2;
    while (bpm > 210) bpm /= 2;
    bpm = std::round(bpm * 10.0) / 10.0;
    double beatMs = 60000.0 / bpm;
    double bestPhase = 0, bestPhaseScore = -1;
    for (double ph = 0; ph < beatMs; ph += kHopMs * 0.5) {
        double s = 0;
        for (double t = ph; t < double(nf) * kHopMs; t += beatMs) {
            const size_t i = size_t(t / kHopMs);
            if (i < nf) s += o[i];
        }
        if (s > bestPhaseScore) {
            bestPhaseScore = s;
            bestPhase = ph;
        }
    }
    if (onsets.size() >= 16) {
        std::vector<double> st;
        for (const Onset& on : onsets) st.push_back(on.strength);
        const double cut = percentile(st, 0.4);
        double ph = bestPhase + kAttackMs;
        const double spans[] = {15000, 30000, 60000, 120000, 1e12, 1e12};
        for (double span : spans) {
            const double slotE = beatMs / 2;
            double sk = 0, st2 = 0, skk = 0, skt = 0, cnt = 0;
            for (const Onset& on : onsets) {
                if (on.strength < cut || on.t > onsets.front().t + span) continue;
                const double k = std::round((on.t - ph) / slotE);
                if (std::fabs(on.t - (ph + k * slotE)) > slotE * 0.25) continue;
                sk += k;
                st2 += on.t;
                skk += k * k;
                skt += k * on.t;
                cnt += 1;
            }
            const double den = cnt * skk - sk * sk;
            if (cnt < 16 || std::fabs(den) < 1e-9) continue;
            const double b = (cnt * skt - sk * st2) / den, a0 = (st2 - b * sk) / cnt;
            if (!(b > 0) || std::fabs(2 * b / beatMs - 1.0) > 0.02) continue;
            beatMs = 2 * b;
            ph = a0;
        }
        while (ph < 0) ph += beatMs;
        bestPhase = std::fmod(ph, beatMs);
        bpm = 60000.0 / beatMs;
    } else {
        bestPhase += kAttackMs;
    }
    phase = bestPhase;
    return bpm > 0;
}

// The sum of several sounds' analyses, frame by frame (they start together).
Analysis mix(const std::vector<const Analysis*>& parts) {
    Analysis m;
    for (const Analysis* p : parts) m.nf = std::max(m.nf, p->nf);
    m.o.assign(m.nf, 0);
    m.band.assign(m.nf, {0, 0, 0, 0});
    m.energy.assign(m.nf, 0);
    m.centroid.assign(m.nf, 0);
    std::vector<float> cw(m.nf, 0);
    for (const Analysis* p : parts)
        for (size_t f = 0; f < p->nf; ++f) {
            m.o[f] += p->o[f] / p->meanO;
            for (int k = 0; k < 4; ++k) m.band[f][size_t(k)] += p->band[f][size_t(k)] / p->meanBand[size_t(k)];
            m.energy[f] += p->energy[f] / p->meanE;
            m.centroid[f] += p->centroid[f] * p->energy[f];
            cw[f] += p->energy[f];
        }
    double so = 0, se = 0;
    std::array<double, 4> sb{0, 0, 0, 0};
    for (size_t f = 0; f < m.nf; ++f) {
        m.centroid[f] = cw[f] > 0 ? m.centroid[f] / cw[f] : 0;
        so += m.o[f];
        se += m.energy[f];
        for (int k = 0; k < 4; ++k) sb[size_t(k)] += m.band[f][size_t(k)];
    }
    if (m.nf) {
        m.meanO = float(std::max(1e-6, so / double(m.nf)));
        m.meanE = float(std::max(1e-6, se / double(m.nf)));
        for (int k = 0; k < 4; ++k) m.meanBand[size_t(k)] = float(std::max(1e-6, sb[size_t(k)] / double(m.nf)));
    }
    return m;
}

// ------------------------------------------------------------ features --

// The three parts of a song the model hears; any may be missing.
struct Sources {
    const Analysis* inst = nullptr;
    const Analysis* vp = nullptr;
    const Analysis* vo = nullptr;
    size_t nf() const {
        return std::max({inst ? inst->nf : 0, vp ? vp->nf : 0, vo ? vo->nf : 0});
    }
};

// The whole song's onset strength, all parts together, normalised.
std::vector<float> totalFlux(const Sources& s) {
    std::vector<float> t(s.nf(), 0);
    for (const Analysis* a : {s.inst, s.vp, s.vo})
        if (a)
            for (size_t f = 0; f < a->nf; ++f) t[f] += a->o[f] / a->meanO;
    return t;
}

void featuresOf(const Sources& s, const std::vector<float>& total, const std::vector<float>& local, const Chart& grid, size_t f,
                float* x) {
    int k = 0;
    for (const Analysis* a : {s.inst, s.vp, s.vo}) {
        if (a && f < a->nf) {
            x[k++] = a->o[f] / a->meanO;
            for (int b = 0; b < 4; ++b) x[k++] = a->band[f][size_t(b)] / a->meanBand[size_t(b)];
            x[k++] = a->energy[f] / a->meanE - 1.0f;
        } else {
            for (int b = 0; b < 6; ++b) x[k++] = 0;
        }
    }
    const double t = double(f) * kHopMs + kHopMs * 0.5 + kAttackMs;
    const double beat = grid.beatAt(t);
    x[k++] = float(std::cos(2 * kPi * beat));
    x[k++] = float(std::cos(4 * kPi * beat));
    x[k++] = float(std::cos(8 * kPi * beat));
    x[k++] = f < local.size() ? local[f] : 0.0f;
    bool peak = f < total.size() && total[f] > 0;
    for (size_t j = f >= 3 ? f - 3 : 0; peak && j <= std::min(total.size() - 1, f + 3); ++j)
        if (j != f && total[j] > total[f]) peak = false;
    x[k++] = peak ? 1.0f : 0.0f;
    const float ep = s.vp && f < s.vp->nf ? s.vp->energy[f] / s.vp->meanE : 0.0f;
    const float eo = s.vo && f < s.vo->nf ? s.vo->energy[f] / s.vo->meanE : 0.0f;
    x[k++] = (ep - eo) / (ep + eo + 1e-3f);
    for (int i = 0; i < AutoModel::kFeatures; ++i) x[i] = std::clamp(x[i], -5.0f, 5.0f);
}

// The mean of the total flux over half a second either side of each frame.
std::vector<float> localMean(const std::vector<float>& total) {
    std::vector<float> m(total.size(), 0);
    const size_t w = 43;
    double acc = 0;
    size_t lo = 0, hi = 0;
    for (size_t f = 0; f < total.size(); ++f) {
        while (hi < total.size() && hi <= f + w) acc += total[hi++];
        while (lo + w < f) acc -= total[lo++];
        m[f] = float(acc / double(std::max<size_t>(1, hi - lo)));
    }
    return m;
}

float dot(const AutoModel::Weights& w, const float* x) {
    float z = w[AutoModel::kFeatures];
    for (int i = 0; i < AutoModel::kFeatures; ++i) z += w[size_t(i)] * x[i];
    return z;
}
float sigmoid(float z) { return 1.0f / (1.0f + std::exp(-std::clamp(z, -30.0f, 30.0f))); }

void adagrad(AutoModel::Weights& w, AutoModel::Weights& g, const float* x, float err, float lr) {
    for (int i = 0; i <= AutoModel::kFeatures; ++i) {
        const float xi = i < AutoModel::kFeatures ? x[i] : 1.0f;
        const float grad = err * xi + (i < AutoModel::kFeatures ? 1e-4f * w[size_t(i)] : 0.0f);
        g[size_t(i)] += grad * grad;
        w[size_t(i)] -= lr * grad / (std::sqrt(g[size_t(i)]) + 1e-6f);
    }
}

// The beat grid of a tempo map (a chart with only its tempo).
Chart gridOf(const std::vector<TempoPoint>& tempo) {
    Chart g;
    for (const TempoPoint& t : tempo)
        if (t.bpm > 0) g.tempo.push_back({t.timeMs, t.bpm, 0});
    if (g.tempo.empty()) g.tempo.push_back({0, 120, 0});
    std::stable_sort(g.tempo.begin(), g.tempo.end(), [](const TempoPoint& a, const TempoPoint& b) { return a.timeMs < b.timeMs; });
    // Beat 0 where the first tempo starts (its phase): the grid runs back from there too.
    const double first = g.tempo.front().timeMs, bpm0 = g.tempo.front().bpm;
    g.finalize(bpm0);
    if (first > 0) {
        const double shift = g.beatAt(first);
        for (TempoPoint& tp : g.tempo) tp.beat -= shift;
    }
    return g;
}

// A time on the grid of `div` notes a beat.
double snapTo(const Chart& grid, double t, int div, long long& slot) {
    const double b = grid.beatAt(t) * div;
    slot = std::llround(b);
    return grid.msAtBeat(double(slot) / div);
}

struct Placed {
    double t;
    int side, lane;
    float strength;
    double length = 0;
    long long slot;
};

// Sides taking turns every four bars (16 beats), from the first beat.
int turnSide(const Chart& grid, double t) {
    const double b = grid.beatAt(t);
    return int(std::floor(b / 16.0)) % 2 == 0 ? 0 : 1;
}

const int kDiv[4] = {1, 2, 4, 8};

}  // namespace

const char* autoRoleName(AutoRole r) {
    switch (r) {
        case AutoRole::Player: return "Player";
        case AutoRole::Opponent: return "Opponent";
        case AutoRole::Turns: return "Both (turns)";
        default: return "Off";
    }
}

const char* autoLanesName(AutoLanes l) {
    switch (l) {
        case AutoLanes::Drums: return "Drums";
        case AutoLanes::Cycle: return "In turn";
        case AutoLanes::Keys: return "SNIFF keys";
        default: return "Pitch";
    }
}

bool detectTempo(const int16_t* samples, size_t frames, int channels, int sampleRate, double& bpm, double& beatMs) {
    const Analysis a = analyze(samples, frames, channels, sampleRate);
    return tempoOf(a, pickOnsets(a), bpm, beatMs);
}

// ----------------------------------------------------------------- model --

bool AutoModel::knows(const std::string& id) const { return std::find(songs.begin(), songs.end(), id) != songs.end(); }

double AutoModel::nps(double loudness) const {
    if (sn < 1) return 4.0;
    // Least squares with the slope held back (ridge): songs of one loudness
    // give their mean, not a line through noise.
    const double mx = sx / sn, my = sy / sn;
    const double var = std::max(0.0, sxx / sn - mx * mx), cov = sxy / sn - mx * my;
    const double b = cov / (var + 0.25);
    return std::clamp(my + b * (loudness - mx), 0.5, 40.0);
}

bool AutoModel::save(const std::string& path, std::string* err) const {
    std::ostringstream o;
    o.precision(9);
    o << "GHAM 1\n" << frames << " " << notes << " " << sx << " " << sy << " " << sxx << " " << sxy << " " << sn << "\n";
    auto put = [&](const Weights& w) {
        for (size_t i = 0; i < w.size(); ++i) o << (i ? " " : "") << w[i];
        o << "\n";
    };
    for (const Weights& w : side) put(w);
    for (const Weights& w : lane) put(w);
    for (const Weights& w : sideG) put(w);
    for (const Weights& w : laneG) put(w);
    o << songs.size() << "\n";
    for (const std::string& s : songs) o << s << "\n";
    const std::string tmp = path + ".part";
    {
        std::ofstream f(std::filesystem::path(std::u8string(tmp.begin(), tmp.end())), std::ios::binary);
        f << o.str();
        if (!f) {
            if (err) *err = "cannot write " + path;
            return false;
        }
    }
    std::error_code ec;
    const auto p8 = std::filesystem::path(std::u8string(path.begin(), path.end()));
    std::filesystem::rename(std::filesystem::path(std::u8string(tmp.begin(), tmp.end())), p8, ec);
    if (ec) {
        std::filesystem::remove(p8, ec);
        ec.clear();
        std::filesystem::rename(std::filesystem::path(std::u8string(tmp.begin(), tmp.end())), p8, ec);
    }
    if (ec && err) *err = "cannot write " + path;
    return !ec;
}

bool AutoModel::load(const std::string& path, std::string* err) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    std::string magic;
    int version = 0;
    if (!(f >> magic >> version) || magic != "GHAM" || version != 1) {
        if (err) *err = "not an AutoChart model";
        return false;
    }
    AutoModel m;
    f >> m.frames >> m.notes >> m.sx >> m.sy >> m.sxx >> m.sxy >> m.sn;
    auto get = [&](Weights& w) {
        for (float& v : w) f >> v;
    };
    for (Weights& w : m.side) get(w);
    for (Weights& w : m.lane) get(w);
    for (Weights& w : m.sideG) get(w);
    for (Weights& w : m.laneG) get(w);
    size_t n = 0;
    f >> n;
    std::string line;
    std::getline(f, line);
    for (size_t i = 0; i < n && std::getline(f, line); ++i) m.songs.push_back(line);
    if (!f && !f.eof()) {
        if (err) *err = "a damaged AutoChart model";
        return false;
    }
    *this = std::move(m);
    return true;
}

size_t autoLearnSong(AutoModel& m, const Chart& chart, const AutoSound& inst, const AutoSound& voiceP, const AutoSound& voiceO,
                     uint64_t seed) {
    if (!inst.ok() || chart.notes.empty()) return 0;
    const Analysis ai = analyze(inst.samples, inst.frames, inst.channels, inst.sampleRate);
    Analysis ap, ao;
    if (voiceP.ok()) ap = analyze(voiceP.samples, voiceP.frames, voiceP.channels, voiceP.sampleRate);
    if (voiceO.ok()) {
        if (voiceO.samples == voiceP.samples) ao = ap;  // one shared voice track: both
        else ao = analyze(voiceO.samples, voiceO.frames, voiceO.channels, voiceO.sampleRate);
    }
    Sources src{&ai, ap.nf ? &ap : nullptr, ao.nf ? &ao : nullptr};
    const size_t nf = src.nf();
    if (nf < 64) return 0;
    const std::vector<float> total = totalFlux(src), local = localMean(total);
    // Which frames have a note, of which side, in which lane.
    std::vector<int8_t> label(nf * 2, 0), laneOf(nf, -1);
    for (const Note& n : chart.notes) {
        if (n.kind != kNormal || n.lane >= chart.keysOf(n.side)) continue;
        const double fp = (n.time - kAttackMs - kHopMs * 0.5) / kHopMs;
        const long long c = std::llround(fp);
        for (long long f = c - 1; f <= c + 1; ++f) {
            if (f < 0 || size_t(f) >= nf) continue;
            label[size_t(f) * 2 + n.side] = 1;
            if (f == c) laneOf[size_t(f)] = int8_t(chart.keysOf(n.side) == 4 ? n.lane : n.lane * 4 / std::max(1, chart.keysOf(n.side)));
        }
    }
    size_t pos = 0;
    for (size_t f = 0; f < nf; ++f) pos += (label[f * 2] || label[f * 2 + 1]) ? 1 : 0;
    if (pos == 0) return 0;
    // Every frame with a note, and about four times as many without.
    const double keep = std::min(1.0, 4.0 * double(pos) / double(std::max<size_t>(1, nf - pos)));
    Rng rng(seed ^ (uint64_t(nf) * 0x9E3779B97F4A7C15ull));
    std::vector<size_t> order;
    for (size_t f = 0; f < nf; ++f)
        if (label[f * 2] || label[f * 2 + 1] || rng.chance(keep)) order.push_back(f);
    for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[size_t(rng.range(0, int(i) - 1))]);
    const Chart grid = [&] {
        Chart g;
        g.tempo = chart.tempo;
        if (g.tempo.empty()) g.tempo.push_back({0, 120, 0});
        g.finalize(g.tempo.front().bpm);
        return g;
    }();
    float x[AutoModel::kFeatures];
    size_t learned = 0;
    for (int epoch = 0; epoch < 2; ++epoch)
        for (size_t f : order) {
            featuresOf(src, total, local, grid, f, x);
            for (int s = 0; s < 2; ++s) {
                const float p = sigmoid(dot(m.side[size_t(s)], x));
                adagrad(m.side[size_t(s)], m.sideG[size_t(s)], x, p - float(label[f * 2 + size_t(s)]), 0.05f);
            }
            if (laneOf[f] >= 0) {
                float z[4], mx = -1e30f, sum = 0;
                for (int l = 0; l < 4; ++l) mx = std::max(mx, z[l] = dot(m.lane[size_t(l)], x));
                for (int l = 0; l < 4; ++l) sum += z[l] = std::exp(z[l] - mx);
                for (int l = 0; l < 4; ++l)
                    adagrad(m.lane[size_t(l)], m.laneG[size_t(l)], x, z[l] / sum - (l == laneOf[f] ? 1.0f : 0.0f), 0.05f);
                if (epoch == 0) ++learned;
            }
        }
    // Notes a second against loudness, over four-bar windows.
    const double end = std::min(chart.lengthMs, double(nf) * kHopMs);
    for (double from = 0; from + 1000 < end;) {
        const double bpm = chart.bpmAt(from), win = 16 * 60000.0 / std::max(30.0, bpm);
        const double to = std::min(end, from + win);
        const auto at = [&](double ms) {
            return std::lower_bound(chart.notes.begin(), chart.notes.end(), ms, [](const Note& n, double t) { return n.time < t; });
        };
        const double cnt = double(std::distance(at(from), at(to)));
        double loud = 0;
        size_t lf = 0;
        for (size_t f = size_t(from / kHopMs); f < std::min(nf, size_t(to / kHopMs)); ++f, ++lf) loud += local[f];
        if (lf > 0) {
            const double xl = loud / double(lf), y = cnt / ((to - from) / 1000.0);
            m.sx += xl;
            m.sy += y;
            m.sxx += xl * xl;
            m.sxy += xl * y;
            m.sn += 1;
        }
        from = to;
    }
    m.frames += nf;
    m.notes += pos;
    return learned;
}

namespace {
std::mutex gModelMtx;
std::string gModelPath;
bool gModelWanted = false;
std::shared_ptr<const AutoModel> gModel;
std::filesystem::file_time_type gModelTime{};
}  // namespace

void useAutoChartModel(const std::string& path, bool wanted) {
    std::lock_guard<std::mutex> lk(gModelMtx);
    if (path != gModelPath) gModel.reset();
    gModelPath = path;
    gModelWanted = wanted;
}

std::shared_ptr<const AutoModel> autoChartModel() {
    std::lock_guard<std::mutex> lk(gModelMtx);
    if (!gModelWanted || gModelPath.empty()) return nullptr;
    std::error_code ec;
    const auto p8 = std::filesystem::path(std::u8string(gModelPath.begin(), gModelPath.end()));
    const auto t = std::filesystem::last_write_time(p8, ec);
    if (ec) {
        gModel.reset();
        return nullptr;
    }
    if (!gModel || t != gModelTime) {
        auto m = std::make_shared<AutoModel>();
        gModel = m->load(gModelPath) && m->trained() ? std::shared_ptr<const AutoModel>(m) : nullptr;
        gModelTime = t;
    }
    return gModel;
}

// ----------------------------------------------------------- generation --

namespace {

// Audio tracks' notes without a model: each track's strongest onsets on the
// difficulty's grid, its role's side, lanes by its mode.
void notesFromAudio(const AutoTrack& tr, const Analysis& a, const Chart& grid, int difficulty, Rng& rng, std::vector<Placed>& out,
                    size_t& onsetsFound) {
    const std::vector<Onset> onsets = pickOnsets(a);
    onsetsFound += onsets.size();
    if (onsets.empty()) return;
    std::vector<double> st;
    for (const Onset& on : onsets) st.push_back(on.strength);
    const double base[4] = {0.55, 0.35, 0.2, 0.08};
    const double p = clampv(1.0 - (1.0 - base[difficulty]) * double(clampv(tr.density, 0.25f, 4.0f)), 0.0, 0.98);
    const double minStrength = percentile(st, p);
    const int div = kDiv[difficulty];
    std::vector<Onset> picked;
    std::vector<long long> slots;
    for (const Onset& on : onsets) {
        if (on.strength < minStrength) continue;
        long long k;
        const double snapped = snapTo(grid, on.t, div, k);
        const double slotMs = 60000.0 / grid.bpmAt(on.t) / div;
        if (std::fabs(snapped - on.t) > slotMs * 0.45 || snapped < 0) continue;
        if (!slots.empty() && slots.back() == k) {
            if (on.strength > picked.back().strength) {
                picked.back() = on;
                picked.back().t = snapped;
            }
            continue;
        }
        Onset q = on;
        q.t = snapped;
        picked.push_back(q);
        slots.push_back(k);
    }
    if (picked.empty()) return;
    std::vector<double> cents;
    for (const Onset& q : picked) cents.push_back(q.centroid);
    const double q1 = percentile(cents, 0.25), q2 = percentile(cents, 0.5), q3 = percentile(cents, 0.75);
    const double strongCut = percentile(st, 0.93);
    int cycle = 0;
    for (size_t i = 0; i < picked.size(); ++i) {
        const Onset& q = picked[i];
        int lane = 0;
        switch (tr.lanes) {
            case AutoLanes::Drums: {  // the band the hit is loudest in: kick left, snare and toms in the middle, hats right
                int b = 0;
                for (int k = 1; k < 4; ++k)
                    if (q.band[size_t(k)] > q.band[size_t(b)]) b = k;
                lane = b;
                break;
            }
            case AutoLanes::Cycle: lane = cycle++ % 4; break;
            default: lane = q.centroid < q1 ? 0 : q.centroid < q2 ? 1 : q.centroid < q3 ? 2 : 3; break;
        }
        const int side = tr.role == AutoRole::Player ? 1 : tr.role == AutoRole::Opponent ? 0 : turnSide(grid, q.t);
        Placed pl{q.t, side, lane, q.strength, 0, slots[i]};
        // A hold where the sound keeps ringing before the next one.
        const double beatMs = 60000.0 / grid.bpmAt(q.t);
        const double gap = i + 1 < picked.size() ? picked[i + 1].t - q.t : beatMs * 2;
        if (gap >= beatMs * (difficulty == 0 ? 1.5 : 1.0)) {
            const size_t end = std::min(a.nf - 1, q.frame + size_t(beatMs * 0.5 / kHopMs));
            double hold = 0;
            for (size_t j = q.frame; j <= end; ++j) hold += a.energy[j];
            hold /= double(end - q.frame + 1);
            if (hold > q.energy * 0.82) pl.length = std::floor((gap - beatMs * 0.25) / (beatMs / 4)) * (beatMs / 4);
        }
        out.push_back(pl);
        if (difficulty >= 2 && q.strength >= strongCut && gap >= 60000.0 / grid.bpmAt(q.t) / kDiv[difficulty]) {
            Placed chord = pl;
            chord.lane = (lane + 2) % 4;
            chord.length = 0;
            out.push_back(chord);
        }
    }
    (void)rng;
}

// Audio tracks' notes with the trained model: the roles say which part each
// sound is (the player's voice, the opponent's, the rest), the model where
// the notes go and whose they are, and how many a stretch this loud gets.
void notesFromModel(const AutoModel& m, const Sources& src, const Chart& grid, int difficulty, double densityScale,
                    std::vector<Placed>& out) {
    const size_t nf = src.nf();
    if (nf < 64) return;
    const std::vector<float> total = totalFlux(src), local = localMean(total);
    std::vector<float> ps[2] = {std::vector<float>(nf), std::vector<float>(nf)};
    std::vector<int> lanes(nf, 0);
    float x[AutoModel::kFeatures];
    for (size_t f = 0; f < nf; ++f) {
        featuresOf(src, total, local, grid, f, x);
        for (int s = 0; s < 2; ++s) ps[s][f] = sigmoid(dot(m.side[size_t(s)], x));
        int best = 0;
        float bz = -1e30f;
        for (int l = 0; l < 4; ++l) {
            const float z = dot(m.lane[size_t(l)], x);
            if (z > bz) {
                bz = z;
                best = l;
            }
        }
        lanes[f] = best;
    }
    struct Cand {
        size_t f;
        int side;
        float p;
    };
    const double scale[4] = {0.35, 0.6, 1.0, 1.6};
    const int div = kDiv[difficulty];
    for (double from = 0; from < double(nf) * kHopMs;) {
        const double win = 16 * 60000.0 / std::max(30.0, grid.bpmAt(from));
        const double to = std::min(double(nf) * kHopMs, from + win);
        const size_t f0 = size_t(from / kHopMs), f1 = std::min(nf, size_t(to / kHopMs) + 1);
        double loud = 0;
        for (size_t f = f0; f < f1; ++f) loud += local[f];
        loud /= double(std::max<size_t>(1, f1 - f0));
        std::vector<Cand> cands;
        for (size_t f = f0; f < f1; ++f)
            for (int s = 0; s < 2; ++s) {
                const std::vector<float>& p = ps[s];
                bool peak = p[f] > 0.05f;
                for (size_t j = f >= 2 ? f - 2 : 0; peak && j <= std::min(nf - 1, f + 2); ++j)
                    if (j != f && p[j] > p[f]) peak = false;
                if (peak) cands.push_back({f, s, p[f]});
            }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.p > b.p; });
        const size_t want = size_t(std::max(0.0, std::round(m.nps(loud) * (to - from) / 1000.0 * scale[difficulty] * densityScale)));
        std::vector<std::pair<long long, int>> used;
        size_t took = 0;
        for (const Cand& c : cands) {
            if (took >= want) break;
            long long slot;
            const double t = snapTo(grid, double(c.f) * kHopMs + kHopMs * 0.5 + kAttackMs, div, slot);
            if (t < 0 || std::find(used.begin(), used.end(), std::make_pair(slot, c.side)) != used.end()) continue;
            used.push_back({slot, c.side});
            out.push_back({t, c.side, lanes[c.f], c.p, 0, slot});
            ++took;
        }
        from = to;
    }
}

// An FL Studio channel's notes: exact, on the difficulty's grid.
void notesFromFlp(const AutoTrack& tr, const Chart& grid, int difficulty, std::vector<Placed>& out) {
    if (tr.flpNotes.empty()) return;
    std::vector<uint32_t> pitches;
    for (const FlpNote& n : tr.flpNotes) pitches.push_back(n.pitch);
    std::sort(pitches.begin(), pitches.end());
    pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
    const int div = kDiv[difficulty];
    int cycle = 0;
    for (const FlpNote& n : tr.flpNotes) {
        const double beat = double(n.time) / std::max(1, tr.ppqn);
        double t = grid.msAtBeat(beat);
        long long slot = 0;
        // The difficulty keeps what falls on its grid (Insane: every note).
        if (difficulty < 3) {
            const double b = beat * div;
            if (std::fabs(b - std::round(b)) > 0.08) continue;
            t = snapTo(grid, t, div, slot);
        } else {
            slot = std::llround(beat * 192);
        }
        int side = tr.role == AutoRole::Player ? 1 : tr.role == AutoRole::Opponent ? 0 : turnSide(grid, t);
        int lane = 0;
        switch (tr.lanes) {
            case AutoLanes::Keys:
                if (n.pitch >= 48 && n.pitch <= 51) {
                    side = 1;
                    lane = int(n.pitch - 48);
                } else if (n.pitch >= 60 && n.pitch <= 63) {
                    side = 0;
                    lane = int(n.pitch - 60);
                } else {
                    continue;
                }
                break;
            case AutoLanes::Drums: {
                const uint32_t p = n.pitch;
                lane = (p == 35 || p == 36) ? 0 : (p == 42 || p == 44) ? 1 : (p == 46 || p == 49 || p == 51 || p == 57) ? 2
                       : (p >= 37 && p <= 40) ? 3 : int(p % 4);
                break;
            }
            case AutoLanes::Cycle: lane = cycle++ % 4; break;
            default: {
                const size_t rank = size_t(std::lower_bound(pitches.begin(), pitches.end(), n.pitch) - pitches.begin());
                lane = int(rank * 4 / std::max<size_t>(1, pitches.size()));
                break;
            }
        }
        Placed pl{t, side, std::clamp(lane, 0, 3), float(n.velocity) / 128.0f, 0, slot};
        if (n.duration >= uint32_t(tr.ppqn)) pl.length = grid.msAtBeat(beat + (double(n.duration) - tr.ppqn / 4.0) / tr.ppqn) - t;
        out.push_back(pl);
    }
}

}  // namespace

bool autoChartSpec(const AutoChartSpec& spec, Chart& out, AutoChartInfo* info, std::string* err) {
    auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    const int difficulty = clampv(spec.difficulty, 0, 3);
    // Every audio track heard once.
    std::vector<Analysis> an(spec.tracks.size());
    std::vector<const Analysis*> heard;
    for (size_t i = 0; i < spec.tracks.size(); ++i) {
        const AutoTrack& t = spec.tracks[i];
        if (t.role == AutoRole::Ignore || !t.audio()) continue;
        an[i] = analyze(t.samples, t.frames, t.channels, t.sampleRate);
        if (an[i].nf) heard.push_back(&an[i]);
    }
    // The tempo: given, or found from the audio heard together.
    std::vector<TempoPoint> tempo = spec.tempo;
    double bpm = 0, phase = 0;
    size_t onsetsFound = 0;
    if (tempo.empty()) {
        if (heard.empty()) {
            bool flp = false;
            for (const AutoTrack& t : spec.tracks) flp = flp || (t.role != AutoRole::Ignore && !t.flpNotes.empty());
            if (!flp) return fail("no sound to chart: give a track a role");
            tempo.push_back({0, 120, 0});
        } else {
            const Analysis all = heard.size() == 1 ? *heard[0] : mix(heard);
            if (!tempoOf(all, pickOnsets(all), bpm, phase)) return fail("no rhythm found in the audio");
            tempo.push_back({phase, bpm, 0});
        }
    }
    const Chart grid = gridOf(tempo);
    if (bpm <= 0) bpm = grid.tempo.front().bpm;
    if (tempo.size() == 1 && tempo[0].timeMs > 0) phase = tempo[0].timeMs;

    std::vector<Placed> placed;
    Rng rng(spec.seed * 2654435761ull + uint64_t(difficulty));
    const bool useModel = spec.model && spec.model->trained() && !heard.empty();
    if (useModel) {
        // The parts of the song by role: the player's voice, the opponent's, the rest.
        std::vector<const Analysis*> parts[3];
        double density = 0;
        int counted = 0;
        for (size_t i = 0; i < spec.tracks.size(); ++i) {
            if (!an[i].nf) continue;
            const AutoRole r = spec.tracks[i].role;
            parts[r == AutoRole::Player ? 1 : r == AutoRole::Opponent ? 2 : 0].push_back(&an[i]);
            density += spec.tracks[i].density;
            ++counted;
        }
        Analysis mixed[3];
        Sources src;
        const Analysis** slot[3] = {&src.inst, &src.vp, &src.vo};
        for (int k = 0; k < 3; ++k) {
            if (parts[k].empty()) continue;
            mixed[k] = parts[k].size() == 1 ? *parts[k][0] : mix(parts[k]);
            *slot[k] = &mixed[k];
        }
        if (!src.inst) src.inst = src.vp ? src.vp : src.vo;
        notesFromModel(*spec.model, src, grid, difficulty, counted ? density / counted : 1.0, placed);
        for (const Analysis* a : heard) onsetsFound += pickOnsets(*a).size();
    } else {
        for (size_t i = 0; i < spec.tracks.size(); ++i)
            if (an[i].nf) notesFromAudio(spec.tracks[i], an[i], grid, difficulty, rng, placed, onsetsFound);
    }
    for (const AutoTrack& t : spec.tracks)
        if (t.role != AutoRole::Ignore && !t.audio()) notesFromFlp(t, grid, difficulty, placed);
    if (placed.empty()) return fail("no notes: the tracks gave none at this difficulty");

    // One note a lane a slot; at most one (Easy, Normal) or two notes a side at once.
    std::sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) {
        if (a.t != b.t) return a.t < b.t;
        if (a.side != b.side) return a.side < b.side;
        return a.strength > b.strength;
    });
    const int maxChord = difficulty >= 2 ? 2 : 1;
    std::vector<Placed> kept;
    for (const Placed& p : placed) {
        int same = 0;
        bool lane = false;
        for (size_t j = kept.size(); j-- > 0 && std::fabs(kept[j].t - p.t) < 1.0;) {
            if (kept[j].side != p.side) continue;
            ++same;
            lane = lane || kept[j].lane == p.lane;
        }
        if (lane || same >= maxChord) continue;
        kept.push_back(p);
    }
    // No fast jacks: a lane twice within three quarters of a beat moves over.
    int prevLane[2] = {-1, -1};
    double prevT[2] = {-1e9, -1e9};
    for (Placed& p : kept) {
        const double beatMs = 60000.0 / grid.bpmAt(p.t);
        if (p.lane == prevLane[p.side] && p.t - prevT[p.side] < beatMs * 0.75 && p.t - prevT[p.side] > 1.0)
            p.lane = (p.lane + (rng.chance(0.5) ? 1 : 3)) % 4;
        prevLane[p.side] = p.lane;
        prevT[p.side] = p.t;
    }

    out = Chart();
    out.keys = 4;
    out.autoCharted = true;
    out.speed = difficulty == 0 ? 1.7 : difficulty == 1 ? 2.1 : difficulty == 2 ? 2.5 : 3.0;
    out.tempo = grid.tempo;
    for (TempoPoint& tp : out.tempo) tp.beat = 0;
    for (const Placed& p : kept) {
        Note n;
        n.time = p.t;
        n.side = uint8_t(p.side);
        n.lane = uint16_t(p.lane);
        n.length = float(std::max(0.0, p.length));
        out.notes.push_back(n);
    }
    out.rawNoteCount = Count(uint64_t(out.notes.size()));
    out.finalize(out.tempo.front().bpm);
    // Sections of four beats from the first beat, the camera on whoever has more notes there.
    {
        const double endMs = out.lengthMs + 1;
        const double startBeat = std::floor(grid.beatAt(0) / 4.0) * 4.0;
        for (double b = startBeat;; b += 4) {
            const double t0 = grid.msAtBeat(b), t1 = grid.msAtBeat(b + 4);
            if (t0 > endMs) break;
            int cnt[2] = {0, 0};
            for (const Note& n : out.notes)
                if (n.time >= t0 && n.time < t1) ++cnt[n.side];
            const bool mustHit = cnt[1] >= cnt[0];
            out.sections.push_back({std::max(0.0, t0), mustHit, false, false, 4, grid.bpmAt(std::max(0.0, t0))});
            if (cnt[0] + cnt[1] > 0) {
                const uint8_t side = mustHit ? 1 : 0;
                if (out.focus.empty() || out.focus.back().side != side) out.focus.push_back({std::max(0.0, t0), side});
            }
        }
    }
    // Beat numbers from the grid's first beat.
    for (size_t i = 0; i < out.tempo.size(); ++i) out.tempo[i].beat = grid.tempo[std::min(i, grid.tempo.size() - 1)].beat;
    if (info) {
        info->bpm = bpm;
        info->firstBeatMs = phase;
        info->onsets = onsetsFound;
        info->trained = useModel;
    }
    return !out.notes.empty() ? true : fail("no notes");
}

bool autoChart(const int16_t* samples, size_t frames, int channels, int sampleRate, int difficulty, Chart& out, AutoChartInfo* info,
               const AutoModel* model) {
    if (!samples || frames < size_t(sampleRate) * 4 || channels < 1 || sampleRate < 8000) return false;
    AutoChartSpec spec;
    AutoTrack t;
    t.name = "Song";
    t.samples = samples;
    t.frames = frames;
    t.channels = channels;
    t.sampleRate = sampleRate;
    t.role = AutoRole::Turns;
    spec.tracks.push_back(t);
    spec.difficulty = clampv(difficulty, 0, 2);
    spec.model = model;
    spec.seed = uint64_t(frames);
    return autoChartSpec(spec, out, info);
}

}  // namespace gh
