// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "render_mode.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <thread>

#include "android_encoder.hpp"
#include "platform.hpp"
#include "rlgl.h"

#if !defined(__ANDROID__)
// raylib's GLFW: where the OpenGL functions raylib does not wrap come from.
typedef void (*GhGlProc)(void);
extern "C" GhGlProc glfwGetProcAddress(const char* procname);
#endif

namespace gh {

namespace fs = std::filesystem;

namespace {

#if !defined(__ANDROID__)
// OpenGL 3.x functions for reading frames back without waiting (PBOs).
struct GlPbo {
    void (*GenBuffers)(int, unsigned*) = nullptr;
    void (*DeleteBuffers)(int, const unsigned*) = nullptr;
    void (*BindBuffer)(unsigned, unsigned) = nullptr;
    void (*BufferData)(unsigned, std::ptrdiff_t, const void*, unsigned) = nullptr;
    void* (*MapBufferRange)(unsigned, std::ptrdiff_t, std::ptrdiff_t, unsigned) = nullptr;
    unsigned char (*UnmapBuffer)(unsigned) = nullptr;
    void (*BindFramebuffer)(unsigned, unsigned) = nullptr;
    void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void*) = nullptr;
    bool ok = false;
};
constexpr unsigned kPixelPackBuffer = 0x88EB, kStreamRead = 0x88E1, kMapRead = 0x0001, kReadFramebuffer = 0x8CA8,
                   kRgba = 0x1908, kUnsignedByte = 0x1401;

const GlPbo& glPbo() {
    static const GlPbo g = [] {
        GlPbo f;
        auto get = [](const char* n) { return reinterpret_cast<void*>(glfwGetProcAddress(n)); };
        f.GenBuffers = reinterpret_cast<decltype(f.GenBuffers)>(get("glGenBuffers"));
        f.DeleteBuffers = reinterpret_cast<decltype(f.DeleteBuffers)>(get("glDeleteBuffers"));
        f.BindBuffer = reinterpret_cast<decltype(f.BindBuffer)>(get("glBindBuffer"));
        f.BufferData = reinterpret_cast<decltype(f.BufferData)>(get("glBufferData"));
        f.MapBufferRange = reinterpret_cast<decltype(f.MapBufferRange)>(get("glMapBufferRange"));
        f.UnmapBuffer = reinterpret_cast<decltype(f.UnmapBuffer)>(get("glUnmapBuffer"));
        f.BindFramebuffer = reinterpret_cast<decltype(f.BindFramebuffer)>(get("glBindFramebuffer"));
        f.ReadPixels = reinterpret_cast<decltype(f.ReadPixels)>(get("glReadPixels"));
        f.ok = f.GenBuffers && f.DeleteBuffers && f.BindBuffer && f.BufferData && f.MapBufferRange && f.UnmapBuffer &&
               f.BindFramebuffer && f.ReadPixels;
        return f;
    }();
    return g;
}
#endif

fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::atomic<int> g_busy{0};
std::mutex g_msgMtx;
std::string g_message;

void setMessage(const std::string& m) {
    std::lock_guard<std::mutex> lk(g_msgMtx);
    g_message = m;
}

void removeFile(const std::string& p) {
    std::error_code ec;
    fs::remove(u8path(p), ec);
}

bool moveFile(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::rename(u8path(from), u8path(to), ec);
    if (!ec) return true;
    fs::copy_file(u8path(from), u8path(to), fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    removeFile(from);
    return true;
}

void put32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
void put16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }

}  // namespace

const std::vector<RenderEncoder>& renderEncoders() {
    static const std::vector<RenderEncoder> list = {
        {"H.264 (x264)", "mp4"}, {"H.265 (x265)", "mp4"}, {"VP9 (WebM)", "webm"}, {"H.264 NVIDIA", "mp4"},
        {"H.265 NVIDIA", "mp4"}, {"H.264 AMD", "mp4"},    {"H.264 Intel", "mp4"},
    };
    return list;
}

