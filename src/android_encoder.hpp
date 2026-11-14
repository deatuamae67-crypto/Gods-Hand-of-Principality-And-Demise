// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Rendering mode on Android, where there is no ffmpeg: the phone's own H.264
// and AAC encoders (MediaCodec) write the video, with its sound, into an MP4
// (MediaMuxer). Only built for Android.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace gh {

// Frames are RGBA, bottom row first (as read from a render texture). The
// colours become BT.709 YUV 4:2:0, as players expect of HD video.
void rgbaToYuv420(const uint8_t* rgbaBottomUp, int w, int h, uint8_t* y, int yStride, uint8_t* u, uint8_t* v, int uvStride,
                  int uvStep);

class AndroidEncoder {
public:
    AndroidEncoder();
    ~AndroidEncoder();
    // Opens `path` (an .mp4) and the encoders; false with the reason in err.
    bool start(const std::string& path, int width, int height, int fps, int bitrateMbps, bool audio, std::string& err);
    bool video(const uint8_t* rgbaBottomUp);     // the next frame
    void audio(const int16_t* stereo, size_t frames);  // 44.1 kHz
    // Ends both streams and closes the file; false: the video is unusable.
    bool finish(std::string& err);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace gh
