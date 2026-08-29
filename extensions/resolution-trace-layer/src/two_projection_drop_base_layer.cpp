#include <android/log.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <time.h>
#include <unordered_set>
#include <unistd.h>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))
#ifndef GXR_LAYER_NAME
#define GXR_LAYER_NAME "XR_APILAYER_local_GalaxyXR_two_projection_drop_base_v1"
#endif

namespace {

constexpr char kLayerName[] = GXR_LAYER_NAME;
constexpr char kLogTag[] = "GXRResolutionTrace";
constexpr char kModeName[] = "two_projection_drop_base_v1";
constexpr char kBuildId[] = "two-projection-drop-base-v1-20260829";
constexpr uint32_t kSourceExtent = 1536;
constexpr int64_t kSourceFormat = 35907; // GL_SRGB8_ALPHA8
constexpr XrCompositionLayerFlags kFoveaFlags =
    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
    XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;

struct Dispatch {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrGetInstanceProcAddr getInstanceProcAddr{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrCreateSession createSession{};
    PFN_xrDestroySession destroySession{};
    PFN_xrCreateSwapchain createSwapchain{};
    PFN_xrDestroySwapchain destroySwapchain{};
    PFN_xrEndFrame endFrame{};
};

struct SwapchainState {
    XrSession session{XR_NULL_HANDLE};
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
};

struct SessionState {
    bool transformed{};
    bool disabled{};
};

struct Fingerprint {
    bool valid{};
    std::string reason;
    XrSession owner{XR_NULL_HANDLE};
    std::array<const XrCompositionLayerProjection*, 3> projections{};
};

Dispatch g;
std::map<XrSwapchain, SwapchainState> swapchains;
std::map<XrSession, SessionState> sessions;
std::atomic<uint64_t> frameCounter{0};

uint64_t elapsedMs() {
    timespec t{};
    clock_gettime(CLOCK_BOOTTIME, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000ULL +
        static_cast<uint64_t>(t.tv_nsec) / 1000000ULL;
}

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '\\') out << "\\\\";
        else if (c == '"') out << "\\\"";
        else if (c == '\n') out << "\\n";
        else if (c < 0x20) out << '?';
        else out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}

void emit(const char* event, const std::string& fields = {}) {
    std::ostringstream out;
    out << "{\"schema\":2,\"runId\":\"pid-" << getpid()
        << "\",\"source\":\"openxr\",\"mode\":\"" << kModeName
        << "\",\"buildId\":\"" << kBuildId << "\",\"elapsedMs\":" << elapsedMs()
        << ",\"event\":\"" << event << '"';
    if (!fields.empty()) out << ',' << fields;
    out << '}';
    __android_log_write(ANDROID_LOG_INFO, kLogTag, out.str().c_str());
}

template <typename T>
void load(const char* name, T& fn) {
    PFN_xrVoidFunction address{};
    if (g.getInstanceProcAddr &&
        XR_SUCCEEDED(g.getInstanceProcAddr(g.instance, name, &address))) {
        fn = reinterpret_cast<T>(address);
    }
}

bool sample(uint64_t frame) { return frame <= 3 || frame % 90 == 0; }
bool close(float a, float b) { return std::fabs(a - b) <= 0.0001f; }

bool samePose(const XrPosef& a, const XrPosef& b) {
    return close(a.orientation.x, b.orientation.x) &&
        close(a.orientation.y, b.orientation.y) &&
        close(a.orientation.z, b.orientation.z) &&
        close(a.orientation.w, b.orientation.w) &&
        close(a.position.x, b.position.x) &&
        close(a.position.y, b.position.y) &&
        close(a.position.z, b.position.z);
}

bool sameFov(const XrFovf& a, const XrFovf& b) {
    return close(a.angleLeft, b.angleLeft) &&
        close(a.angleRight, b.angleRight) &&
        close(a.angleUp, b.angleUp) &&
        close(a.angleDown, b.angleDown);
}

float spanX(const XrFovf& fov) {
    return std::tan(fov.angleRight) - std::tan(fov.angleLeft);
}

float spanY(const XrFovf& fov) {
    return std::tan(fov.angleUp) - std::tan(fov.angleDown);
}

bool finitePositiveFov(const XrFovf& fov) {
    const float width = spanX(fov);
    const float height = spanY(fov);
    return std::isfinite(width) && std::isfinite(height) && width > 0.01f && height > 0.01f;
}

bool strictlyInset(const XrFovf& full, const XrFovf& inset) {
    constexpr float epsilon = 0.0001f;
    return finitePositiveFov(full) && finitePositiveFov(inset) &&
        inset.angleLeft >= full.angleLeft - epsilon &&
        inset.angleRight <= full.angleRight + epsilon &&
        inset.angleDown >= full.angleDown - epsilon &&
        inset.angleUp <= full.angleUp + epsilon &&
        spanX(inset) < spanX(full) * 0.95f &&
        spanY(inset) < spanY(full) * 0.95f;
}

bool safeProjectionNext(const void* next) {
    if (!next) return true;
    const auto* header = static_cast<const XrBaseInStructure*>(next);
    if (header->type != XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB || header->next) return false;
    return reinterpret_cast<const XrCompositionLayerSettingsFB*>(next)->layerFlags == 0;
}

Fingerprint inspect(const XrFrameEndInfo* info) {
    Fingerprint result;
    if (!info || info->layerCount != 3 || !info->layers) {
        result.reason = "layer_count";
        return result;
    }

    for (size_t layer = 0; layer < result.projections.size(); ++layer) {
        const auto* header = info->layers[layer];
        if (!header || header->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            result.reason = "non_projection";
            return result;
        }
        const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(header);
        if (projection->viewCount != 2 || !projection->views) {
            result.reason = "view_count";
            return result;
        }
        if (!safeProjectionNext(projection->next)) {
            result.reason = "projection_next";
            return result;
        }
        result.projections[layer] = projection;
    }

    const auto& base = *result.projections[0];
    const auto& under = *result.projections[1];
    const auto& fovea = *result.projections[2];
    if (base.layerFlags != 0 || under.layerFlags != 0 || fovea.layerFlags != kFoveaFlags) {
        result.reason = "flags";
        return result;
    }
    if (base.space == XR_NULL_HANDLE || base.space != under.space || under.space != fovea.space) {
        result.reason = "space";
        return result;
    }

    XrSession owner = XR_NULL_HANDLE;
    std::unordered_set<XrSwapchain> unique;
    for (size_t layer = 0; layer < result.projections.size(); ++layer) {
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const auto& view = result.projections[layer]->views[eye];
            const auto& rect = view.subImage.imageRect;
            if (view.next || rect.offset.x != 0 || rect.offset.y != 0 ||
                rect.extent.width != static_cast<int32_t>(kSourceExtent) ||
                rect.extent.height != static_cast<int32_t>(kSourceExtent) ||
                view.subImage.imageArrayIndex != 0) {
                result.reason = "subimage";
                return result;
            }
            if (!samePose(view.pose, under.views[eye].pose)) {
                result.reason = "pose";
                return result;
            }
            const auto swapchain = swapchains.find(view.subImage.swapchain);
            if (swapchain == swapchains.end()) {
                result.reason = "unknown_swapchain";
                return result;
            }
            const auto& createInfo = swapchain->second.info;
            if (createInfo.width != kSourceExtent || createInfo.height != kSourceExtent ||
                createInfo.arraySize != 1 || createInfo.sampleCount != 2 ||
                createInfo.format != kSourceFormat ||
                createInfo.usageFlags != (XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_SAMPLED_BIT) ||
                createInfo.faceCount != 1 || createInfo.mipCount != 1) {
                result.reason = "swapchain_info";
                return result;
            }
            if (owner == XR_NULL_HANDLE) owner = swapchain->second.session;
            if (owner != swapchain->second.session) {
                result.reason = "session";
                return result;
            }
            unique.insert(view.subImage.swapchain);
        }
    }