std::vector<std::string> renderVideoArgs(const RenderOptions& o, const std::string& out) {
    const std::string br = std::to_string(std::max(1, o.bitrateMbps)) + "M";
    std::vector<std::string> a = {"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba", "-s",
                                  std::to_string(o.width) + "x" + std::to_string(o.height), "-r", std::to_string(o.fps), "-i", "-",
                                  "-vf", "vflip"};
    switch (o.encoder) {
        case 1: a.insert(a.end(), {"-c:v", "libx265", "-preset", "fast", "-b:v", br, "-pix_fmt", "yuv420p", "-tag:v", "hvc1"}); break;
        case 2:
            a.insert(a.end(), {"-c:v", "libvpx-vp9", "-b:v", br, "-deadline", "realtime", "-cpu-used", "8", "-row-mt", "1",
                               "-pix_fmt", "yuv420p"});
            break;
        case 3: a.insert(a.end(), {"-c:v", "h264_nvenc", "-preset", "p4", "-b:v", br, "-pix_fmt", "yuv420p"}); break;
        case 4: a.insert(a.end(), {"-c:v", "hevc_nvenc", "-preset", "p4", "-b:v", br, "-pix_fmt", "yuv420p", "-tag:v", "hvc1"}); break;
        case 5: a.insert(a.end(), {"-c:v", "h264_amf", "-b:v", br, "-pix_fmt", "yuv420p"}); break;
        case 6: a.insert(a.end(), {"-c:v", "h264_qsv", "-b:v", br, "-pix_fmt", "nv12"}); break;
        default:
            a.insert(a.end(), {"-c:v", "libx264", "-preset", "veryfast", "-b:v", br, "-maxrate", br, "-bufsize",
                               std::to_string(std::max(1, o.bitrateMbps) * 2) + "M", "-pix_fmt", "yuv420p"});
            break;
    }
    a.push_back(out);
    return a;
}

std::vector<std::string> renderMuxArgs(const RenderOptions& o, const std::string& video, const std::string& wav,
                                       const std::string& out) {
    const bool webm = o.encoder == 2;
    std::vector<std::string> a = {"-hide_banner", "-loglevel", "error", "-y", "-i", video, "-i", wav, "-map", "0:v", "-map", "1:a",
                                  "-c:v", "copy"};
    if (webm) a.insert(a.end(), {"-c:a", "libopus", "-b:a", "160k"});
    else a.insert(a.end(), {"-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart"});
    a.insert(a.end(), {"-shortest", out});
    return a;
}

VideoRenderer::VideoRenderer() = default;

VideoRenderer::~VideoRenderer() {
    std::string err;
    if (active_) finish(err);
}

