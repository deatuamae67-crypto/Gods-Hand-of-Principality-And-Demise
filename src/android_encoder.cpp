// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "android_encoder.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <unistd.h>
#endif

namespace gh {

void rgbaToYuv420(const uint8_t* src, int w, int h, uint8_t* y, int yStride, uint8_t* u, uint8_t* v, int uvStride, int uvStep) {
    // BT.709 with video levels (Y 16..235, U and V 16..240), in fixed point.
    const size_t row = size_t(w) * 4;
    for (int r = 0; r < h; ++r) {
        const uint8_t* s = src + size_t(h - 1 - r) * row;  // the picture is stored bottom row first
        uint8_t* yo = y + size_t(r) * size_t(yStride);
        for (int x = 0; x < w; ++x, s += 4) yo[x] = uint8_t(16 + ((47 * s[0] + 157 * s[1] + 16 * s[2] + 128) >> 8));
    }
    for (int r = 0; r < h / 2; ++r) {
        const uint8_t* a = src + size_t(h - 1 - 2 * r) * row;
        const uint8_t* b = src + size_t(h - 2 - 2 * r) * row;
        uint8_t* uo = u + size_t(r) * size_t(uvStride);
        uint8_t* vo = v + size_t(r) * size_t(uvStride);
        for (int x = 0; x < w / 2; ++x, a += 8, b += 8) {
            // The four pixels of the 2x2 block, added up.
            const int R = a[0] + a[4] + b[0] + b[4], G = a[1] + a[5] + b[1] + b[5], B = a[2] + a[6] + b[2] + b[6];
            uo[size_t(x) * size_t(uvStep)] = uint8_t((-26 * R - 86 * G + 112 * B + (128 << 10) + 512) >> 10);
            vo[size_t(x) * size_t(uvStep)] = uint8_t((112 * R - 102 * G - 10 * B + (128 << 10) + 512) >> 10);
        }
    }
}

#if defined(__ANDROID__)

namespace {

constexpr int32_t kColorPlanar = 19, kColorSemiPlanar = 21;  // COLOR_FormatYUV420Planar / SemiPlanar

void logLine(const std::string& s) { __android_log_print(ANDROID_LOG_INFO, "GodsHand", "%s", s.c_str()); }

}  // namespace

struct AndroidEncoder::Impl {
    AMediaCodec* venc = nullptr;
    AMediaCodec* aenc = nullptr;
    AMediaMuxer* mux = nullptr;
    int fd = -1;
    int w = 0, h = 0, fps = 60, stride = 0, slice = 0;
    bool semiPlanar = true, wantAudio = false, started = false;
    int vTrack = -1, aTrack = -1;
    int64_t frame = 0;
    uint64_t audioFrames = 0, written = 0;
    struct Sample {
        bool audio;
        std::vector<uint8_t> data;
        AMediaCodecBufferInfo info;
    };
    std::vector<Sample> early;  // encoded before the muxer could start

    void startMuxer() {
        if (started || vTrack < 0 || (wantAudio && aTrack < 0)) return;
        if (AMediaMuxer_start(mux) != AMEDIA_OK) return;
        started = true;
        for (Sample& s : early) {
            const int t = s.audio ? aTrack : vTrack;
            if (t >= 0) AMediaMuxer_writeSampleData(mux, size_t(t), s.data.data(), &s.info);
        }
        early.clear();
    }

    // Takes what the encoder has made; untilEnd: waits for its last buffer.
    bool drain(AMediaCodec* c, bool audio, bool untilEnd) {
        for (int idle = 0;;) {
            AMediaCodecBufferInfo info{};
            const ssize_t o = AMediaCodec_dequeueOutputBuffer(c, &info, untilEnd ? 10000 : 0);
            if (o == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                if (!untilEnd) return true;
                if (++idle > 500) return false;  // 5 seconds without its end
                continue;
            }
            idle = 0;
            if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* f = AMediaCodec_getOutputFormat(c);
                const ssize_t t = started ? -1 : AMediaMuxer_addTrack(mux, f);
                AMediaFormat_delete(f);
                (audio ? aTrack : vTrack) = int(t);
                startMuxer();
                continue;
            }
            if (o == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            if (o < 0) return false;
            size_t cap = 0;
            uint8_t* data = AMediaCodec_getOutputBuffer(c, size_t(o), &cap);
            const bool end = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            // The codec's setup (SPS/PPS, AAC config) reaches the muxer in its format.
            if (data && info.size > 0 && !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
                const int track = audio ? aTrack : vTrack;
                if (started && track >= 0) {
                    AMediaMuxer_writeSampleData(mux, size_t(track), data, &info);
                    if (!audio) ++written;
                } else if (!started) {
                    Sample s{audio, std::vector<uint8_t>(data + info.offset, data + info.offset + info.size), info};
                    s.info.offset = 0;
                    early.push_back(std::move(s));
                    if (!audio) ++written;
                }
            }
            AMediaCodec_releaseOutputBuffer(c, size_t(o), false);
            if (end) return true;
        }
    }

