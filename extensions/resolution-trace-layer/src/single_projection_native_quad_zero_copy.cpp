#include <android/log.h>
#include <dlfcn.h>
#include <openxr/openxr.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>
#include <unistd.h>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

constexpr char kHelperName[] = "libgxr_nqv";
constexpr char kModeName[] = "single_projection_native_quad_zero_copy_v1";
constexpr char kBuildId[] = "single-projection-native-quad-zero-copy-v1.0-20260831";
constexpr char kLogTag[] = "GXRResolutionTrace";
constexpr uint32_t kSourceExtent = 1536;
constexpr int64_t kSourceFormat = 0x8C43; // GL_SRGB8_ALPHA8, without a GLES dependency.
constexpr uint64_t kSuccessSummaryInterval = 900;
constexpr XrViewConfigurationType kQuadViewConfiguration =
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_WITH_FOVEATED_INSET;
constexpr XrCompositionLayerFlags kFoveaFlags =
    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
    XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;

struct Dispatch {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrEnumerateInstanceExtensionProperties enumerateExtensions{};
    PFN_xrCreateInstance createInstance{};
    PFN_xrGetInstanceProcAddr getInstanceProcAddr{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrGetSystem getSystem{};
    PFN_xrEnumerateViewConfigurations enumerateViewConfigurations{};
    PFN_xrGetViewConfigurationProperties getViewConfigurationProperties{};
    PFN_xrEnumerateViewConfigurationViews enumerateViewConfigurationViews{};
    PFN_xrEnumerateEnvironmentBlendModes enumerateEnvironmentBlendModes{};
    PFN_xrCreateSession createSession{};
    PFN_xrDestroySession destroySession{};
    PFN_xrBeginSession beginSession{};
    PFN_xrEndSession endSession{};
    PFN_xrLocateViews locateViews{};
    PFN_xrCreateSwapchain createSwapchain{};
    PFN_xrDestroySwapchain destroySwapchain{};
    PFN_xrAcquireSwapchainImage acquireSwapchainImage{};
    PFN_xrWaitSwapchainImage waitSwapchainImage{};
    PFN_xrReleaseSwapchainImage releaseSwapchainImage{};
    PFN_xrEndFrame endFrame{};
    PFN_xrRequestExitSession requestExitSession{};
};

struct SystemState {
    bool probed{};
    bool quadSupported{};
    bool fovMutable{};
    std::array<XrViewConfigurationView, 4> views{};
    std::string reason{"not_probed"};
};

struct LocateSnapshot {
    bool valid{};
    XrTime displayTime{};
    XrSpace space{XR_NULL_HANDLE};
    XrViewStateFlags flags{};
    std::array<XrView, 4> views{};
};

struct SwapchainState;

struct SessionState {
    XrSystemId systemId{XR_NULL_SYSTEM_ID};
    bool quadCommitted{};
    bool unsafe{};
    bool exitRequested{};
    bool forceStock{};
    uint64_t successfulFrames{};
    uint64_t framesSinceSummary{};
    LocateSnapshot locate{};
    struct SourceBinding {
        XrSwapchain handle{XR_NULL_HANDLE};
        SwapchainState* state{};
        uint64_t generation{};
    };
    std::array<SourceBinding, 6> sources{};
    std::array<XrFovf, 6> fingerprintFovs{};
    bool fingerprintReady{};
    uint64_t fastFingerprintCount{};
    uint64_t slowFingerprintCount{};
    uint64_t sourceCacheInvalidationCount{};
};

struct SwapchainState {
    XrSession session{XR_NULL_HANDLE};
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    uint32_t acquired{};
    bool waited{};
    uint64_t generation{};
};

Dispatch g;
std::once_flag dispatchOnce;
std::atomic<int> dispatchState{0};
std::mutex callsiteMutex;
std::atomic<uintptr_t> streamCallsite{0};
std::atomic<uintptr_t> exitCallsite{0};
std::atomic<bool> unexpectedCallsiteLogged{false};
std::atomic<bool> forceNextSessionStock{false};
std::atomic<uint64_t> nextSwapchainGeneration{1};
std::mutex stateMutex;
std::map<XrSystemId, SystemState> systems;
std::map<XrSession, SessionState> sessions;
std::map<XrSwapchain, SwapchainState> swapchains;
bool quadExtensionEnabled{};
bool coreQuadEnabled{};

uint64_t elapsedMs() {
    timespec value{};
    clock_gettime(CLOCK_BOOTTIME, &value);
    return static_cast<uint64_t>(value.tv_sec) * 1000ULL +
        static_cast<uint64_t>(value.tv_nsec) / 1000000ULL;
}

template <typename Handle>
uint64_t handleValue(Handle handle) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
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

template <typename Function>
bool resolve(void* loader, const char* name, Function& function) {
    function = reinterpret_cast<Function>(dlsym(loader, name));
    return function != nullptr;
}

bool initializeDispatch() {
    std::call_once(dispatchOnce, [] {
        void* loader = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
        Dispatch resolved{};
        bool ok = loader != nullptr;
#define GXR_RESOLVE(field, name) ok = resolve(loader, name, resolved.field) && ok
        if (loader) {
            GXR_RESOLVE(enumerateExtensions, "xrEnumerateInstanceExtensionProperties");
            GXR_RESOLVE(createInstance, "xrCreateInstance");
            GXR_RESOLVE(getInstanceProcAddr, "xrGetInstanceProcAddr");
            GXR_RESOLVE(destroyInstance, "xrDestroyInstance");
            GXR_RESOLVE(getSystem, "xrGetSystem");
            GXR_RESOLVE(enumerateViewConfigurations, "xrEnumerateViewConfigurations");
            GXR_RESOLVE(getViewConfigurationProperties, "xrGetViewConfigurationProperties");
            GXR_RESOLVE(enumerateViewConfigurationViews, "xrEnumerateViewConfigurationViews");
            GXR_RESOLVE(enumerateEnvironmentBlendModes, "xrEnumerateEnvironmentBlendModes");
            GXR_RESOLVE(createSession, "xrCreateSession");
            GXR_RESOLVE(destroySession, "xrDestroySession");
            GXR_RESOLVE(beginSession, "xrBeginSession");
            GXR_RESOLVE(endSession, "xrEndSession");
            GXR_RESOLVE(locateViews, "xrLocateViews");
            GXR_RESOLVE(createSwapchain, "xrCreateSwapchain");
            GXR_RESOLVE(destroySwapchain, "xrDestroySwapchain");
            GXR_RESOLVE(acquireSwapchainImage, "xrAcquireSwapchainImage");
            GXR_RESOLVE(waitSwapchainImage, "xrWaitSwapchainImage");
            GXR_RESOLVE(releaseSwapchainImage, "xrReleaseSwapchainImage");
            GXR_RESOLVE(endFrame, "xrEndFrame");
            GXR_RESOLVE(requestExitSession, "xrRequestExitSession");
        }
#undef GXR_RESOLVE
        if (ok) {
            g = resolved;
            dispatchState.store(1, std::memory_order_release);
        } else {
            dispatchState.store(-1, std::memory_order_release);
            emit("native_quad_failure", "\"reason\":\"openxr_symbol_resolution\"");
        }
    });
    return dispatchState.load(std::memory_order_acquire) == 1;
}

bool extensionAdvertised(const char* name) {
    uint32_t count{};
    if (!g.enumerateExtensions ||
        XR_FAILED(g.enumerateExtensions(nullptr, 0, &count, nullptr))) return false;
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    if (count && XR_FAILED(g.enumerateExtensions(nullptr, count, &count, extensions.data()))) return false;
    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) return true;
    }
    return false;
}