bool VideoRenderer::start(const std::string& ffmpeg, const std::string& outBase, const RenderOptions& o, std::string& err) {
    if (active_) finish(err);
    err.clear();
    opt_ = o;
    opt_.width = std::max(16, o.width & ~1);  // the encoders want even sizes
    opt_.height = std::max(16, o.height & ~1);
    opt_.fps = std::clamp(o.fps, 1, 1000);
    if (opt_.encoder < 0 || opt_.encoder >= int(renderEncoders().size())) opt_.encoder = 0;
    if (platform::kAndroid) opt_.encoder = 0;  // the phone's H.264 encoder, in an .mp4
    const std::string ext = renderEncoders()[size_t(opt_.encoder)].ext;
    ffmpeg_ = ffmpeg;
    outBase_ = outBase;
    final_ = outBase + "." + ext;
    videoTmp_ = outBase + ".tmp." + ext;
    wavTmp_ = outBase + ".tmp.wav";
    log_ = outBase + ".log";
    std::error_code ec;
    fs::create_directories(u8path(outBase).parent_path(), ec);
    rt_ = LoadRenderTexture(opt_.width, opt_.height);
    if (!rt_.id) {
        err = "could not make a " + std::to_string(opt_.width) + "x" + std::to_string(opt_.height) + " picture to record";
        return false;
    }
    SetTextureFilter(rt_.texture, TEXTURE_FILTER_BILINEAR);
    frames_ = 0;
    audioFrames_ = 0;
    failed_ = false;
    if (platform::kAndroid) {
        // Straight into the finished .mp4, sound included.
        android_ = std::make_unique<AndroidEncoder>();
        if (!android_->start(final_, opt_.width, opt_.height, opt_.fps, opt_.bitrateMbps, opt_.audio, err)) {
            android_.reset();
            UnloadRenderTexture(rt_);
            rt_ = {};
            removeFile(final_);
            return false;
        }
        galleryName_ = u8path(final_).filename().string();
        startWorker();
        active_ = true;
        return true;
    }
    if (!proc_.start(ffmpeg, renderVideoArgs(opt_, videoTmp_), log_)) {
        UnloadRenderTexture(rt_);
        rt_ = {};
        err = "could not start ffmpeg";
        return false;
    }
    usePbo_ = initPbo();
    startWorker();
    if (opt_.audio) {
        wav_ = std::fopen(wavTmp_.c_str(), "wb");
        if (wav_) {
            std::fwrite("RIFF", 1, 4, wav_);
            put32(wav_, 36);  // sizes written at the end
            std::fwrite("WAVEfmt ", 1, 8, wav_);
            put32(wav_, 16);
            put16(wav_, 1);
            put16(wav_, 2);
            put32(wav_, 44100);
            put32(wav_, 44100 * 4);
            put16(wav_, 4);
            put16(wav_, 16);
            std::fwrite("data", 1, 4, wav_);
            put32(wav_, 0);
        }
    }
    active_ = true;
    return true;
}

// ------------------------------------------------- the encoder's thread --

void VideoRenderer::startWorker() {
    stopWorker_ = false;
    queuedFrames_ = 0;
    jobs_.clear();
    worker_ = std::thread([this] { workerLoop(); });
}

void VideoRenderer::stopWorker() {
    if (!worker_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(qm_);
        stopWorker_ = true;  // after what is queued
    }
    qcv_.notify_all();
    worker_.join();
    spare_.clear();
}

void VideoRenderer::workerLoop() {
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> lk(qm_);
            qcv_.wait(lk, [&] { return !jobs_.empty() || stopWorker_; });
            if (jobs_.empty()) return;
            j = std::move(jobs_.front());
            jobs_.pop_front();
        }
        if (!failed_) {
            if (android_) {
                if (!j.px.empty() && !android_->video(j.px.data())) failed_ = true;
                if (!j.pcm.empty()) android_->audio(j.pcm.data(), j.pcm.size() / 2);
            } else if (!j.px.empty() && !proc_.write(j.px.data(), j.px.size())) {
                failed_ = true;  // ffmpeg stopped
            }
        }
        std::lock_guard<std::mutex> lk(qm_);
        if (!j.px.empty()) {
            --queuedFrames_;
            if (spare_.size() < size_t(kQueued) + 1) spare_.push_back(std::move(j.px));
        }
        qcv_.notify_all();
    }
}

void VideoRenderer::post(Job&& j) {
    std::unique_lock<std::mutex> lk(qm_);
    const bool frame = !j.px.empty();
    // The game waits only when the encoder is kQueued frames behind.
    qcv_.wait(lk, [&] { return !frame || queuedFrames_ < kQueued || failed_; });
    if (frame) ++queuedFrames_;
    jobs_.push_back(std::move(j));
    qcv_.notify_all();
}

std::vector<uint8_t> VideoRenderer::takeBuffer(size_t bytes) {
    std::vector<uint8_t> b;
    {
        std::lock_guard<std::mutex> lk(qm_);
        if (!spare_.empty()) {
            b = std::move(spare_.back());
            spare_.pop_back();
        }
    }
    b.resize(bytes);
    return b;
}

// ------------------------------------------ reading frames back, late --