    // An input buffer of the encoder, taking its output meanwhile if it is full.
    ssize_t input(AMediaCodec* c, bool audio) {
        for (int i = 0; i < 300; ++i) {
            const ssize_t idx = AMediaCodec_dequeueInputBuffer(c, 10000);
            if (idx >= 0) return idx;
            if (!drain(c, audio, false)) return -1;
        }
        return -1;
    }

    bool configureVideo(int32_t color, int mbps) {
        AMediaFormat* f = AMediaFormat_new();
        AMediaFormat_setString(f, "mime", "video/avc");
        AMediaFormat_setInt32(f, "width", w);
        AMediaFormat_setInt32(f, "height", h);
        AMediaFormat_setInt32(f, "bitrate", std::clamp(mbps, 1, 200) * 1000000);
        AMediaFormat_setInt32(f, "frame-rate", fps);
        AMediaFormat_setInt32(f, "i-frame-interval", 1);
        AMediaFormat_setInt32(f, "color-format", color);
        const media_status_t st = AMediaCodec_configure(venc, f, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        AMediaFormat_delete(f);
        return st == AMEDIA_OK;
    }

    void release() {
        for (AMediaCodec** c : {&venc, &aenc})
            if (*c) {
                AMediaCodec_stop(*c);
                AMediaCodec_delete(*c);
                *c = nullptr;
            }
        if (mux) {
            if (started) AMediaMuxer_stop(mux);
            AMediaMuxer_delete(mux);
            mux = nullptr;
        }
        if (fd >= 0) close(fd);
        fd = -1;
    }
};

AndroidEncoder::AndroidEncoder() : d_(std::make_unique<Impl>()) {}
AndroidEncoder::~AndroidEncoder() { d_->release(); }

bool AndroidEncoder::start(const std::string& path, int width, int height, int fps, int bitrateMbps, bool audio, std::string& err) {
    Impl& d = *d_;
    d.w = width & ~1;
    d.h = height & ~1;
    d.fps = std::max(1, fps);
    d.wantAudio = audio;
    d.fd = open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (d.fd < 0) {
        err = "cannot write " + path;
        return false;
    }
    d.mux = AMediaMuxer_new(d.fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    d.venc = AMediaCodec_createEncoderByType("video/avc");
    if (!d.mux || !d.venc) {
        err = "this device has no H.264 encoder";
        d.release();
        return false;
    }
    // The pixel layouts every Android encoder takes: NV12, else I420.
    if (d.configureVideo(kColorSemiPlanar, bitrateMbps)) {
        d.semiPlanar = true;
    } else if (d.configureVideo(kColorPlanar, bitrateMbps)) {
        d.semiPlanar = false;
    } else {
        err = "this device cannot encode " + std::to_string(d.w) + "x" + std::to_string(d.h) + " video (try a smaller video size)";
        d.release();
        return false;
    }
    d.stride = d.w;
    d.slice = d.h;
    // Android 9+: the encoder says how it lays out its rows (some pad them).
    using InputFormatFn = AMediaFormat* (*)(AMediaCodec*);
    if (auto fn = reinterpret_cast<InputFormatFn>(dlsym(RTLD_DEFAULT, "AMediaCodec_getInputFormat"))) {
        if (AMediaFormat* f = fn(d.venc)) {
            int32_t v = 0;
            if (AMediaFormat_getInt32(f, "stride", &v) && v >= d.w && v < d.w * 2) d.stride = v;
            if (AMediaFormat_getInt32(f, "slice-height", &v) && v >= d.h && v < d.h * 2) d.slice = v;
            AMediaFormat_delete(f);
        }
    }
    if (AMediaCodec_start(d.venc) != AMEDIA_OK) {
        err = "the video encoder did not start";
        d.release();
        return false;
    }
    if (audio) {
        d.aenc = AMediaCodec_createEncoderByType("audio/mp4a-latm");
        AMediaFormat* f = AMediaFormat_new();
        AMediaFormat_setString(f, "mime", "audio/mp4a-latm");
        AMediaFormat_setInt32(f, "sample-rate", 44100);
        AMediaFormat_setInt32(f, "channel-count", 2);
        AMediaFormat_setInt32(f, "bitrate", 192000);
        AMediaFormat_setInt32(f, "aac-profile", 2);  // AAC-LC
        AMediaFormat_setInt32(f, "max-input-size", 16384);
        const bool ok = d.aenc && AMediaCodec_configure(d.aenc, f, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK &&
                        AMediaCodec_start(d.aenc) == AMEDIA_OK;
        AMediaFormat_delete(f);
        if (!ok) {  // a video without its sound is still a video
            if (d.aenc) AMediaCodec_delete(d.aenc);
            d.aenc = nullptr;
            d.wantAudio = false;
            logLine("rendering mode: no AAC encoder, the video has no sound");
        }
    }
    logLine("rendering mode: " + std::to_string(d.w) + "x" + std::to_string(d.h) + (d.semiPlanar ? " NV12" : " I420") +
            ", stride " + std::to_string(d.stride) + ", rows " + std::to_string(d.slice));
    return true;
}

bool AndroidEncoder::video(const uint8_t* rgba) {
    Impl& d = *d_;
    const ssize_t idx = d.input(d.venc, false);
    if (idx < 0) return false;
    size_t cap = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(d.venc, size_t(idx), &cap);
    const size_t ySize = size_t(d.stride) * size_t(d.slice), need = ySize + ySize / 2;
    if (!buf || cap < need) return false;
    if (d.semiPlanar) {
        rgbaToYuv420(rgba, d.w, d.h, buf, d.stride, buf + ySize, buf + ySize + 1, d.stride, 2);
    } else {
        uint8_t* u = buf + ySize;
        rgbaToYuv420(rgba, d.w, d.h, buf, d.stride, u, u + ySize / 4, d.stride / 2, 1);
    }
    const int64_t pts = d.frame * 1000000 / d.fps;
    ++d.frame;
    if (AMediaCodec_queueInputBuffer(d.venc, size_t(idx), 0, need, uint64_t(pts), 0) != AMEDIA_OK) return false;
    return d.drain(d.venc, false, false);
}

void AndroidEncoder::audio(const int16_t* pcm, size_t frames) {
    Impl& d = *d_;
    if (!d.aenc) return;
    while (frames > 0) {
        const ssize_t idx = d.input(d.aenc, true);
        if (idx < 0) return;
        size_t cap = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(d.aenc, size_t(idx), &cap);
        if (!buf || cap < 4) return;
        const size_t n = std::min(frames, cap / 4);
        std::memcpy(buf, pcm, n * 4);
        const uint64_t pts = d.audioFrames * 1000000ull / 44100ull;
        AMediaCodec_queueInputBuffer(d.aenc, size_t(idx), 0, n * 4, pts, 0);
        d.audioFrames += n;
        pcm += n * 2;
        frames -= n;
        d.drain(d.aenc, true, false);
    }
}

bool AndroidEncoder::finish(std::string& err) {
    Impl& d = *d_;
    // The end of each stream, then everything they still hold.
    const uint64_t vEnd = uint64_t(d.frame * 1000000 / d.fps), aEnd = d.audioFrames * 1000000ull / 44100ull;
    for (AMediaCodec* c : {d.venc, d.aenc}) {
        if (!c) continue;
        const bool audio = c == d.aenc;
        const ssize_t idx = d.input(c, audio);
        if (idx >= 0) AMediaCodec_queueInputBuffer(c, size_t(idx), 0, 0, audio ? aEnd : vEnd, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        d.drain(c, audio, true);
    }
    if (!d.started && d.vTrack >= 0) {  // no sound ever came: the video alone
        d.wantAudio = false;
        d.startMuxer();
    }
    const bool ok = d.started && d.written > 0;
    d.release();
    if (!ok) err = "the video encoder made no frames";
    return ok;
}

#else  // not Android: only the colour conversion (tested on every system)

struct AndroidEncoder::Impl {};
AndroidEncoder::AndroidEncoder() : d_(std::make_unique<Impl>()) {}
AndroidEncoder::~AndroidEncoder() = default;
bool AndroidEncoder::start(const std::string&, int, int, int, int, bool, std::string& err) {
    err = "Android only";
    return false;
}
bool AndroidEncoder::video(const uint8_t*) { return false; }
void AndroidEncoder::audio(const int16_t*, size_t) {}
bool AndroidEncoder::finish(std::string& err) {
    err = "Android only";
    return false;
}

#endif

}  // namespace gh
