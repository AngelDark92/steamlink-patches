#pragma once
#include <openxr/openxr.h>
#include <array>
#include "layer_policy.h"

namespace gxr::layer {
// Pure frame preparation shared with host tests. The caller submits only on
// true. Lookup returns a tracked source or nullptr; no runtime/GPU calls happen
// here. Supports both the 2-projection 5001712 topology and the 3-projection
// 5002395 topology.
template <size_t N, typename Lookup>
bool prepareFrame(XrSession session, const XrFrameEndInfo& info, Lookup lookup,
    std::array<XrCompositionLayerProjection, N>& layers,
    std::array<std::array<XrCompositionLayerProjectionView, 2>, N>& views,
    std::array<const XrCompositionLayerBaseHeader*, N>& pointers,
    std::array<std::array<XrRect2Di, 2>, N>& rects) {
    if (info.layerCount < 2 || info.layerCount > N || !info.layers) return false;
    for (size_t i = 0; i < N; ++i) {
        if (i >= info.layerCount) {
            layers[i] = {};
            pointers[i] = nullptr;
            continue;
        }
        const auto* base = info.layers[i];
        if (!base || base->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) return false;
        const auto* layer = reinterpret_cast<const XrCompositionLayerProjection*>(base);
        if (layer->viewCount != 2 || !layer->views) return false;
        layers[i] = *layer;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const auto& view = layer->views[eye];
            const auto& sub = view.subImage;
            auto* source = lookup(sub.swapchain);
            if (view.type != XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW || view.next ||
                !source || !source->supported || source->session != session) return false;
            if (sub.imageArrayIndex >= source->info.arraySize || !rectValid(
                sub.imageRect.offset.x, sub.imageRect.offset.y,
                sub.imageRect.extent.width, sub.imageRect.extent.height,
                source->info.width, source->info.height)) return false;
            rects[i][eye] = sub.imageRect;
            views[i][eye] = view;
        }
        layers[i].views = views[i].data();
        pointers[i] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layers[i]);
    }
    return true;
}
} // namespace gxr::layer