bool extensionEnabled(const XrInstanceCreateInfo& info, const char* name) {
    for (uint32_t index = 0; index < info.enabledExtensionCount; ++index) {
        if (std::strcmp(info.enabledExtensionNames[index], name) == 0) return true;
    }
    return false;
}

bool containsQuadConfiguration(const std::vector<XrViewConfigurationType>& configurations) {
    for (XrViewConfigurationType configuration : configurations) {
        if (configuration == kQuadViewConfiguration) return true;
    }
    return false;
}

SystemState probeSystem(XrInstance instance, XrSystemId systemId) {
    SystemState state;
    state.probed = true;
    if (!coreQuadEnabled && !quadExtensionEnabled) {
        state.reason = "quad_not_enabled";
        return state;
    }

    uint32_t configurationCount{};
    XrResult result = g.enumerateViewConfigurations(
        instance, systemId, 0, &configurationCount, nullptr);
    if (XR_FAILED(result) || !configurationCount) {
        state.reason = "enumerate_configurations";
        return state;
    }
    std::vector<XrViewConfigurationType> configurations(configurationCount);
    result = g.enumerateViewConfigurations(
        instance, systemId, configurationCount, &configurationCount, configurations.data());
    if (XR_FAILED(result) || !containsQuadConfiguration(configurations)) {
        state.reason = "quad_configuration_missing";
        return state;
    }

    XrViewConfigurationProperties properties{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
    result = g.getViewConfigurationProperties(instance, systemId, kQuadViewConfiguration, &properties);
    if (XR_FAILED(result)) {
        state.reason = "configuration_properties";
        return state;
    }
    state.fovMutable = properties.fovMutable == XR_TRUE;

    uint32_t viewCount{};
    result = g.enumerateViewConfigurationViews(
        instance, systemId, kQuadViewConfiguration, 0, &viewCount, nullptr);
    if (XR_FAILED(result) || viewCount != state.views.size()) {
        state.reason = "view_count";
        return state;
    }
    for (auto& view : state.views) view = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
    result = g.enumerateViewConfigurationViews(
        instance, systemId, kQuadViewConfiguration, viewCount, &viewCount, state.views.data());
    if (XR_FAILED(result) || viewCount != state.views.size()) {
        state.reason = "view_properties";
        return state;
    }
    for (const auto& view : state.views) {
        if (!view.recommendedImageRectWidth || !view.recommendedImageRectHeight ||
            view.recommendedImageRectWidth > view.maxImageRectWidth ||
            view.recommendedImageRectHeight > view.maxImageRectHeight ||
            !view.recommendedSwapchainSampleCount ||
            view.recommendedSwapchainSampleCount > view.maxSwapchainSampleCount ||
            view.maxImageRectWidth < kSourceExtent ||
            view.maxImageRectHeight < kSourceExtent ||
            view.maxSwapchainSampleCount < 2) {
            state.reason = "view_limits";
            return state;
        }
    }

    uint32_t blendCount{};
    result = g.enumerateEnvironmentBlendModes(
        instance, systemId, kQuadViewConfiguration, 0, &blendCount, nullptr);
    if (XR_FAILED(result) || !blendCount) {
        state.reason = "blend_modes";
        return state;
    }
    std::vector<XrEnvironmentBlendMode> blendModes(blendCount);
    result = g.enumerateEnvironmentBlendModes(
        instance, systemId, kQuadViewConfiguration, blendCount, &blendCount, blendModes.data());
    if (XR_FAILED(result)) {
        state.reason = "blend_modes";
        return state;
    }
    bool opaque = false;
    for (XrEnvironmentBlendMode mode : blendModes) {
        if (mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) opaque = true;
    }
    if (!opaque) {
        state.reason = "opaque_blend_missing";
        return state;
    }

    state.quadSupported = true;
    state.reason = "supported";
    return state;
}

bool closeFloat(float left, float right) {
    return std::fabs(left - right) <= 0.0001f;
}

bool samePose(const XrPosef& left, const XrPosef& right) {
    return closeFloat(left.orientation.x, right.orientation.x) &&
        closeFloat(left.orientation.y, right.orientation.y) &&
        closeFloat(left.orientation.z, right.orientation.z) &&
        closeFloat(left.orientation.w, right.orientation.w) &&
        closeFloat(left.position.x, right.position.x) &&
        closeFloat(left.position.y, right.position.y) &&
        closeFloat(left.position.z, right.position.z);
}

bool sameFov(const XrFovf& left, const XrFovf& right) {
    return closeFloat(left.angleLeft, right.angleLeft) &&
        closeFloat(left.angleRight, right.angleRight) &&
        closeFloat(left.angleUp, right.angleUp) &&
        closeFloat(left.angleDown, right.angleDown);
}

bool finiteFov(const XrFovf& fov) {
    return std::isfinite(fov.angleLeft) && std::isfinite(fov.angleRight) &&
        std::isfinite(fov.angleUp) && std::isfinite(fov.angleDown) &&
        fov.angleLeft < fov.angleRight && fov.angleDown < fov.angleUp;
}

bool containedFov(const XrFovf& inner, const XrFovf& outer) {
    return finiteFov(inner) && finiteFov(outer) &&
        inner.angleLeft + 0.0001f >= outer.angleLeft &&
        inner.angleRight - 0.0001f <= outer.angleRight &&
        inner.angleDown + 0.0001f >= outer.angleDown &&
        inner.angleUp - 0.0001f <= outer.angleUp;
}

bool safeProjectionNext(const void* next) {
    if (!next) return true;
    const auto* header = static_cast<const XrBaseInStructure*>(next);
    if (header->type != XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB || header->next) return false;
    return reinterpret_cast<const XrCompositionLayerSettingsFB*>(next)->layerFlags == 0;
}

struct Fingerprint {
    bool valid{};
    const char* reason{"unknown"};
    std::array<const XrCompositionLayerProjection*, 3> projections{};
    std::array<XrSwapchain, 6> handles{};
};

bool sameFovBits(const XrFovf& left, const XrFovf& right) {
    return std::memcmp(&left, &right, sizeof(XrFovf)) == 0;
}

bool validFovTopology(const Fingerprint& fingerprint) {
    const auto* base = fingerprint.projections[0];
    const auto* under = fingerprint.projections[1];
    const auto* fovea = fingerprint.projections[2];
    return sameFov(base->views[0].fov, under->views[0].fov) &&
        sameFov(base->views[1].fov, under->views[1].fov) &&
        finiteFov(under->views[0].fov) && finiteFov(under->views[1].fov) &&
        containedFov(fovea->views[0].fov, under->views[0].fov) &&
        containedFov(fovea->views[1].fov, under->views[1].fov);
}

Fingerprint inspectFingerprint(XrSession session, const XrFrameEndInfo* info) {
    Fingerprint fingerprint;
    if (!info || info->type != XR_TYPE_FRAME_END_INFO || info->next ||
        info->environmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE ||
        info->layerCount != 3 || !info->layers) {
        fingerprint.reason = "layer_count";
        return fingerprint;
    }
    for (uint32_t layer = 0; layer < 3; ++layer) {
        if (!info->layers[layer] ||
            info->layers[layer]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            fingerprint.reason = "non_projection";
            return fingerprint;
        }
        const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[layer]);
        if (projection->viewCount != 2 || !projection->views ||
            !safeProjectionNext(projection->next)) {
            fingerprint.reason = "projection_shape";
            return fingerprint;
        }
        fingerprint.projections[layer] = projection;
    }
    const auto* base = fingerprint.projections[0];
    const auto* under = fingerprint.projections[1];
    const auto* fovea = fingerprint.projections[2];
    if (base->layerFlags != 0 || under->layerFlags != 0 || fovea->layerFlags != kFoveaFlags ||
        base->space != under->space || under->space != fovea->space) {
        fingerprint.reason = "layer_contract";
        return fingerprint;
    }

    for (uint32_t layer = 0; layer < 3; ++layer) {
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const uint32_t sourceIndex = layer * 2 + eye;
            const auto& view = fingerprint.projections[layer]->views[eye];
            const auto& rect = view.subImage.imageRect;
            if (view.next || rect.offset.x || rect.offset.y ||
                rect.extent.width != static_cast<int32_t>(kSourceExtent) ||
                rect.extent.height != static_cast<int32_t>(kSourceExtent) ||
                view.subImage.imageArrayIndex != 0 ||
                !samePose(view.pose, under->views[eye].pose)) {
                fingerprint.reason = "view_contract";
                return fingerprint;
            }
            const auto swapchain = view.subImage.swapchain;
            for (uint32_t previous = 0; previous < sourceIndex; ++previous) {
                if (fingerprint.handles[previous] == swapchain) {
                    fingerprint.reason = "duplicate_swapchain";
                    return fingerprint;
                }
            }
            fingerprint.handles[sourceIndex] = swapchain;
        }
    }

    std::lock_guard<std::mutex> lock(stateMutex);
    auto sessionFound = sessions.find(session);
    if (sessionFound == sessions.end()) {
        fingerprint.reason = "unknown_session";
        return fingerprint;
    }
    SessionState& state = sessionFound->second;
    bool fovsUnchanged = state.fingerprintReady;
    for (uint32_t index = 0; index < fingerprint.handles.size() && fovsUnchanged; ++index) {
        fovsUnchanged = sameFovBits(
            fingerprint.projections[index / 2]->views[index % 2].fov,
            state.fingerprintFovs[index]);
    }
    if (state.fingerprintReady) {
        bool bindingsValid = true;
        for (uint32_t index = 0; index < state.sources.size(); ++index) {
            const auto& binding = state.sources[index];
            bindingsValid = bindingsValid && binding.handle == fingerprint.handles[index] &&
                binding.state && binding.state->generation == binding.generation &&
                binding.state->session == session && binding.state->acquired == 0;
        }
        if (bindingsValid) {
            if (!fovsUnchanged && !validFovTopology(fingerprint)) {
                fingerprint.reason = "fov_topology";
                return fingerprint;
            }
            if (!fovsUnchanged) {
                for (uint32_t index = 0; index < state.fingerprintFovs.size(); ++index)
                    state.fingerprintFovs[index] =
                        fingerprint.projections[index / 2]->views[index % 2].fov;
            }
            ++state.fastFingerprintCount;
            fingerprint.valid = true;
            fingerprint.reason = "valid_fast";
            return fingerprint;
        }
    }

    ++state.slowFingerprintCount;
    std::array<SessionState::SourceBinding, 6> bindings{};
    for (uint32_t index = 0; index < fingerprint.handles.size(); ++index) {
        auto found = swapchains.find(fingerprint.handles[index]);
        if (found == swapchains.end() || found->second.session != session ||
            found->second.acquired != 0) {
            fingerprint.reason = "source_not_released";
            return fingerprint;
        }
        const auto& createInfo = found->second.info;
        if (createInfo.width != kSourceExtent || createInfo.height != kSourceExtent ||
            createInfo.arraySize != 1 || createInfo.sampleCount != 2 ||
            createInfo.format != kSourceFormat || createInfo.faceCount != 1 ||
            createInfo.mipCount != 1 ||
            createInfo.usageFlags != (XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                XR_SWAPCHAIN_USAGE_SAMPLED_BIT)) {
            fingerprint.reason = "swapchain_contract";
            return fingerprint;
        }
        bindings[index] = {
            fingerprint.handles[index], &found->second, found->second.generation,
        };
    }
    if (!validFovTopology(fingerprint)) {
        fingerprint.reason = "fov_topology";
        return fingerprint;
    }
    state.sources = bindings;
    for (uint32_t index = 0; index < state.fingerprintFovs.size(); ++index)
        state.fingerprintFovs[index] = fingerprint.projections[index / 2]->views[index % 2].fov;
    state.fingerprintReady = true;
    fingerprint.valid = true;
    fingerprint.reason = "valid_slow";
    return fingerprint;
}

