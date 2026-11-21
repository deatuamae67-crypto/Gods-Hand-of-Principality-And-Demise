// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "song_icons.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "app.hpp"
#include "scene.hpp"

namespace gh {

const Texture2D* SongIcons::get(const SongEntry& e, int* frames) {
    if (e.builtin || e.autoChart) return nullptr;
    Slot& s = slots_[e.id];
    if (!s.done && !s.job.valid()) {
        if (busy_ >= 3) return nullptr;  // a few at a time: the list scrolls on
        ++busy_;
        const SongEntry copy = e;
        const std::vector<std::string> shared = app().library.sharedAssetRootsFor(e);
        s.job = std::async(std::launch::async, [copy, shared] {
            Image img{};
            const std::string f = findSongIcon(copy, shared);
            std::string bytes;
            if (!f.empty() && scene::readFile(f, bytes))
                img = LoadImageFromMemory(".png", reinterpret_cast<const unsigned char*>(bytes.data()), int(bytes.size()));
            return img;
        });
    }
    if (!s.done && s.job.valid() && s.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Image img = s.job.get();
        --busy_;
        s.done = true;
        if (img.data) {
            s.tex = LoadTextureFromImage(img);
            SetTextureFilter(s.tex, TEXTURE_FILTER_BILINEAR);
            s.frames = std::max(1, int(std::lround(double(img.width) / std::max(1, img.height))));
            UnloadImage(img);
        }
    }
    if (!s.done || !s.tex.id) return nullptr;
    if (frames) *frames = s.frames;
    return &s.tex;
}

void SongIcons::clear() {
    for (auto& [id, s] : slots_) {
        if (s.job.valid()) {
            Image img = s.job.get();  // a lookup still running: wait for it, then drop it
            if (img.data) UnloadImage(img);
        }
        if (s.tex.id && IsWindowReady()) UnloadTexture(s.tex);
    }
    slots_.clear();
    busy_ = 0;
}

void drawSongIcon(const Texture2D& tex, int frames, Vector2 c, float size, float alpha) {
    const float fw = float(tex.width) / float(std::max(1, frames));
    DrawTexturePro(tex, {0, 0, fw, float(tex.height)}, {c.x - size * 0.5f, c.y - size * 0.5f, size, size}, {0, 0}, 0,
                   Color{255, 255, 255, (unsigned char)std::clamp(alpha * 255.0f, 0.0f, 255.0f)});
}

}  // namespace gh