bool VideoRenderer::initPbo() {
#if defined(__ANDROID__)
    return false;  // OpenGL ES 2 has no pixel buffers
#else
    const GlPbo& gl = glPbo();
    if (!gl.ok) return false;
    const std::ptrdiff_t bytes = std::ptrdiff_t(opt_.width) * opt_.height * 4;
    gl.GenBuffers(3, pbo_);
    for (unsigned b : pbo_) {
        gl.BindBuffer(kPixelPackBuffer, b);
        gl.BufferData(kPixelPackBuffer, bytes, nullptr, kStreamRead);
    }
    gl.BindBuffer(kPixelPackBuffer, 0);
    pboHead_ = pboPending_ = 0;
    return pbo_[0] && pbo_[1] && pbo_[2];
#endif
}

void VideoRenderer::readPbo() {
#if !defined(__ANDROID__)
    const GlPbo& gl = glPbo();
    gl.BindFramebuffer(kReadFramebuffer, rt_.id);
    gl.BindBuffer(kPixelPackBuffer, pbo_[pboHead_ % 3]);
    gl.ReadPixels(0, 0, opt_.width, opt_.height, kRgba, kUnsignedByte, nullptr);  // into the buffer, no waiting
    gl.BindBuffer(kPixelPackBuffer, 0);
    gl.BindFramebuffer(kReadFramebuffer, 0);
    ++pboHead_;
    ++pboPending_;
#endif
}

void VideoRenderer::collectPbo() {
#if !defined(__ANDROID__)
    if (pboPending_ <= 0) return;
    const GlPbo& gl = glPbo();
    const size_t bytes = size_t(opt_.width) * size_t(opt_.height) * 4;
    gl.BindBuffer(kPixelPackBuffer, pbo_[(pboHead_ - pboPending_) % 3]);
    --pboPending_;
    Job j;
    if (const void* p = gl.MapBufferRange(kPixelPackBuffer, 0, std::ptrdiff_t(bytes), kMapRead)) {
        j.px = takeBuffer(bytes);
        std::memcpy(j.px.data(), p, bytes);
        gl.UnmapBuffer(kPixelPackBuffer);
    } else {
        failed_ = true;
    }
    gl.BindBuffer(kPixelPackBuffer, 0);
    if (!j.px.empty()) post(std::move(j));
#endif
}

void VideoRenderer::freePbo() {
#if !defined(__ANDROID__)
    if (pbo_[0]) glPbo().DeleteBuffers(3, pbo_);
#endif
    pbo_[0] = pbo_[1] = pbo_[2] = 0;
    pboHead_ = pboPending_ = 0;
    usePbo_ = false;
}

unsigned renderAudioFrames(long long frame, int fps) {
    const uint64_t f = uint64_t(std::max(1, fps));
    const uint64_t a = uint64_t(frame) * 44100 / f, b = uint64_t(frame + 1) * 44100 / f;
    return unsigned(b - a);
}

unsigned VideoRenderer::audioFramesFor(long long frame) const { return renderAudioFrames(frame, opt_.fps); }

bool VideoRenderer::captureFrame() {
    if (!active_ || !rt_.id || failed_) return false;
    if (usePbo_) {
        // This frame goes into a pixel buffer; the one from two frames ago,
        // long since copied by the graphics card, goes to the encoder.
        if (pboPending_ >= 3) collectPbo();
        readPbo();
        if (pboPending_ >= 3) collectPbo();
    } else {
        void* px = rlReadTexturePixels(rt_.texture.id, rt_.texture.width, rt_.texture.height, rt_.texture.format);
        if (!px) return false;
        const size_t bytes = size_t(rt_.texture.width) * size_t(rt_.texture.height) * 4;
        Job j;
        j.px = takeBuffer(bytes);
        std::memcpy(j.px.data(), px, bytes);
        MemFree(px);
        post(std::move(j));
    }
    ++frames_;
    return !failed_;
}

