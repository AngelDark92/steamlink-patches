#include "../src/layer_frame.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

void check(bool value) { if (!value) std::abort(); }

struct Source {
    XrSession session{};
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    bool supported{true};
};

template <size_t N>
void run() {
    XrSession session = reinterpret_cast<XrSession>(1);
    std::map<XrSwapchain, Source> sources;
    std::array<XrCompositionLayerProjection, N> inLayers{}, outLayers{};
    std::array<std::array<XrCompositionLayerProjectionView, 2>, N> inViews{}, outViews{};
    std::array<const XrCompositionLayerBaseHeader*, N> input{}, output{};
    std::array<std::array<XrRect2Di, 2>, N> rects{};
    for (size_t i = 0; i < N; ++i) {
        auto& layer = inLayers[i];
        layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        layer.space = reinterpret_cast<XrSpace>(42);
        layer.viewCount = 2;
        layer.views = inViews[i].data();
        input[i] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
        for (size_t e = 0; e < 2; ++e) {
            auto xr = reinterpret_cast<XrSwapchain>(10 + i * 2 + e);
            auto& s = sources[xr];
            s.session = session;
            s.info.width = 3072;
            s.info.height = 6144;
            s.info.arraySize = 1;
            auto& v = inViews[i][e];
            v.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            v.pose.orientation.w = 1;
            v.pose.position.x = float(e);
            v.fov = {-.7f, .8f, .6f, -.5f};
            v.subImage = {xr, {{int32_t(e * 1536), 0}, {1536, 6144}}, 0};
        }
    }
    XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};
    info.displayTime = 1234;
    info.layerCount = N;
    info.layers = input.data();
    auto lookup = [&](XrSwapchain xr) -> Source* {
        auto it = sources.find(xr);
        return it == sources.end() ? nullptr : &it->second;
    };
    auto prepare = [&] {
        return gxr::layer::prepareFrame<N>(session, info, lookup, outLayers, outViews, output, rects);
    };
    check(prepare());
    for (size_t i = 0; i < N; ++i) {
        check(output[i] == reinterpret_cast<const XrCompositionLayerBaseHeader*>(&outLayers[i]));
        check(outLayers[i].views == outViews[i].data());
        for (size_t e = 0; e < 2; ++e) {
            const auto& a = inViews[i][e];
            const auto& b = outViews[i][e];
            check(!std::memcmp(&a.pose, &b.pose, sizeof(a.pose)));
            check(!std::memcmp(&a.fov, &b.fov, sizeof(a.fov)));
            check(!std::memcmp(&a.subImage.imageRect, &b.subImage.imageRect, sizeof(XrRect2Di)));
            check(rects[i][e].offset.x == a.subImage.imageRect.offset.x);
            check(rects[i][e].extent.width == a.subImage.imageRect.extent.width);
        }
    }

    // A 2-projection frame is valid in the 3-slot helper (5001712), but a
    // 3-projection frame cannot fit the 2-slot helper.
    if (N == 3) {
        info.layerCount = 2;
        check(prepare());
        info.layerCount = 1;
        check(!prepare());
    } else {
        info.layerCount = 3;
        check(!prepare());
    }
    info.layerCount = N;

    // Invalid frames must return false without mutating the caller's arrays.
    inLayers[0].type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    check(!prepare());
    inLayers[0].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    inViews[0][0].type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    check(!prepare());
    inViews[0][0].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    inViews[0][0].subImage.imageRect.extent.width = 9999;
    check(!prepare());
    inViews[0][0].subImage.imageRect.extent.width = 1536;
    inViews[0][0].subImage.imageArrayIndex = 1;
    check(!prepare());
    inViews[0][0].subImage.imageArrayIndex = 0;
    sources.begin()->second.session = nullptr;
    check(!prepare());
    sources.begin()->second.session = session;
    sources.begin()->second.supported = false;
    check(!prepare());
    sources.begin()->second.supported = true;
    inViews[N - 1][1].next = &info;
    check(!prepare());
    inViews[N - 1][1].next = nullptr;
    check(prepare());

    std::cout << "PASS " << N << "-projection frame validation, rect capture and field preservation\n";
}

int main() {
    run<2>();
    run<3>();
    return 0;
}