bool markSessionUnsafe(XrSession session) {
    bool requestExit = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = sessions.find(session);
        if (found != sessions.end()) {
            found->second.unsafe = true;
            found->second.forceStock = true;
            found->second.locate.valid = false;
            if (!found->second.exitRequested) {
                found->second.exitRequested = true;
                requestExit = true;
            }
        }
    }
    forceNextSessionStock.store(true, std::memory_order_release);
    return requestExit;
}

XrResult markUnsafeAndExit(XrSession session, const XrFrameEndInfo* info, const char* reason) {
    const bool requestExit = markSessionUnsafe(session);
    if (requestExit) emit("native_quad_disabled", "\"session\":" +
        std::to_string(handleValue(session)) + ",\"reason\":\"" + reason + "\"");
    XrFrameEndInfo empty = info ? *info : XrFrameEndInfo{XR_TYPE_FRAME_END_INFO};
    empty.layerCount = 0;
    empty.layers = nullptr;
    XrResult result = g.endFrame(session, &empty);
    if (requestExit) g.requestExitSession(session);
    return result;
}

XrResult markUnsafeAfterEndFrame(XrSession session, XrResult result, const char* reason) {
    const bool requestExit = markSessionUnsafe(session);
    emit("native_quad_disabled", "\"session\":" + std::to_string(handleValue(session)) +
        ",\"reason\":\"" + reason + "\",\"result\":" + std::to_string(result));
    if (requestExit) g.requestExitSession(session);
    return result;
}