    for (uint32_t eye = 0; eye < 2; ++eye) {
        if (!sameFov(base.views[eye].fov, under.views[eye].fov) ||
            !strictlyInset(under.views[eye].fov, fovea.views[eye].fov)) {
            result.reason = "topology";
            return result;
        }
    }
    if (unique.size() != 6) {
        result.reason = "swapchain_alias";
        return result;
    }

    result.valid = true;
    result.owner = owner;
    return result;
}

XrResult XRAPI_PTR layerCreateSession(
    XrInstance instance,
    const XrSessionCreateInfo* createInfo,
    XrSession* session) {
    const XrResult result = g.createSession(instance, createInfo, session);
    if (XR_SUCCEEDED(result) && session) sessions[*session] = {};
    return result;
}

XrResult XRAPI_PTR layerDestroySession(XrSession session) {
    sessions.erase(session);
    for (auto it = swapchains.begin(); it != swapchains.end();) {
        if (it->second.session == session) it = swapchains.erase(it);
        else ++it;
    }
    return g.destroySession(session);
}

XrResult XRAPI_PTR layerCreateSwapchain(
    XrSession session,
    const XrSwapchainCreateInfo* createInfo,
    XrSwapchain* swapchain) {
    const XrResult result = g.createSwapchain(session, createInfo, swapchain);
    if (XR_SUCCEEDED(result) && createInfo && swapchain) {
        SwapchainState state;
        state.session = session;
        state.info = *createInfo;
        state.info.next = nullptr;
        swapchains[*swapchain] = state;
    }
    return result;
}

XrResult XRAPI_PTR layerDestroySwapchain(XrSwapchain swapchain) {
    swapchains.erase(swapchain);
    return g.destroySwapchain(swapchain);
}