void VideoRenderer::addAudio(const float* stereo, unsigned frames) {
    if (!active_ || frames == 0 || !(wav_ || (android_ && opt_.audio))) return;
    pcm_.resize(size_t(frames) * 2);
    for (size_t i = 0; i < pcm_.size(); ++i) pcm_[i] = int16_t(std::lround(std::clamp(stereo[i], -1.0f, 1.0f) * 32767.0f));
    audioFrames_ += frames;
    if (android_) {  // encoded with the frames, on the encoder's thread
        Job j;
        j.pcm = pcm_;
        post(std::move(j));
        return;
    }
    std::fwrite(pcm_.data(), 2, pcm_.size(), wav_);
}

void VideoRenderer::closeWav() {
    if (!wav_) return;
    const uint64_t bytes = std::min<uint64_t>(audioFrames_ * 4, 0xFFFFFF00ull);
    std::fseek(wav_, 4, SEEK_SET);
    put32(wav_, uint32_t(36 + bytes));
    std::fseek(wav_, 40, SEEK_SET);
    put32(wav_, uint32_t(bytes));
    std::fclose(wav_);
    wav_ = nullptr;
}

std::string VideoRenderer::finish(std::string& err) {
    err.clear();
    if (!active_) return "";
    active_ = false;
    while (usePbo_ && pboPending_ > 0) collectPbo();  // the last frames, still in pixel buffers
    stopWorker();  // everything queued reaches the encoder
    freePbo();
    if (android_) {
        const bool ok = !failed_ && android_->finish(err);
        android_.reset();
        if (rt_.id) {
            UnloadRenderTexture(rt_);
            rt_ = {};
        }
        if (!ok || frames_ == 0) {
            if (err.empty()) err = frames_ == 0 ? "nothing was recorded" : "the video encoder stopped";
            removeFile(final_);
            return "";
        }
#if defined(__ANDROID__)
        // Into the phone's Movies/GodsHand folder, where the gallery finds it.
        const std::string shown = platform::publishVideo(final_, galleryName_);
        if (!shown.empty()) {
            removeFile(final_);
            setMessage("Video saved: " + shown);
            return shown;
        }
        setMessage("Video saved: " + platform::displayPath(final_));
#endif
        return final_;
    }
    const int rc = proc_.finish();  // ffmpeg encodes what is left and ends
    if (rt_.id) {
        UnloadRenderTexture(rt_);
        rt_ = {};
    }
    closeWav();
    if (rc != 0 || frames_ == 0) {
        err = frames_ == 0 ? "nothing was recorded" : "ffmpeg could not make the video (its messages: " + log_ + ")";
        removeFile(videoTmp_);
        removeFile(wavTmp_);
        return "";
    }
    removeFile(log_);
    if (!opt_.audio || audioFrames_ == 0) {
        removeFile(wavTmp_);
        if (!moveFile(videoTmp_, final_)) {
            err = "could not write " + final_;
            return "";
        }
        setMessage("Video saved: " + final_);
        return final_;
    }
    // The audio goes in by copying the video as it is: a few seconds, in the background.
    ++g_busy;
    std::thread([ffmpeg = ffmpeg_, opt = opt_, video = videoTmp_, wav = wavTmp_, out = final_] {
        if (runProcess(ffmpeg, renderMuxArgs(opt, video, wav, out)) == 0) {
            removeFile(video);
            removeFile(wav);
            setMessage("Video saved: " + out);
        } else {
            removeFile(out);
            moveFile(video, out);
            setMessage("Video saved without its audio (ffmpeg could not add it; the audio is in " + wav + "): " + out);
        }
        --g_busy;
    }).detach();
    setMessage("Adding the audio to " + final_ + "...");
    return final_;
}

bool VideoRenderer::busy() { return g_busy.load() > 0; }

void VideoRenderer::waitIdle(double maxSeconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(maxSeconds);
    while (busy() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

std::string VideoRenderer::takeMessage() {
    std::lock_guard<std::mutex> lk(g_msgMtx);
    std::string m;
    m.swap(g_message);
    return m;
}

}  // namespace gh