XrResult hookCreateInstance(const XrInstanceCreateInfo* info, XrInstance* instance) {
    if (!info || !instance || !initializeDispatch()) return XR_ERROR_INITIALIZATION_FAILED;
    const bool core11 = XR_VERSION_MAJOR(info->applicationInfo.apiVersion) > 1 ||
        (XR_VERSION_MAJOR(info->applicationInfo.apiVersion) == 1 &&
         XR_VERSION_MINOR(info->applicationInfo.apiVersion) >= 1);
    const bool advertised = extensionAdvertised(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
    const bool appEnabled = extensionEnabled(*info, XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
    const bool append = !core11 && advertised && !appEnabled;
    std::vector<const char*> extensions;
    XrInstanceCreateInfo patched = *info;
    if (append) {
        extensions.reserve(info->enabledExtensionCount + 1);
        for (uint32_t index = 0; index < info->enabledExtensionCount; ++index)
            extensions.push_back(info->enabledExtensionNames[index]);
        extensions.push_back(XR_VARJO_QUAD_VIEWS_EXTENSION_NAME);
        patched.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        patched.enabledExtensionNames = extensions.data();
    }
    XrResult result = g.createInstance(&patched, instance);
    if (XR_SUCCEEDED(result)) {
        g.instance = *instance;
        coreQuadEnabled = core11;
        quadExtensionEnabled = appEnabled || append;
        emit("native_quad_initialized", "\"helperName\":\"" + std::string(kHelperName) +
            "\",\"core11\":" + (core11 ? "true" : "false") +
            ",\"extensionAdvertised\":" + (advertised ? "true" : "false") +
            ",\"extensionAppEnabled\":" + (appEnabled ? "true" : "false") +
            ",\"extensionAppended\":" + (append ? "true" : "false"));
    }
    return result;
}

XrResult hookDestroyInstance(XrInstance instance) {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        systems.clear();
        sessions.clear();
        swapchains.clear();
    }
    quadExtensionEnabled = false;
    coreQuadEnabled = false;
    g.instance = XR_NULL_HANDLE;
    streamCallsite.store(0, std::memory_order_release);
    exitCallsite.store(0, std::memory_order_release);
    unexpectedCallsiteLogged.store(false, std::memory_order_release);
    emit("native_quad_destroy_instance");
    return g.destroyInstance(instance);
}

XrResult hookGetSystem(XrInstance instance, const XrSystemGetInfo* info, XrSystemId* systemId) {
    XrResult result = g.getSystem(instance, info, systemId);
    if (XR_FAILED(result) || !systemId) return result;
    SystemState state = probeSystem(instance, *systemId);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        systems[*systemId] = state;
    }
    emit("native_quad_capability", "\"systemId\":" + std::to_string(handleValue(*systemId)) +
        ",\"supported\":" + (state.quadSupported ? "true" : "false") +
        ",\"fovMutable\":" + (state.fovMutable ? "true" : "false") +
        ",\"reason\":\"" + state.reason + "\"");
    return result;
}

