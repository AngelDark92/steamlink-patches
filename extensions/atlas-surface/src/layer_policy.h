#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace gxr::layer {
// OpenXR permits multiple acquired images. A wait timeout must not grant read access.
struct ImageQueue {
    struct Image { uint32_t index; bool waited; };
    std::deque<Image> images;
    void acquired(uint32_t index) { images.push_back({index, false}); }
    void waited() {
        for (auto& image : images) if (!image.waited) { image.waited = true; return; }
    }
    const Image* readable() const {
        return !images.empty() && images.front().waited ? &images.front() : nullptr;
    }
    void released() { if (!images.empty()) images.pop_front(); }
    void clear() { images.clear(); }
};

struct Extent {
    uint32_t width{};
    uint32_t height{};
};

inline bool sameExtent(Extent a, Extent b) {
    return a.width == b.width && a.height == b.height;
}

inline bool rectValid(int32_t x, int32_t y, int32_t w, int32_t h,
                      uint32_t width, uint32_t height) {
    return x >= 0 && y >= 0 && w > 0 && h > 0 &&
        uint64_t(x) + uint64_t(w) <= width && uint64_t(y) + uint64_t(h) <= height;
}

inline bool dimensionsValid(uint32_t w, uint32_t h, uint32_t arrays,
                            uint32_t samples, uint32_t faces, uint32_t mips) {
    return w && h && w <= 8192 && h <= 8192 && arrays >= 1 && arrays <= 2 &&
        samples == 1 && faces == 1 && mips == 1;
}

// Preflight mode: grow a projection swapchain by 1.5x, capped at the GL/XR
// maximum texture extent. Never shrink. The app keeps its own original
// viewport and imageRect; only the backing image grows.
inline Extent enlargedExtent(uint32_t width, uint32_t height,
                             uint32_t maximum = 8192) {
    if (!width || !height) return {};
    const uint64_t grownWidth = std::min<uint64_t>(
        static_cast<uint64_t>(width) * 3 / 2, maximum);
    const uint64_t grownHeight = std::min<uint64_t>(
        static_cast<uint64_t>(height) * 3 / 2, maximum);
    return {
        static_cast<uint32_t>(std::max<uint64_t>(width, grownWidth)),
        static_cast<uint32_t>(std::max<uint64_t>(height, grownHeight)),
    };
}

// Atlas mode: scale a source view rect up to the runtime's max per-eye extent,
// preserving its aspect ratio. Never downscale: when the source already exceeds
// the runtime max on one axis, the source rect is kept.
inline Extent targetExtent(uint32_t sourceWidth, uint32_t sourceHeight,
                           uint32_t maxWidth, uint32_t maxHeight) {
    if (!sourceWidth || !sourceHeight) return {};
    if (!maxWidth || !maxHeight) return {sourceWidth, sourceHeight};
    const double scaleX = static_cast<double>(maxWidth) / sourceWidth;
    const double scaleY = static_cast<double>(maxHeight) / sourceHeight;
    double scale = std::min(scaleX, scaleY);
    if (!(scale > 1.0)) scale = 1.0;
    const uint64_t width = std::min<uint64_t>(
        static_cast<uint64_t>(std::llround(sourceWidth * scale)),
        std::max<uint64_t>(sourceWidth, maxWidth));
    const uint64_t height = std::min<uint64_t>(
        static_cast<uint64_t>(std::llround(sourceHeight * scale)),
        std::max<uint64_t>(sourceHeight, maxHeight));
    return {
        static_cast<uint32_t>(std::max<uint64_t>(sourceWidth, width)),
        static_cast<uint32_t>(std::max<uint64_t>(sourceHeight, height)),
    };
}

// Normalized source rectangle for the blit shader. OpenXR imageRect origin is
// top-left; GL texture origin is bottom-left, so Y is mirrored here.
struct UvRect {
    float x{}, y{}, width{}, height{};
};

inline UvRect sourceRectUv(int32_t x, int32_t y, int32_t width, int32_t height,
                           uint32_t sourceWidth, uint32_t sourceHeight) {
    if (!sourceWidth || !sourceHeight || width <= 0 || height <= 0) return {};
    const float u = static_cast<float>(x) / static_cast<float>(sourceWidth);
    const float vTop = static_cast<float>(y) / static_cast<float>(sourceHeight);
    const float uWidth = static_cast<float>(width) / static_cast<float>(sourceWidth);
    const float vHeight = static_cast<float>(height) / static_cast<float>(sourceHeight);
    return {u, 1.0f - (vTop + vHeight), uWidth, vHeight};
}
} // namespace gxr::layer
