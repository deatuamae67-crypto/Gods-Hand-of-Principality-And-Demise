// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Audio: a small mixer that owns the song clock, plus generated sound effects.
#pragma once

#include <atomic>
#include <deque>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gh {

struct PcmStream;  // audio_decode.hpp

// 16-bit PCM at 44.1 kHz, mono or interleaved stereo: all of it in `data`,
// or (a song file) decoded in the background into `stream` while it plays.
struct Pcm {
    std::vector<int16_t> data;
    int channels = 2;
    std::shared_ptr<PcmStream> stream;  // set: `data` stays empty
    size_t streamFrames = 0;            // the stream's length
    size_t frames() const { return stream ? streamFrames : (channels > 0 ? data.size() / size_t(channels) : 0); }
};
using PcmPtr = std::shared_ptr<const Pcm>;

// Decodes a whole ogg/mp3/wav (or qoa, through raylib) file to 44.1 kHz.
bool loadAudioFile(const std::string& path, Pcm& out, std::string& err);
// A song file: streamed (playable at once) when it is ogg/mp3/wav, else
// decoded whole. `startFraction`: where playing will start. Null on failure.
PcmPtr loadSongAudio(const std::string& path, double startFraction = 0.0);

// The mixer feeds one raylib AudioStream from a callback and counts the exact
// number of frames handed to the device; time() derives a smooth, monotonic
// song position from that counter. Without an audio device it falls back to a
// steady clock so the game stays playable (and testable) anyway.
class Mixer {
public:
    static constexpr int kRate = 44100;
    enum Track { kInst = 0, kVoicePlayer = 1, kVoiceOpponent = 2, kTrackCount = 3 };

    bool init();
    void shutdown();
    bool audioOk() const { return audioOk_; }

    void setTracks(PcmPtr inst, PcmPtr voicePlayer, PcmPtr voiceOpponent);
    void clear() { setTracks(nullptr, nullptr, nullptr); }
    void play(double fromSec);          // may be negative (silent lead-in)
    void pause();
    void resume();
    void stop();
    void seek(double sec);
    bool playing() const { return playing_.load(); }
    bool paused() const { return paused_; }
    void setLoop(bool loop, double startSec = 0, double endSec = -1);
    void setRate(double r);
    double rate() const { return rate_.load(); }
    void setTrackVolume(int track, float v);
    void setVolume(float v) { volume_.store(v); }  // music volume (master is raylib's)
    void fadeTo(float v, float seconds);           // smooth volume ramp

    double time(double now);  // smoothed position in seconds
    double rawTime() const;   // position of the last mixed frame
    double lengthSec() const;
    bool ended() const;
    // A streamed track has not decoded the part being played yet (after a
    // jump far ahead): the song clock waits for it.
    bool waitingForAudio() const { return stalled_.load(); }

    void audioCallback(float* out, unsigned frames);  // audio thread

    // Hitsounds that follow the notes (issue #22): each one starts on the
    // very sample of its note in the song, on a voice of its side that the
    // next note cuts (a sampler set to cut itself). A fast spam becomes a
    // tone at the rate of its notes (16 a step at 520 BPM: 554.67 Hz)
    // instead of a louder pile that crackles, and a hitch of the game does
    // not move them. side: 0 player, 1 opponent.
    void hitAt(double songSec, int side, float volume);
    void clearHits();
    void setHitSound(std::vector<float> mono);  // instead of the game's (or the mod's) hitsound

    // Rendering mode: the device plays nothing and the game mixes the song
    // itself, one video frame at a time; the song clock is then exactly the
    // position mixed so far.
    void setOffline(bool on);
    bool offline() const { return offline_.load(); }
    void mixOffline(float* out, unsigned frames);  // the next `frames` stereo frames

private:
    double estimate(double now) const;

    std::mutex mtx_;  // guards track pointers
    PcmPtr tracks_[kTrackCount];
    std::atomic<double> pos_{0.0};        // frames
    std::atomic<double> cbTime_{0.0};     // steady time of last callback
    std::atomic<unsigned> cbFrames_{0};
    std::atomic<bool> playing_{false};
    std::atomic<double> rate_{1.0};
    std::atomic<float> trackVol_[kTrackCount];
    std::atomic<float> volume_{1.0f};
    std::atomic<float> fadeTarget_{1.0f};
    std::atomic<float> fadeStep_{0.0f};
    std::atomic<bool> loop_{false};
    std::atomic<bool> stalled_{false};
    std::atomic<bool> offline_{false};
    void hintStreams(double sec);  // tells streamed tracks where playing goes on
    // The hitsounds of this buffer, from song frame pos0 on (audio thread).
    void mixHits(float* out, unsigned frames, double pos0, double rate);
    struct Grain {
        double at = 0;  // song frame
        float vol = 1;
    };
    std::mutex hitMtx_;  // guards hits_ and hitPcm_
    std::deque<Grain> hits_[2];
    std::shared_ptr<const std::vector<float>> hitPcm_, hitPcmAudio_;
    unsigned hitVersion_ = ~0u;
    struct HitVoice {
        double pos = -1, oldPos = -1;  // in the sound; -1 silent
        float vol = 0, oldVol = 0;
        int fade = 0;  // frames left of the cut sound's fade-out
    };
    HitVoice voice_[2];
    std::vector<Grain> due_[2];  // this buffer's (audio thread)
    // Mixes `frames` frames from `pos` into out (pos moves on); true when a
    // streamed track has not decoded that part yet (silence, pos kept).
    bool mixTracks(float* out, unsigned frames, double& pos);
    std::atomic<double> loopStart_{0}, loopEnd_{0};
    float curVol_[kTrackCount] = {1, 1, 1};
    float fadeGain_ = 1.0f;
    bool audioOk_ = false;
    bool paused_ = false;
    void* stream_ = nullptr;  // AudioStream*
    // fallback clock
    double fbStart_ = 0, fbStartPos_ = 0;
    // smoothing
    bool haveLast_ = false;
    double lastValue_ = 0, lastNow_ = 0;
};

// Procedurally generated sound effects.
enum class Sfx { Scroll, Confirm, Cancel, Hit, Miss, Mine, Count3, Count2, Count1, CountGo, Death, Unlock, kCount };
void sfxInit();
void sfxShutdown();
void sfxPlay(Sfx s, float volume = 1.0f, float pitch = 1.0f);
void sfxSetVolume(float v);
// The samples of a sound effect (the mod's when it overrides it), mono,
// 44.1 kHz; sfxVersion() changes whenever the overrides do.
std::vector<float> sfxSamples(Sfx id);
unsigned sfxVersion();
// Plays the sound in `path` (any audio file the game reads) for `id` from
// now on; an empty path goes back to the game's own sound.
bool sfxOverride(Sfx id, const std::string& path);
void sfxClearOverrides();

}  // namespace gh