XrResult hookCreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
    XrResult result = g.createSession(instance, info, session);
    if (XR_SUCCEEDED(result) && info && session) {
        SessionState state;
        state.systemId = info->systemId;
        std::lock_guard<std::mutex> lock(stateMutex);
        sessions[*session] = state;
    }
    return result;
}

XrResult hookDestroySession(XrSession session) {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        sessions.erase(session);
        for (auto found = swapchains.begin(); found != swapchains.end();) {
            if (found->second.session == session) found = swapchains.erase(found);
            else ++found;
        }
    }
    return g.destroySession(session);
}

XrResult hookBeginSession(XrSession session, const XrSessionBeginInfo* info) {
    if (!info) return g.beginSession(session, info);
    bool useQuad = false;
    const char* reason = "stock_requested";
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = sessions.find(session);
        if (found != sessions.end()) {
            const auto system = systems.find(found->second.systemId);
            const bool globallyForced =
                forceNextSessionStock.exchange(false, std::memory_order_acq_rel);
            const bool forced = found->second.forceStock || globallyForced;
            if (forced) reason = "forced_stock_after_failure";
            else if (info->type != XR_TYPE_SESSION_BEGIN_INFO || info->next)
                reason = "begin_info_chain";
            else if (info->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
                reason = "non_stereo_request";
            else if (system == systems.end() || !system->second.quadSupported)
                reason = "quad_unavailable";
            else useQuad = true;
        } else reason = "unknown_session";
    }

    XrSessionBeginInfo patched = *info;
    if (useQuad) patched.primaryViewConfigurationType = kQuadViewConfiguration;
    XrResult result = g.beginSession(session, useQuad ? &patched : info);
    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = sessions.find(session);
        if (found != sessions.end()) {
            found->second.quadCommitted = useQuad;
            found->second.unsafe = false;
            found->second.exitRequested = false;
            found->second.locate.valid = false;
        }
    }
    emit("native_quad_begin_session", "\"session\":" + std::to_string(handleValue(session)) +
        ",\"quadCommitted\":" + (useQuad && XR_SUCCEEDED(result) ? "true" : "false") +
        ",\"reason\":\"" + (useQuad ? "capability_supported" : reason) + "\"" +
        ",\"result\":" + std::to_string(result));
    return result;
}

XrResult hookEndSession(XrSession session) {
    XrResult result = g.endSession(session);
    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = sessions.find(session);
        if (found != sessions.end()) {
            found->second.quadCommitted = false;
            found->second.locate.valid = false;
        }
    }
    return result;
}