XrResult XRAPI_PTR layerEndFrame(XrSession session, const XrFrameEndInfo* info) {
    const uint64_t frame = ++frameCounter;
    auto sessionState = sessions.find(session);
    const Fingerprint fingerprint = inspect(info);

    if (sessionState == sessions.end() || sessionState->second.disabled ||
        !fingerprint.valid || fingerprint.owner != session) {
        const std::string reason = sessionState == sessions.end() ? "unknown_session" :
            sessionState->second.disabled ? "session_disabled" :
            !fingerprint.valid ? fingerprint.reason : "owner_mismatch";
        if (sessionState != sessions.end() && sessionState->second.transformed &&
            !sessionState->second.disabled) {
            sessionState->second.disabled = true;
            emit("two_projection_drop_base_disabled", "\"frame\":" + std::to_string(frame) +
                ",\"reason\":" + quote(reason));
        }
        if (sample(frame)) {
            emit("two_projection_drop_base_passthrough", "\"frame\":" +
                std::to_string(frame) + ",\"reason\":" + quote(reason));
        }
        const XrResult result = g.endFrame(session, info);
        if (sample(frame)) {
            emit("end_frame_result", "\"frame\":" + std::to_string(frame) +
                ",\"result\":" + std::to_string(result));
        }
        return result;
    }

    sessionState->second.transformed = true;
    const std::array<const XrCompositionLayerBaseHeader*, 2> forwarded = {
        info->layers[1],
        info->layers[2],
    };
    XrFrameEndInfo output = *info;
    output.layerCount = static_cast<uint32_t>(forwarded.size());
    output.layers = forwarded.data();

    emit("two_projection_drop_base_transform", "\"frame\":" + std::to_string(frame) +
        ",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"forwardedLayerCount\":2," 
        "\"outputProjectionCount\":2,\"sourceViewCount\":6,\"outputViewCount\":4," 
        "\"droppedBaseProjectionCount\":1,\"droppedLayerIndex\":0," 
        "\"forwardedLayerIndices\":[1,2],\"unsafeLayerCount\":0," 
        "\"preservedSourceStructures\":true,\"changed\":true");
    const XrResult result = g.endFrame(session, &output);
    emit("end_frame_result", "\"frame\":" + std::to_string(frame) +
        ",\"result\":" + std::to_string(result));
    if (XR_FAILED(result)) {
        sessionState->second.disabled = true;
        emit("two_projection_drop_base_disabled", "\"frame\":" + std::to_string(frame) +
            ",\"reason\":\"end_frame_failed\",\"result\":" + std::to_string(result));
    }
    return result;
}

XrResult XRAPI_PTR layerDestroyInstance(XrInstance instance) {
    sessions.clear();
    swapchains.clear();
    emit("destroy_instance");
    const XrResult result = g.destroyInstance(instance);
    g = {};
    return result;
}

XrResult XRAPI_PTR layerGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;
#define ROUTE(functionName, implementation) \
    if (std::strcmp(name, functionName) == 0) \
        *function = reinterpret_cast<PFN_xrVoidFunction>(implementation)
    ROUTE("xrGetInstanceProcAddr", layerGetInstanceProcAddr);
    else ROUTE("xrDestroyInstance", layerDestroyInstance);
    else ROUTE("xrCreateSession", layerCreateSession);
    else ROUTE("xrDestroySession", layerDestroySession);
    else ROUTE("xrCreateSwapchain", layerCreateSwapchain);
    else ROUTE("xrDestroySwapchain", layerDestroySwapchain);
    else ROUTE("xrEndFrame", layerEndFrame);
    else return g.getInstanceProcAddr ?
        g.getInstanceProcAddr(instance, name, function) : XR_ERROR_FUNCTION_UNSUPPORTED;
#undef ROUTE
    return XR_SUCCESS;
}

XrResult XRAPI_PTR layerCreateApiLayerInstance(
    const XrInstanceCreateInfo* createInfo,
    const XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance) {
    if (!apiLayerInfo || !apiLayerInfo->nextInfo) return XR_ERROR_INITIALIZATION_FAILED;
    XrApiLayerCreateInfo next = *apiLayerInfo;
    next.nextInfo = apiLayerInfo->nextInfo->next;
    const XrResult result = apiLayerInfo->nextInfo->nextCreateApiLayerInstance(
        createInfo, &next, instance);
    if (XR_FAILED(result)) return result;

    g.instance = *instance;
    g.getInstanceProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;
    load("xrDestroyInstance", g.destroyInstance);
    load("xrCreateSession", g.createSession);
    load("xrDestroySession", g.destroySession);
    load("xrCreateSwapchain", g.createSwapchain);
    load("xrDestroySwapchain", g.destroySwapchain);
    load("xrEndFrame", g.endFrame);
    emit("layer_initialized", "\"layerName\":" + quote(kLayerName) +
        ",\"dropBaseBuildId\":" + quote(kBuildId));
    return XR_SUCCESS;
}

} // namespace

GXR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* request) {
    if (!loaderInfo || !layerName || !request || std::strcmp(layerName, kLayerName) != 0) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = layerGetInstanceProcAddr;
    request->createApiLayerInstance = layerCreateApiLayerInstance;
    return XR_SUCCESS;
}