XrResult hookLocateViews(XrSession session, const XrViewLocateInfo* info,
                         XrViewState* viewState, uint32_t capacity,
                         uint32_t* count, XrView* views) {
    bool committed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = sessions.find(session);
        committed = found != sessions.end() && found->second.quadCommitted;
    }
    if (!committed || !info || info->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
        return g.locateViews(session, info, viewState, capacity, count, views);

    if (info->type != XR_TYPE_VIEW_LOCATE_INFO || info->next) {
        const bool requestExit = markSessionUnsafe(session);
        emit("native_quad_disabled", "\"session\":" + std::to_string(handleValue(session)) +
            ",\"reason\":\"locate_info_chain\"");
        if (requestExit) g.requestExitSession(session);
        return XR_ERROR_VALIDATION_FAILURE;
    }

    if (!count || !viewState) return XR_ERROR_VALIDATION_FAILURE;
    XrViewLocateInfo patched = *info;
    patched.viewConfigurationType = kQuadViewConfiguration;
    std::array<XrView, 4> quadViews{};
    for (auto& view : quadViews) view = {XR_TYPE_VIEW};
    uint32_t quadCount{};
    XrResult result = g.locateViews(
        session, &patched, viewState, static_cast<uint32_t>(quadViews.size()), &quadCount, quadViews.data());
    if (XR_FAILED(result) || quadCount != quadViews.size()) {
        const bool requestExit = markSessionUnsafe(session);
        emit("native_quad_disabled", "\"session\":" + std::to_string(handleValue(session)) +
            ",\"reason\":\"locate_quad_failed\",\"result\":" + std::to_string(result));
        if (requestExit) g.requestExitSession(session);
        return XR_FAILED(result) ? result : XR_ERROR_RUNTIME_FAILURE;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = sessions.find(session);
        if (found != sessions.end() && !found->second.unsafe) {
            found->second.locate.valid = true;
            found->second.locate.displayTime = info->displayTime;
            found->second.locate.space = info->space;
            found->second.locate.flags = viewState->viewStateFlags;
            found->second.locate.views = quadViews;
        }
    }
    *count = 2;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 2) return XR_ERROR_SIZE_INSUFFICIENT;
    if (!views) return XR_ERROR_VALIDATION_FAILURE;
    views[0] = quadViews[0];
    views[1] = quadViews[1];
    return result;
}

XrResult hookCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info,
                             XrSwapchain* swapchain) {
    XrResult result = g.createSwapchain(session, info, swapchain);
    if (XR_SUCCEEDED(result) && info && swapchain) {
        SwapchainState state;
        state.session = session;
        state.info = *info;
        state.info.next = nullptr;
        state.generation = nextSwapchainGeneration.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(stateMutex);
        swapchains[*swapchain] = state;
    }
    return result;
}

XrResult hookDestroySwapchain(XrSwapchain swapchain) {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto existing = swapchains.find(swapchain);
        if (existing != swapchains.end()) {
            auto session = sessions.find(existing->second.session);
            if (session != sessions.end()) {
                bool matched = false;
                for (auto& binding : session->second.sources) {
                    if (binding.handle == swapchain) {
                        binding = {};
                        matched = true;
                    }
                }
                if (matched) {
                    session->second.fingerprintReady = false;
                    ++session->second.sourceCacheInvalidationCount;
                }
            }
        }
        swapchains.erase(swapchain);
    }
    return g.destroySwapchain(swapchain);
}

XrResult hookAcquireSwapchainImage(XrSwapchain swapchain,
                                   const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
    XrResult result = g.acquireSwapchainImage(swapchain, info, index);
    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = swapchains.find(swapchain);
        if (found != swapchains.end()) {
            ++found->second.acquired;
            found->second.waited = false;
        }
    }
    return result;
}

XrResult hookWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo* info) {
    XrResult result = g.waitSwapchainImage(swapchain, info);
    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = swapchains.find(swapchain);
        if (found != swapchains.end()) found->second.waited = true;
    }
    return result;
}

XrResult hookReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* info) {
    XrResult result = g.releaseSwapchainImage(swapchain, info);
    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = swapchains.find(swapchain);
        if (found != swapchains.end()) {
            if (found->second.acquired) --found->second.acquired;
            found->second.waited = false;
        }
    }
    return result;
}

XrResult hookEndFrame(XrSession session, const XrFrameEndInfo* info) {
    SessionState state;
    SystemState system;
    bool known{};
    bool committed{};
    bool unsafe{};
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = sessions.find(session);
        known = found != sessions.end();
        if (known) {
            committed = found->second.quadCommitted;
            unsafe = found->second.unsafe;
            state = found->second;
            auto systemFound = systems.find(state.systemId);
            if (systemFound != systems.end()) system = systemFound->second;
        }
    }
    if (!known || !committed) return g.endFrame(session, info);
    if (unsafe) return markUnsafeAndExit(session, info, "session_unsafe");

    Fingerprint fingerprint = inspectFingerprint(session, info);
    if (!fingerprint.valid) return markUnsafeAndExit(session, info, fingerprint.reason);
    if (info->environmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
        return markUnsafeAndExit(session, info, "environment_blend_mode");
    constexpr XrViewStateFlags requiredFlags =
        XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if (!state.locate.valid || state.locate.displayTime != info->displayTime ||
        state.locate.space != fingerprint.projections[1]->space ||
        (state.locate.flags & requiredFlags) != requiredFlags) {
        return markUnsafeAndExit(session, info, "locate_contract");
    }

    const auto* under = fingerprint.projections[1];
    const auto* fovea = fingerprint.projections[2];
    for (uint32_t eye = 0; eye < 2; ++eye) {
        if (!samePose(under->views[eye].pose, state.locate.views[eye].pose) ||
            !samePose(fovea->views[eye].pose, state.locate.views[eye + 2].pose) ||
            !sameFov(under->views[eye].fov, state.locate.views[eye].fov)) {
            return markUnsafeAndExit(session, info, "outer_view_mismatch");
        }
        const bool insetCompatible = system.fovMutable ||
            sameFov(fovea->views[eye].fov, state.locate.views[eye + 2].fov);
        if (!insetCompatible) return markUnsafeAndExit(session, info, "inset_view_mismatch");
    }

    std::array<XrCompositionLayerProjectionView, 4> views{};
    views[0] = under->views[0];
    views[1] = under->views[1];
    views[2] = fovea->views[0];
    views[3] = fovea->views[1];
    for (auto& view : views) view.next = nullptr;
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    // OpenXR exposes flags per projection, not per view. Preserve Valve's foveal alpha contract for
    // this experimental 4-view submission; live acceptance must prove outer alpha is opaque and
    // that the runtime's inset-edge blend has no holes before this mode can be called successful.
    projection.layerFlags = kFoveaFlags;
    projection.space = under->space;
    projection.viewCount = static_cast<uint32_t>(views.size());
    projection.views = views.data();
    const auto* layer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    XrFrameEndInfo output = *info;
    output.layerCount = 1;
    output.layers = &layer;
    XrResult result = g.endFrame(session, &output);
    if (XR_FAILED(result)) return markUnsafeAfterEndFrame(session, result, "quad_end_frame_failed");

    bool summarize = false;
    uint64_t total{};
    uint64_t fastFingerprints{}, slowFingerprints{}, invalidations{};
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto found = sessions.find(session);
        if (found != sessions.end()) {
            ++found->second.successfulFrames;
            ++found->second.framesSinceSummary;
            found->second.locate.valid = false;
            total = found->second.successfulFrames;
            fastFingerprints = found->second.fastFingerprintCount;
            slowFingerprints = found->second.slowFingerprintCount;
            invalidations = found->second.sourceCacheInvalidationCount;
            if (found->second.framesSinceSummary >= kSuccessSummaryInterval) {
                found->second.framesSinceSummary = 0;
                summarize = true;
            }
        }
    }
    if (summarize) emit("native_quad_summary", "\"session\":" +
        std::to_string(handleValue(session)) + ",\"successfulFrames\":" + std::to_string(total) +
        ",\"fastFingerprintCount\":" + std::to_string(fastFingerprints) +
        ",\"slowFingerprintCount\":" + std::to_string(slowFingerprints) +
        ",\"sourceCacheInvalidationCount\":" + std::to_string(invalidations) +
        ",\"sourceProjectionCount\":3,\"outputProjectionCount\":1,\"outputViewCount\":4," 
        "\"reconstructionPasses\":0");
    return result;
}

XrResult hookRequestExitSession(XrSession session) {
    return g.requestExitSession(session);
}

XrResult hookGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);

bool initializeCallsites(const void* returnAddress) {
    if (streamCallsite.load(std::memory_order_acquire)) return true;
    std::lock_guard<std::mutex> lock(callsiteMutex);
    if (streamCallsite.load(std::memory_order_relaxed)) return true;
    Dl_info info{};
    if (!dladdr(returnAddress, &info) || !info.dli_fbase) {
        emit("native_quad_failure", "\"reason\":\"unresolved_callsite\"");
        return false;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
    const uintptr_t offset = reinterpret_cast<uintptr_t>(returnAddress) - base;
    if (offset != 0x0011AA80u && offset != 0x00142064u) {
        emit("native_quad_failure", "\"reason\":\"unexpected_callsite\",\"returnOffset\":" +
            std::to_string(offset));
        return false;
    }
    exitCallsite.store(base + 0x00142064u, std::memory_order_relaxed);
    streamCallsite.store(base + 0x0011AA80u, std::memory_order_release);
    return true;
}

XrResult dispatchPatchedEndFrame(XrSession session, const XrFrameEndInfo* info,
                                const void* returnAddress) {
    if (!initializeDispatch()) return XR_ERROR_RUNTIME_FAILURE;
    if (!initializeCallsites(returnAddress)) return XR_ERROR_RUNTIME_FAILURE;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(returnAddress);
    if (caller == streamCallsite.load(std::memory_order_relaxed)) return hookEndFrame(session, info);
    if (caller == exitCallsite.load(std::memory_order_relaxed)) return hookRequestExitSession(session);
    if (!unexpectedCallsiteLogged.exchange(true, std::memory_order_relaxed)) {
        const uintptr_t base = streamCallsite.load(std::memory_order_relaxed) - 0x0011AA80u;
        emit("native_quad_failure", "\"reason\":\"unexpected_callsite\",\"returnOffset\":" +
            std::to_string(caller - base));
    }
    return XR_ERROR_RUNTIME_FAILURE;
}

} // namespace

GXR_EXPORT XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* info, XrInstance* instance) {
    return hookCreateInstance(info, instance);
}
GXR_EXPORT XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance instance, const char* name,
                                                     PFN_xrVoidFunction* function) {
    return hookGetInstanceProcAddr(instance, name, function);
}
GXR_EXPORT XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance) {
    return hookDestroyInstance(instance);
}
GXR_EXPORT XrResult XRAPI_CALL xrGetSystem(XrInstance instance, const XrSystemGetInfo* info,
                                          XrSystemId* systemId) {
    return hookGetSystem(instance, info, systemId);
}
GXR_EXPORT XrResult XRAPI_CALL xrEnumerateViewConfigurations(
    XrInstance instance, XrSystemId systemId, uint32_t capacity, uint32_t* count,
    XrViewConfigurationType* configurations) {
    return g.enumerateViewConfigurations(instance, systemId, capacity, count, configurations);
}
GXR_EXPORT XrResult XRAPI_CALL xrGetViewConfigurationProperties(
    XrInstance instance, XrSystemId systemId, XrViewConfigurationType type,
    XrViewConfigurationProperties* properties) {
    return g.getViewConfigurationProperties(instance, systemId, type, properties);
}
GXR_EXPORT XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(
    XrInstance instance, XrSystemId systemId, XrViewConfigurationType type,
    uint32_t capacity, uint32_t* count, XrViewConfigurationView* views) {
    return g.enumerateViewConfigurationViews(instance, systemId, type, capacity, count, views);
}
GXR_EXPORT XrResult XRAPI_CALL xrCreateSession(XrInstance instance, const XrSessionCreateInfo* info,
                                              XrSession* session) {
    return hookCreateSession(instance, info, session);
}
GXR_EXPORT XrResult XRAPI_CALL xrDestroySession(XrSession session) {
    return hookDestroySession(session);
}
GXR_EXPORT XrResult XRAPI_CALL xrBeginSession(XrSession session, const XrSessionBeginInfo* info) {
    return hookBeginSession(session, info);
}
GXR_EXPORT XrResult XRAPI_CALL xrEndSession(XrSession session) {
    return hookEndSession(session);
}
GXR_EXPORT XrResult XRAPI_CALL xrLocateViews(
    XrSession session, const XrViewLocateInfo* info, XrViewState* state,
    uint32_t capacity, uint32_t* count, XrView* views) {
    return hookLocateViews(session, info, state, capacity, count, views);
}
GXR_EXPORT XrResult XRAPI_CALL xrCreateSwapchain(
    XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* swapchain) {
    return hookCreateSwapchain(session, info, swapchain);
}
GXR_EXPORT XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain) {
    return hookDestroySwapchain(swapchain);
}
GXR_EXPORT XrResult XRAPI_CALL xrAcquireSwapchainImage(
    XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
    return hookAcquireSwapchainImage(swapchain, info, index);
}
GXR_EXPORT XrResult XRAPI_CALL xrWaitSwapchainImage(
    XrSwapchain swapchain, const XrSwapchainImageWaitInfo* info) {
    return hookWaitSwapchainImage(swapchain, info);
}
GXR_EXPORT XrResult XRAPI_CALL xrReleaseSwapchainImage(
    XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* info) {
    return hookReleaseSwapchainImage(swapchain, info);
}
GXR_EXPORT XrResult XRAPI_CALL xrRequestExitSession(XrSession session) {
    return hookRequestExitSession(session);
}
GXR_EXPORT __attribute__((noinline)) XrResult XRAPI_CALL gxrEndFrame(
    XrSession session, const XrFrameEndInfo* info) {
    return dispatchPatchedEndFrame(session, info, __builtin_return_address(0));
}

namespace {

XrResult hookGetInstanceProcAddr(XrInstance instance, const char* name,
                                 PFN_xrVoidFunction* function) {
    if (!name || !function || !initializeDispatch()) return XR_ERROR_VALIDATION_FAILURE;
#define GXR_ROUTE(symbol) \
    if (std::strcmp(name, #symbol) == 0) { \
        *function = reinterpret_cast<PFN_xrVoidFunction>(symbol); \
        return XR_SUCCESS; \
    }
    GXR_ROUTE(xrCreateInstance)
    GXR_ROUTE(xrGetInstanceProcAddr)
    GXR_ROUTE(xrDestroyInstance)
    GXR_ROUTE(xrGetSystem)
    GXR_ROUTE(xrEnumerateViewConfigurations)
    GXR_ROUTE(xrGetViewConfigurationProperties)
    GXR_ROUTE(xrEnumerateViewConfigurationViews)
    GXR_ROUTE(xrCreateSession)
    GXR_ROUTE(xrDestroySession)
    GXR_ROUTE(xrBeginSession)
    GXR_ROUTE(xrEndSession)
    GXR_ROUTE(xrLocateViews)
    GXR_ROUTE(xrCreateSwapchain)
    GXR_ROUTE(xrDestroySwapchain)
    GXR_ROUTE(xrAcquireSwapchainImage)
    GXR_ROUTE(xrWaitSwapchainImage)
    GXR_ROUTE(xrReleaseSwapchainImage)
    GXR_ROUTE(xrRequestExitSession)
#undef GXR_ROUTE
    return g.getInstanceProcAddr(instance, name, function);
}

} // namespace
