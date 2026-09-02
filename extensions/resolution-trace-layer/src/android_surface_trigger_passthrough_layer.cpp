#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#if GXR_AST_DFR_REARM
#include <android/trace.h>
#endif
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <time.h>
#include <unistd.h>
#include <vector>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

// This API layer is intentionally append-only. Steam Link continues to render its
// build-specific native projection layers into Valve-owned swapchains. We neither
// sample nor rewrite those images. The only added composition work is a static 2x2
// Android Surface quad that activates the Galaxy XR compositor path observed to retain
// full sharpness without SYSTEM_ALERT_WINDOW.

#if GXR_AST_SOURCE_PROJECTION_COUNT != 2 && GXR_AST_SOURCE_PROJECTION_COUNT != 3
#error "GXR_AST_SOURCE_PROJECTION_COUNT must be 2 or 3"
#endif

#if GXR_AST_DFR_REARM
constexpr char kLayerName[] =
    "XR_APILAYER_local_GalaxyXR_android_surface_trigger_dfr_rearm_v1";
constexpr char kModeName[] = "android_surface_trigger_dfr_rearm_v1";
constexpr char kBuildId[] = "android-surface-trigger-dfr-rearm-v1.0-20260902";
#else
constexpr char kLayerName[] =
    "XR_APILAYER_local_GalaxyXR_android_surface_trigger_passthrough_v1";
constexpr char kModeName[] = "android_surface_trigger_passthrough_v1";
#if GXR_AST_SOURCE_PROJECTION_COUNT == 2
constexpr char kBuildId[] = "android-surface-trigger-5001712-v1.0-20260902";
#else
constexpr char kBuildId[] = "android-surface-trigger-passthrough-v1.2-20260902";
#endif
#endif
constexpr char kLogTag[] = "GXRSurfaceTrigger";
constexpr uint32_t kTriggerWidth = 2;
constexpr uint32_t kTriggerHeight = 2;
constexpr uint32_t kSourceProjectionCount = GXR_AST_SOURCE_PROJECTION_COUNT;
constexpr uint32_t kSourceViewCount = kSourceProjectionCount * 2;
constexpr uint32_t kRequiredLayerCount = kSourceProjectionCount + 1;
constexpr uint64_t kInitialDiagnosticFrames = 3;
#if GXR_AST_DFR_REARM
// Give the operator enough time to reach a stable fixed scene before the phase
// change. At 72-120 Hz this is approximately 100-60 seconds.
constexpr uint64_t kWarmupSuccessfulFrames = 7200;
constexpr uint64_t kPeriodicProbeOmittedFrames = 90;
constexpr uint32_t kRearmSuccessfulFrames = 3;

enum RearmReason : uint32_t {
    kRearmNone = 0,
    kRearmComposition = 1u << 0,
    kRearmSwapchain = 1u << 1,
    kRearmVisible = 1u << 2,
    kRearmPeriodic = 1u << 3,
    kRearmEligible = 1u << 4,
};
#endif

struct Dispatch {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrGetInstanceProcAddr getInstanceProcAddr{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrEnumerateViewConfigurationViews enumerateViews{};
    PFN_xrGetSystemProperties getSystemProperties{};
    PFN_xrCreateSession createSession{};
    PFN_xrDestroySession destroySession{};
    PFN_xrCreateReferenceSpace createReferenceSpace{};
    PFN_xrDestroySpace destroySpace{};
    PFN_xrCreateSwapchain createSwapchain{};
    PFN_xrCreateSwapchainAndroidSurfaceKHR createAndroidSurfaceSwapchain{};
    PFN_xrDestroySwapchain destroySwapchain{};
    PFN_xrEndFrame endFrame{};
    PFN_xrPollEvent pollEvent{};
};

struct SwapchainContract {
    // Retain the application's exact format for diagnostics only. In particular, a
    // future RGB10_A2 Valve swapchain passes through unchanged; this helper never
    // substitutes an 8-bit projection swapchain or converts projection pixels.
    int64_t format{};
    uint32_t width{};
    uint32_t height{};
};

struct SessionState {
    std::mutex mutex;
    XrSession session{XR_NULL_HANDLE};
    std::atomic<XrSessionState> state{XR_SESSION_STATE_UNKNOWN};
    uint32_t maxLayerCount{};
    XrSpace viewSpace{XR_NULL_HANDLE};
    XrSwapchain surfaceSwapchain{XR_NULL_HANDLE};
    ANativeWindow* window{};
    bool bufferQueued{};
    std::atomic<bool> triggerReady{};
    std::atomic<bool> failureLogged{};
    std::atomic<bool> passthroughLogged{};
    std::atomic<uint64_t> appendedFrames{};
#if GXR_AST_DFR_REARM
    std::atomic<bool> warmupLogged{};
    std::atomic<bool> omissionLogged{};
    std::atomic<bool> omissionFailureLogged{};
    std::atomic<bool> refreshFailureLogged{};
    std::atomic<uint64_t> omittedFrames{};
    std::atomic<uint64_t> omittedSinceProbe{};
    std::atomic<uint64_t> bufferPostSerial{1};
    std::atomic<uint64_t> stableCompositionSignature{};
    std::atomic<uint64_t> candidateCompositionSignature{};
    std::atomic<uint32_t> candidateCompositionFrames{};
    std::atomic<uint64_t> observedSwapchainGeneration{};
    std::atomic<uint32_t> pendingRearmReasons{};
    std::atomic<uint32_t> activeRearmReasons{};
    std::atomic<uint32_t> rearmFramesRemaining{};
    std::atomic<bool> lastFrameEligible{};
#endif
};

Dispatch g;
JavaVM* applicationVm{};
bool extensionAdvertised{};
bool extensionEnabled{};
std::mutex sessionsMutex;
std::map<XrSession, std::shared_ptr<SessionState>> sessions;
std::mutex swapchainsMutex;
std::map<XrSwapchain, SwapchainContract> applicationSwapchains;
#if GXR_AST_DFR_REARM
std::map<XrSwapchain, XrSession> applicationSwapchainOwners;
std::atomic<uint64_t> applicationSwapchainGeneration{};
#endif
thread_local XrSession cachedSessionHandle{XR_NULL_HANDLE};
thread_local std::weak_ptr<SessionState> cachedSession;

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
    std::ostringstream output;
    output << "{\"schema\":2,\"runId\":\"pid-" << getpid()
           << "\",\"source\":\"openxr\",\"mode\":\"" << kModeName
           << "\",\"buildId\":\"" << kBuildId
           << "\",\"elapsedMs\":" << elapsedMs()
           << ",\"event\":\"" << event << '"';
    if (!fields.empty()) output << ',' << fields;
    output << '}';
    __android_log_write(ANDROID_LOG_INFO, kLogTag, output.str().c_str());
}

template <typename Function>
bool load(const char* name, Function& function) {
    PFN_xrVoidFunction address{};
    const XrResult result = g.getInstanceProcAddr(g.instance, name, &address);
    function = reinterpret_cast<Function>(address);
    return XR_SUCCEEDED(result) && function;
}

bool runtimeAdvertisesExtension(
    PFN_xrGetInstanceProcAddr getInstanceProcAddr,
    const char* extensionName
) {
    PFN_xrVoidFunction address{};
    if (!getInstanceProcAddr || XR_FAILED(getInstanceProcAddr(
            XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", &address)) || !address) {
        return false;
    }
    auto enumerate = reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(address);
    uint32_t count{};
    if (XR_FAILED(enumerate(nullptr, 0, &count, nullptr)) || count == 0) return false;
    std::vector<XrExtensionProperties> properties(count);
    for (auto& property : properties) property.type = XR_TYPE_EXTENSION_PROPERTIES;
    if (XR_FAILED(enumerate(nullptr, count, &count, properties.data()))) return false;
    for (const auto& property : properties) {
        if (std::strcmp(property.extensionName, extensionName) == 0) return true;
    }
    return false;
}

JavaVM* findApplicationVm(const XrInstanceCreateInfo* createInfo) {
    const auto* next = createInfo ?
        static_cast<const XrBaseInStructure*>(createInfo->next) : nullptr;
    for (uint32_t depth = 0; next && depth < 32; ++depth, next = next->next) {
        if (next->type == XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR) {
            const auto* androidInfo =
                reinterpret_cast<const XrInstanceCreateInfoAndroidKHR*>(next);
            return static_cast<JavaVM*>(androidInfo->applicationVM);
        }
    }
    return nullptr;
}

bool isVisible(XrSessionState state) {
    return state == XR_SESSION_STATE_VISIBLE || state == XR_SESSION_STATE_FOCUSED;
}

std::shared_ptr<SessionState> findSession(XrSession session) {
    // xrEndFrame normally stays on 1 render thread. A weak TLS cache removes the
    // global session-map mutex from that hot path without extending session lifetime
    // or risking a stale object when an OpenXR handle is later reused.
    if (cachedSessionHandle == session) {
        if (auto cached = cachedSession.lock()) return cached;
    }
    std::lock_guard<std::mutex> lock(sessionsMutex);
    const auto iterator = sessions.find(session);
    if (iterator == sessions.end()) {
        cachedSessionHandle = XR_NULL_HANDLE;
        cachedSession.reset();
        return nullptr;
    }
    cachedSessionHandle = session;
    cachedSession = iterator->second;
    return iterator->second;
}

#if GXR_AST_DFR_REARM
void requestRearm(const std::shared_ptr<SessionState>& state, uint32_t reasons) {
    if (state && reasons != kRearmNone) {
        state->pendingRearmReasons.fetch_or(reasons, std::memory_order_release);
    }
}

std::string rearmReasonText(uint32_t reasons) {
    std::string value;
    const auto append = [&value](const char* item) {
        if (!value.empty()) value += '+';
        value += item;
    };
    if (reasons & kRearmComposition) append("composition_signature");
    if (reasons & kRearmSwapchain) append("application_swapchain");
    if (reasons & kRearmVisible) append("visible_reentry");
    if (reasons & kRearmPeriodic) append("periodic_probe");
    if (reasons & kRearmEligible) append("eligible_reentry");
    return value.empty() ? "none" : value;
}

void traceRearm(
    const char* phase,
    uint32_t reasons,
    uint32_t outputLayerCount,
    uint64_t bufferPostSerial
) {
    const std::string marker = std::string("GXR_AST_") + phase +
        "_reason=" + rearmReasonText(reasons) +
        "_layers=" + std::to_string(outputLayerCount) +
        "_post=" + std::to_string(bufferPostSerial);
    ATrace_beginSection(marker.c_str());
    ATrace_endSection();
}
#endif

std::optional<SwapchainContract> findApplicationSwapchain(XrSwapchain swapchain) {
    std::lock_guard<std::mutex> lock(swapchainsMutex);
    const auto iterator = applicationSwapchains.find(swapchain);
    return iterator != applicationSwapchains.end() ?
        std::optional<SwapchainContract>(iterator->second) : std::nullopt;
}

void cleanupTrigger(SessionState& state) {
    ANativeWindow* window{};
    XrSwapchain surfaceSwapchain{XR_NULL_HANDLE};
    XrSpace viewSpace{XR_NULL_HANDLE};
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.triggerReady.store(false, std::memory_order_release);
        state.bufferQueued = false;
        window = state.window;
        state.window = nullptr;
        surfaceSwapchain = state.surfaceSwapchain;
        state.surfaceSwapchain = XR_NULL_HANDLE;
        viewSpace = state.viewSpace;
        state.viewSpace = XR_NULL_HANDLE;
    }
    if (window) ANativeWindow_release(window);
    if (surfaceSwapchain != XR_NULL_HANDLE && g.destroySwapchain) {
        g.destroySwapchain(surfaceSwapchain);
    }
    if (viewSpace != XR_NULL_HANDLE && g.destroySpace) g.destroySpace(viewSpace);
}

bool obtainNativeWindow(jobject surface, ANativeWindow*& window) {
    if (!applicationVm || !surface) return false;
    JNIEnv* environment{};
    bool attached = false;
    const jint getEnvironment = applicationVm->GetEnv(
        reinterpret_cast<void**>(&environment), JNI_VERSION_1_6);
    if (getEnvironment == JNI_EDETACHED) {
        if (applicationVm->AttachCurrentThread(&environment, nullptr) != JNI_OK) return false;
        attached = true;
    } else if (getEnvironment != JNI_OK || !environment) {
        return false;
    }
    window = ANativeWindow_fromSurface(environment, surface);
    environment->DeleteLocalRef(surface);
    if (attached) applicationVm->DetachCurrentThread();
    return window != nullptr;
}

bool createTrigger(SessionState& state) {
    if (!extensionEnabled || !g.createAndroidSurfaceSwapchain || !applicationVm ||
        state.maxLayerCount < kRequiredLayerCount) {
        emit("surface_trigger_unavailable",
            "\"session\":" + std::to_string(handleValue(state.session)) +
            ",\"extensionEnabled\":" + (extensionEnabled ? std::string("true") : "false") +
            ",\"hasCreateFunction\":" +
                (g.createAndroidSurfaceSwapchain ? std::string("true") : "false") +
            ",\"hasJavaVm\":" + (applicationVm ? std::string("true") : "false") +
            ",\"maxLayerCount\":" + std::to_string(state.maxLayerCount));
        return false;
    }

    XrReferenceSpaceCreateInfo spaceInfo{};
    spaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    const XrResult spaceResult =
        g.createReferenceSpace(state.session, &spaceInfo, &state.viewSpace);
    if (XR_FAILED(spaceResult)) {
        emit("surface_trigger_create_result",
            "\"session\":" + std::to_string(handleValue(state.session)) +
            ",\"spaceResult\":" + std::to_string(spaceResult) +
            ",\"swapchainResult\":" + std::to_string(XR_ERROR_HANDLE_INVALID));
        cleanupTrigger(state);
        return false;
    }

    XrSwapchainCreateInfo swapchainInfo{};
    swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    swapchainInfo.createFlags = 0;
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    // XR_KHR_android_surface_swapchain requires format/sample/face/array/mip to be 0.
    // This describes only the independent 2x2 trigger Surface. It does not constrain
    // or alter the format (sRGB8 today, potentially RGB10_A2 later) of Valve's views.
    swapchainInfo.format = 0;
    swapchainInfo.sampleCount = 0;
    swapchainInfo.width = kTriggerWidth;
    swapchainInfo.height = kTriggerHeight;
    swapchainInfo.faceCount = 0;
    swapchainInfo.arraySize = 0;
    swapchainInfo.mipCount = 0;
    jobject surface{};
    const XrResult swapchainResult = g.createAndroidSurfaceSwapchain(
        state.session, &swapchainInfo, &state.surfaceSwapchain, &surface);
    const bool windowReady = XR_SUCCEEDED(swapchainResult) &&
        state.surfaceSwapchain != XR_NULL_HANDLE && obtainNativeWindow(surface, state.window);
    emit("surface_trigger_create_result",
        "\"session\":" + std::to_string(handleValue(state.session)) +
        ",\"spaceResult\":" + std::to_string(spaceResult) +
        ",\"swapchainResult\":" + std::to_string(swapchainResult) +
        ",\"swapchain\":" + std::to_string(handleValue(state.surfaceSwapchain)) +
        ",\"width\":2,\"height\":2,\"usageFlags\":" +
            std::to_string(XR_SWAPCHAIN_USAGE_SAMPLED_BIT) +
        ",\"format\":0,\"sampleCount\":0," +
        "\"faceCount\":0,\"arraySize\":0,\"mipCount\":0," +
        "\"nativeWindowReady\":" + (windowReady ? std::string("true") : "false"));
    if (!windowReady) {
        cleanupTrigger(state);
        return false;
    }
    return true;
}

bool queueTriggerBuffer(SessionState& state) {
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.window || state.bufferQueued ||
            !isVisible(state.state.load(std::memory_order_acquire))) return false;
        const int geometryResult = ANativeWindow_setBuffersGeometry(
            state.window, kTriggerWidth, kTriggerHeight, WINDOW_FORMAT_RGBA_8888);
        ANativeWindow_Buffer buffer{};
        const int lockResult = geometryResult == 0 ?
            ANativeWindow_lock(state.window, &buffer, nullptr) : geometryResult;
        int postResult = lockResult;
        const bool writableBuffer = lockResult == 0 && buffer.bits &&
            buffer.stride >= static_cast<int32_t>(kTriggerWidth);
        if (writableBuffer) {
            for (uint32_t y = 0; y < kTriggerHeight; ++y) {
                auto* row = static_cast<uint8_t*>(buffer.bits) +
                    static_cast<size_t>(y) * static_cast<size_t>(buffer.stride) * 4;
                for (uint32_t x = 0; x < kTriggerWidth; ++x) {
                    row[x * 4 + 0] = 0;
                    row[x * 4 + 1] = 0;
                    row[x * 4 + 2] = 0;
                    row[x * 4 + 3] = 1;
                }
            }
        }
        if (lockResult == 0) postResult = ANativeWindow_unlockAndPost(state.window);
        state.bufferQueued = geometryResult == 0 && writableBuffer && postResult == 0;
        state.triggerReady.store(state.bufferQueued, std::memory_order_release);
        queued = state.bufferQueued;
        emit("surface_buffer_queued",
            "\"session\":" + std::to_string(handleValue(state.session)) +
            ",\"sessionState\":" + std::to_string(static_cast<int>(
                state.state.load(std::memory_order_acquire))) +
            ",\"geometryResult\":" + std::to_string(geometryResult) +
            ",\"lockResult\":" + std::to_string(lockResult) +
            ",\"postResult\":" + std::to_string(postResult) +
            ",\"rgba\":[0,0,0,1],\"queued\":" +
                (state.bufferQueued ? std::string("true") : "false"));
    }
    if (!queued) cleanupTrigger(state);
    return queued;
}

#if GXR_AST_DFR_REARM
// Refresh the existing Surface producer instead of replacing any OpenXR object.
// Alternating the almost-transparent alpha makes each pulse a real buffer
// transaction while keeping the 2x2 trigger visually negligible.
bool refreshTriggerBuffer(SessionState& state, uint32_t reasons) {
    int lockResult = -1;
    int postResult = -1;
    uint8_t alpha = 1;
    uint64_t serial = state.bufferPostSerial.load(std::memory_order_relaxed);
    bool posted = false;
    {
        // This lock is taken only for a 3-frame re-arm pulse, never on the normal
        // omitted-frame path.
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.window || !state.bufferQueued ||
            !state.triggerReady.load(std::memory_order_acquire) ||
            !isVisible(state.state.load(std::memory_order_acquire))) {
            lockResult = -1;
        } else {
            ANativeWindow_Buffer buffer{};
            lockResult = ANativeWindow_lock(state.window, &buffer, nullptr);
            const bool writableBuffer = lockResult == 0 && buffer.bits &&
                buffer.stride >= static_cast<int32_t>(kTriggerWidth);
            alpha = (serial & 1u) == 0 ? 1 : 2;
            if (writableBuffer) {
                for (uint32_t y = 0; y < kTriggerHeight; ++y) {
                    auto* row = static_cast<uint8_t*>(buffer.bits) +
                        static_cast<size_t>(y) * static_cast<size_t>(buffer.stride) * 4;
                    for (uint32_t x = 0; x < kTriggerWidth; ++x) {
                        row[x * 4 + 0] = 0;
                        row[x * 4 + 1] = 0;
                        row[x * 4 + 2] = 0;
                        row[x * 4 + 3] = alpha;
                    }
                }
            }
            if (lockResult == 0) postResult = ANativeWindow_unlockAndPost(state.window);
            posted = writableBuffer && postResult == 0;
            if (posted) {
                serial = state.bufferPostSerial.fetch_add(
                    1, std::memory_order_relaxed) + 1;
            }
        }
    }

    if (posted) {
        emit("surface_trigger_buffer_reposted",
            "\"session\":" + std::to_string(handleValue(state.session)) +
            ",\"reason\":\"" + rearmReasonText(reasons) + "\"" +
            ",\"bufferPostSerial\":" + std::to_string(serial) +
            ",\"alpha\":" + std::to_string(alpha) +
            ",\"lockResult\":" + std::to_string(lockResult) +
            ",\"postResult\":" + std::to_string(postResult));
    } else if (!state.refreshFailureLogged.exchange(true, std::memory_order_relaxed)) {
        // A refresh failure does not invalidate the already-presented buffer or any
        // trigger handle. The quad pulse can still reassert compositor topology.
        emit("surface_trigger_buffer_repost_failed",
            "\"session\":" + std::to_string(handleValue(state.session)) +
            ",\"reason\":\"" + rearmReasonText(reasons) + "\"" +
            ",\"bufferPostSerial\":" + std::to_string(serial) +
            ",\"lockResult\":" + std::to_string(lockResult) +
            ",\"postResult\":" + std::to_string(postResult) +
            ",\"triggerRetained\":true");
    }
    return posted;
}
#endif

bool valveProjectionShape(
    const XrFrameEndInfo* info,
    [[maybe_unused]] uint32_t maxLayerCount
) {
    if (!info || info->type != XR_TYPE_FRAME_END_INFO ||
#if GXR_AST_DFR_REARM
        info->layerCount == 0 || info->layerCount >= maxLayerCount ||
#else
        info->layerCount != kSourceProjectionCount ||
#endif
        !info->layers) return false;
    uint32_t projectionCount = 0;
    for (uint32_t index = 0; index < info->layerCount; ++index) {
        const auto* base = info->layers[index];
        if (!base) return false;
#if !GXR_AST_DFR_REARM
        if (base->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) return false;
#else
        if (base->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
#endif
        const auto* projection =
            reinterpret_cast<const XrCompositionLayerProjection*>(base);
        if (projection->viewCount != 2 || !projection->views) return false;
        ++projectionCount;
    }
    return projectionCount == kSourceProjectionCount;
}

#if GXR_AST_DFR_REARM
uint64_t mixSignature(uint64_t hash, uint64_t value) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    for (uint32_t byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xffu;
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t compositionSignature(const XrFrameEndInfo* info) {
    // Hash stable composition resources and rectangles, not pose/FOV values that
    // legitimately change every frame with head tracking and foveation.
    uint64_t hash = 1469598103934665603ULL;
    hash = mixSignature(hash, info->layerCount);
    hash = mixSignature(hash, static_cast<uint64_t>(info->environmentBlendMode));
    const auto mixSubImage = [&hash](const XrSwapchainSubImage& subImage) {
        hash = mixSignature(hash, handleValue(subImage.swapchain));
        hash = mixSignature(hash, static_cast<uint64_t>(
            static_cast<uint32_t>(subImage.imageRect.offset.x)));
        hash = mixSignature(hash, static_cast<uint64_t>(
            static_cast<uint32_t>(subImage.imageRect.offset.y)));
        hash = mixSignature(hash, static_cast<uint64_t>(
            static_cast<uint32_t>(subImage.imageRect.extent.width)));
        hash = mixSignature(hash, static_cast<uint64_t>(
            static_cast<uint32_t>(subImage.imageRect.extent.height)));
        hash = mixSignature(hash, subImage.imageArrayIndex);
    };
    for (uint32_t layerIndex = 0; layerIndex < info->layerCount; ++layerIndex) {
        const auto* layer = info->layers[layerIndex];
        hash = mixSignature(hash, static_cast<uint64_t>(layer->type));
        hash = mixSignature(hash, static_cast<uint64_t>(layer->layerFlags));
        hash = mixSignature(hash, handleValue(layer->space));
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto* projection =
                reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            hash = mixSignature(hash, projection->viewCount);
            for (uint32_t viewIndex = 0; viewIndex < projection->viewCount; ++viewIndex) {
                mixSubImage(projection->views[viewIndex].subImage);
            }
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const auto* quad = reinterpret_cast<const XrCompositionLayerQuad*>(layer);
            mixSubImage(quad->subImage);
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR) {
            const auto* cylinder =
                reinterpret_cast<const XrCompositionLayerCylinderKHR*>(layer);
            mixSubImage(cylinder->subImage);
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR) {
            const auto* equirect =
                reinterpret_cast<const XrCompositionLayerEquirectKHR*>(layer);
            mixSubImage(equirect->subImage);
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) {
            const auto* equirect =
                reinterpret_cast<const XrCompositionLayerEquirect2KHR*>(layer);
            mixSubImage(equirect->subImage);
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_CUBE_KHR) {
            const auto* cube = reinterpret_cast<const XrCompositionLayerCubeKHR*>(layer);
            hash = mixSignature(hash, handleValue(cube->swapchain));
            hash = mixSignature(hash, cube->imageArrayIndex);
        }
    }
    return hash;
}

bool observeStableCompositionChange(SessionState& state, uint64_t signature) {
    const uint64_t stable =
        state.stableCompositionSignature.load(std::memory_order_relaxed);
    if (stable == signature) {
        state.candidateCompositionFrames.store(0, std::memory_order_relaxed);
        return false;
    }
    const uint64_t candidate =
        state.candidateCompositionSignature.load(std::memory_order_relaxed);
    uint32_t frames = 1;
    if (candidate == signature) {
        frames = state.candidateCompositionFrames.fetch_add(
            1, std::memory_order_relaxed) + 1;
    } else {
        state.candidateCompositionSignature.store(signature, std::memory_order_relaxed);
        state.candidateCompositionFrames.store(1, std::memory_order_relaxed);
    }
    if (frames < 2) return false;
    state.stableCompositionSignature.store(signature, std::memory_order_relaxed);
    state.candidateCompositionFrames.store(0, std::memory_order_relaxed);
    return stable != 0;
}

std::string sourceLayerTypes(const XrFrameEndInfo* info) {
    std::ostringstream output;
    output << '[';
    for (uint32_t index = 0; index < info->layerCount; ++index) {
        if (index) output << ',';
        output << static_cast<uint32_t>(info->layers[index]->type);
    }
    output << ']';
    return output.str();
}
#endif

std::string sourceFrameContract(const XrFrameEndInfo* info, uint64_t frame) {
    std::ostringstream output;
    output << "\"frame\":" << frame
           << ",\"sourceLayerCount\":" << info->layerCount
           << ",\"sourceProjectionCount\":" << kSourceProjectionCount
           << ",\"sourceViewCount\":" << kSourceViewCount
           << ",\"projections\":[";
    uint32_t projectionIndex = 0;
    for (uint32_t layerIndex = 0; layerIndex < info->layerCount; ++layerIndex) {
        if (info->layers[layerIndex]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            continue;
        }
        if (projectionIndex++) output << ',';
        const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(
            info->layers[layerIndex]);
        output << "{\"layerIndex\":" << layerIndex
               << ",\"pointer\":" << handleValue(info->layers[layerIndex])
               << ",\"viewCount\":2,\"views\":[";
        for (uint32_t viewIndex = 0; viewIndex < 2; ++viewIndex) {
            if (viewIndex) output << ',';
            const auto& subImage = projection->views[viewIndex].subImage;
            const auto contract = findApplicationSwapchain(subImage.swapchain);
            output << "{\"viewIndex\":" << viewIndex
                   << ",\"swapchain\":" << handleValue(subImage.swapchain)
                   << ",\"format\":"
                   << (contract ? contract->format : 0)
                   << ",\"swapchainWidth\":"
                   << (contract ? contract->width : 0)
                   << ",\"swapchainHeight\":"
                   << (contract ? contract->height : 0)
                   << ",\"arrayIndex\":" << subImage.imageArrayIndex
                   << ",\"offsetX\":" << subImage.imageRect.offset.x
                   << ",\"offsetY\":" << subImage.imageRect.offset.y
                   << ",\"width\":" << subImage.imageRect.extent.width
                   << ",\"height\":" << subImage.imageRect.extent.height << '}';
        }
        output << "]}";
    }
    output << ']';
    return output.str();
}

XrResult XRAPI_PTR layerEnumerateViews(
    XrInstance instance,
    XrSystemId system,
    XrViewConfigurationType type,
    uint32_t capacity,
    uint32_t* count,
    XrViewConfigurationView* views
) {
    const XrResult result = g.enumerateViews(instance, system, type, capacity, count, views);
    if (XR_SUCCEEDED(result) && type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO &&
        count && views && capacity >= *count) {
        for (uint32_t eye = 0; eye < *count; ++eye) {
            emit("view_configuration_eye",
                "\"eye\":" + std::to_string(eye) +
                ",\"viewCount\":" + std::to_string(*count) +
                ",\"recommendedWidth\":" +
                    std::to_string(views[eye].recommendedImageRectWidth) +
                ",\"recommendedHeight\":" +
                    std::to_string(views[eye].recommendedImageRectHeight) +
                ",\"maxWidth\":" + std::to_string(views[eye].maxImageRectWidth) +
                ",\"maxHeight\":" + std::to_string(views[eye].maxImageRectHeight));
        }
    }
    return result;
}

XrResult XRAPI_PTR layerCreateSession(
    XrInstance instance,
    const XrSessionCreateInfo* info,
    XrSession* session
) {
    const XrResult result = g.createSession(instance, info, session);
    if (XR_FAILED(result) || !info || !session) return result;
    auto state = std::make_shared<SessionState>();
    state->session = *session;
    XrSystemProperties properties{};
    properties.type = XR_TYPE_SYSTEM_PROPERTIES;
    const XrResult propertiesResult =
        g.getSystemProperties(instance, info->systemId, &properties);
    if (XR_SUCCEEDED(propertiesResult)) {
        state->maxLayerCount = properties.graphicsProperties.maxLayerCount;
    }
    createTrigger(*state);
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        sessions[*session] = state;
    }
    emit("session_created",
        "\"session\":" + std::to_string(handleValue(*session)) +
        ",\"result\":" + std::to_string(result) +
        ",\"systemPropertiesResult\":" + std::to_string(propertiesResult) +
        ",\"maxLayerCount\":" + std::to_string(state->maxLayerCount));
    return result;
}

XrResult XRAPI_PTR layerDestroySession(XrSession session) {
    std::shared_ptr<SessionState> state;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        const auto iterator = sessions.find(session);
        if (iterator != sessions.end()) {
            state = iterator->second;
            sessions.erase(iterator);
        }
    }
    if (state) {
        uint64_t appendedFrames{};
#if GXR_AST_DFR_REARM
        uint64_t omittedFrames{};
#endif
        {
            appendedFrames = state->appendedFrames.load(std::memory_order_relaxed);
#if GXR_AST_DFR_REARM
            omittedFrames = state->omittedFrames.load(std::memory_order_relaxed);
#endif
        }
        emit("surface_trigger_summary",
            "\"session\":" + std::to_string(handleValue(session)) +
            ",\"appendedFrames\":" + std::to_string(appendedFrames)
#if GXR_AST_DFR_REARM
            + ",\"omittedFrames\":" + std::to_string(omittedFrames) +
            ",\"warmupSuccessfulFrames\":" +
                std::to_string(kWarmupSuccessfulFrames)
#endif
        );
        cleanupTrigger(*state);
    }
    return g.destroySession(session);
}

XrResult XRAPI_PTR layerCreateSwapchain(
    XrSession session,
    const XrSwapchainCreateInfo* info,
    XrSwapchain* swapchain
) {
    const XrResult result = g.createSwapchain(session, info, swapchain);
    if (XR_SUCCEEDED(result) && info && swapchain && *swapchain != XR_NULL_HANDLE) {
        {
            std::lock_guard<std::mutex> lock(swapchainsMutex);
            applicationSwapchains[*swapchain] = {info->format, info->width, info->height};
#if GXR_AST_DFR_REARM
            applicationSwapchainOwners[*swapchain] = session;
#endif
        }
#if GXR_AST_DFR_REARM
        applicationSwapchainGeneration.fetch_add(1, std::memory_order_release);
        requestRearm(findSession(session), kRearmSwapchain);
#endif
    }
    if (info) {
        emit("create_swapchain",
            "\"session\":" + std::to_string(handleValue(session)) +
            ",\"result\":" + std::to_string(result) +
            ",\"swapchain\":" + std::to_string(
                XR_SUCCEEDED(result) && swapchain ? handleValue(*swapchain) : 0) +
            ",\"format\":" + std::to_string(info->format) +
            ",\"width\":" + std::to_string(info->width) +
            ",\"height\":" + std::to_string(info->height) +
            ",\"androidSurfaceSwapchain\":false");
    }
    return result;
}

XrResult XRAPI_PTR layerDestroySwapchain(XrSwapchain swapchain) {
#if GXR_AST_DFR_REARM
    XrSession owner = XR_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(swapchainsMutex);
        const auto iterator = applicationSwapchainOwners.find(swapchain);
        if (iterator != applicationSwapchainOwners.end()) owner = iterator->second;
    }
#endif
    const XrResult result = g.destroySwapchain(swapchain);
    if (XR_SUCCEEDED(result)) {
        {
            std::lock_guard<std::mutex> lock(swapchainsMutex);
            applicationSwapchains.erase(swapchain);
#if GXR_AST_DFR_REARM
            applicationSwapchainOwners.erase(swapchain);
#endif
        }
#if GXR_AST_DFR_REARM
        applicationSwapchainGeneration.fetch_add(1, std::memory_order_release);
        if (owner != XR_NULL_HANDLE) requestRearm(findSession(owner), kRearmSwapchain);
#endif
    }
    return result;
}

XrResult XRAPI_PTR layerEndFrame(XrSession session, const XrFrameEndInfo* info) {
    const auto state = findSession(session);
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrSwapchain surfaceSwapchain = XR_NULL_HANDLE;
    bool triggerReady = false;
    uint64_t appendedBefore = 0;
    if (state) {
        // Handles are immutable until externally synchronized session destruction.
        sessionState = state->state.load(std::memory_order_acquire);
        triggerReady = state->triggerReady.load(std::memory_order_acquire);
        if (triggerReady) {
            viewSpace = state->viewSpace;
            surfaceSwapchain = state->surfaceSwapchain;
        }
        appendedBefore = state->appendedFrames.load(std::memory_order_relaxed);
    }
    const bool exactValveShape = valveProjectionShape(
        info, state ? state->maxLayerCount : 0);
    const bool eligible = state && triggerReady && isVisible(sessionState) && exactValveShape;
    if (!eligible) {
#if GXR_AST_DFR_REARM
        if (state) state->lastFrameEligible.store(false, std::memory_order_relaxed);
#endif
        const bool shouldLog = state &&
            !state->passthroughLogged.exchange(true, std::memory_order_relaxed);
        if (shouldLog) {
            emit("surface_trigger_passthrough",
                "\"session\":" + std::to_string(handleValue(session)) +
                ",\"sourceLayerCount\":" + std::to_string(info ? info->layerCount : 0) +
                ",\"triggerReady\":" +
                    (triggerReady ? std::string("true") : "false") +
                ",\"changed\":false,\"noCopy\":true,\"noReconstruction\":true");
        }
        return g.endFrame(session, info);
    }

#if GXR_AST_DFR_REARM
    const bool eligibleReentry = !state->lastFrameEligible.exchange(
        true, std::memory_order_relaxed);
    const uint64_t signature = compositionSignature(info);
    const uint64_t previousSignature =
        state->stableCompositionSignature.load(std::memory_order_relaxed);
    const bool signatureChanged = observeStableCompositionChange(*state, signature);
    const uint64_t swapchainGeneration =
        applicationSwapchainGeneration.load(std::memory_order_acquire);
    const bool warmupComplete = appendedBefore >= kWarmupSuccessfulFrames;
    if (!warmupComplete) {
        // Learn the baseline while the quad is already continuously present.
        state->observedSwapchainGeneration.store(
            swapchainGeneration, std::memory_order_relaxed);
        state->pendingRearmReasons.store(kRearmNone, std::memory_order_relaxed);
    } else {
        if (eligibleReentry) requestRearm(state, kRearmEligible);
        if (signatureChanged) {
            requestRearm(state, kRearmComposition);
            emit("surface_trigger_composition_changed",
                "\"session\":" + std::to_string(handleValue(session)) +
                ",\"previousSignature\":" + std::to_string(previousSignature) +
                ",\"signature\":" + std::to_string(signature) +
                ",\"sourceLayerCount\":" + std::to_string(info->layerCount) +
                ",\"sourceLayerTypes\":" + sourceLayerTypes(info));
        }
        const uint64_t previousGeneration = state->observedSwapchainGeneration.exchange(
            swapchainGeneration, std::memory_order_acq_rel);
        if (previousGeneration != swapchainGeneration) requestRearm(state, kRearmSwapchain);
        if (state->omittedSinceProbe.load(std::memory_order_relaxed) >=
            kPeriodicProbeOmittedFrames) {
            requestRearm(state, kRearmPeriodic);
        }
        const uint32_t reasons = state->pendingRearmReasons.exchange(
            kRearmNone, std::memory_order_acq_rel);
        if (reasons != kRearmNone) {
            const uint32_t active = state->activeRearmReasons.fetch_or(
                reasons, std::memory_order_relaxed) | reasons;
            state->rearmFramesRemaining.store(
                kRearmSuccessfulFrames, std::memory_order_relaxed);
            state->omittedSinceProbe.store(0, std::memory_order_relaxed);
            const bool reposted = refreshTriggerBuffer(*state, active);
            const uint64_t postSerial =
                state->bufferPostSerial.load(std::memory_order_relaxed);
            traceRearm("rearm_started", active, info->layerCount + 1, postSerial);
            emit("surface_trigger_rearm_started",
                "\"session\":" + std::to_string(handleValue(session)) +
                ",\"reason\":\"" + rearmReasonText(active) + "\"" +
                ",\"sourceLayerCount\":" + std::to_string(info->layerCount) +
                ",\"outputLayerCount\":" + std::to_string(info->layerCount + 1) +
                ",\"bufferReposted\":" + (reposted ? std::string("true") : "false") +
                ",\"bufferPostSerial\":" + std::to_string(postSerial) +
                ",\"successfulFramesRequested\":" +
                    std::to_string(kRearmSuccessfulFrames));
        }
    }

    // DFR-UI exposes no reliable Android event, so every 90 successful omitted
    // frames a bounded pulse also tests and refreshes the vendor policy.
    if (warmupComplete &&
        state->rearmFramesRemaining.load(std::memory_order_relaxed) == 0) {
        const XrResult result = g.endFrame(session, info);
        bool logTransition = false;
        bool logFailure = false;
        uint64_t omittedFrames = state->omittedFrames.load(std::memory_order_relaxed);
        if (XR_SUCCEEDED(result)) {
            omittedFrames = state->omittedFrames.fetch_add(
                1, std::memory_order_relaxed) + 1;
            state->omittedSinceProbe.fetch_add(1, std::memory_order_relaxed);
            logTransition = !state->omissionLogged.exchange(
                true, std::memory_order_relaxed);
        } else {
            logFailure = !state->omissionFailureLogged.exchange(
                true, std::memory_order_relaxed);
        }
        if (logTransition) {
            traceRearm("quad_omitted", kRearmNone, info->layerCount,
                state->bufferPostSerial.load(std::memory_order_relaxed));
            emit("surface_trigger_quad_omitted",
                sourceFrameContract(info, omittedFrames) +
                ",\"sessionState\":" +
                    std::to_string(static_cast<int>(sessionState)) +
                ",\"outputLayerCount\":" + std::to_string(info->layerCount) +
                ",\"triggerTerminal\":false," +
                "\"surfaceRetained\":true,\"swapchainRetained\":true," +
                "\"nativeWindowRetained\":true,\"bufferRetained\":true," +
                "\"acceptedFrames\":" + std::to_string(appendedBefore) +
                ",\"result\":" + std::to_string(result));
        } else if (logFailure) {
            emit("surface_trigger_omitted_submission_failed",
                "\"session\":" + std::to_string(handleValue(session)) +
                ",\"acceptedFrames\":" + std::to_string(appendedBefore) +
                ",\"result\":" + std::to_string(result));
        }
        return result;
    }
#endif

    XrCompositionLayerQuad quad{};
    quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = viewSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = surfaceSwapchain;
    quad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(kTriggerWidth),
        static_cast<int32_t>(kTriggerHeight)}};
    quad.subImage.imageArrayIndex = 0;
    quad.pose.orientation.w = 1.0f;
    quad.pose.position.z = -1.0f;
    quad.size = {0.001f, 0.001f};

    // Preserve every source pointer and its order; only append the terminal quad.
#if GXR_AST_DFR_REARM
    thread_local std::vector<const XrCompositionLayerBaseHeader*> layers;
    layers.clear();
    if (layers.capacity() < info->layerCount + 1) layers.reserve(info->layerCount + 1);
    for (uint32_t index = 0; index < info->layerCount; ++index) {
        layers.push_back(info->layers[index]);
    }
    layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad));
#else
    std::array<const XrCompositionLayerBaseHeader*, kRequiredLayerCount> layers{};
    for (uint32_t index = 0; index < kSourceProjectionCount; ++index) {
        layers[index] = info->layers[index];
    }
    layers[kSourceProjectionCount] =
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
#endif
    XrFrameEndInfo output = *info;
    output.layerCount = info->layerCount + 1;
    output.layers = layers.data();
    bool pointersPreserved = true;
    for (uint32_t index = 0; index < info->layerCount; ++index) {
        pointersPreserved = pointersPreserved && layers[index] == info->layers[index];
    }
    const XrResult result = g.endFrame(session, &output);
    uint64_t appendedFrames = appendedBefore;
#if GXR_AST_DFR_REARM
    bool logWarmup = false;
    uint32_t completedRearmReasons = kRearmNone;
#endif
    if (XR_SUCCEEDED(result)) {
        appendedFrames = state->appendedFrames.fetch_add(
            1, std::memory_order_relaxed) + 1;
#if GXR_AST_DFR_REARM
        logWarmup = !state->warmupLogged.exchange(true, std::memory_order_relaxed);
        if (warmupComplete) {
            const uint32_t remaining = state->rearmFramesRemaining.fetch_sub(
                1, std::memory_order_relaxed) - 1;
            if (remaining == 0) {
                completedRearmReasons = state->activeRearmReasons.exchange(
                    kRearmNone, std::memory_order_relaxed);
            }
        }
#endif
    }
    const bool initialDiagnostic = XR_SUCCEEDED(result) &&
        appendedFrames <= kInitialDiagnosticFrames;
    if (initialDiagnostic) {
        emit("surface_trigger_frame", sourceFrameContract(info, appendedFrames) +
            ",\"sessionState\":" + std::to_string(static_cast<int>(sessionState)) +
            ",\"outputLayerCount\":" + std::to_string(output.layerCount) +
            ",\"triggerTerminal\":true");
    }
#if GXR_AST_DFR_REARM
    if (logWarmup) {
        traceRearm("warmup_started", kRearmNone, output.layerCount,
            state->bufferPostSerial.load(std::memory_order_relaxed));
        emit("surface_trigger_warmup_started",
            "\"session\":" + std::to_string(handleValue(session)) +
            ",\"outputLayerCount\":" + std::to_string(output.layerCount) +
            ",\"warmupSuccessfulFrames\":" +
                std::to_string(kWarmupSuccessfulFrames) +
            ",\"surfaceRetainedAfterWarmup\":true");
    }
    if (completedRearmReasons != kRearmNone) {
        const uint64_t postSerial =
            state->bufferPostSerial.load(std::memory_order_relaxed);
        traceRearm("rearm_completed", completedRearmReasons,
            output.layerCount, postSerial);
        emit("surface_trigger_rearm_completed",
            "\"session\":" + std::to_string(handleValue(session)) +
            ",\"reason\":\"" + rearmReasonText(completedRearmReasons) + "\"" +
            ",\"outputLayerCount\":" + std::to_string(output.layerCount) +
            ",\"bufferPostSerial\":" + std::to_string(postSerial) +
            ",\"successfulFrames\":" + std::to_string(kRearmSuccessfulFrames));
    }
#endif
    if (initialDiagnostic || XR_FAILED(result)) {
        std::ostringstream submission;
        submission << "\"session\":" << handleValue(session)
                   << ",\"frame\":" << appendedFrames
                   << ",\"sourceLayerCount\":" << info->layerCount
                   << ",\"sourceProjectionCount\":" << kSourceProjectionCount
                   << ",\"sourceViewCount\":" << kSourceViewCount
                   << ",\"outputLayerCount\":" << output.layerCount
                   << ",\"submittedProjectionCount\":" << kSourceProjectionCount
                   << ",\"triggerQuadCount\":1,\"originalPointersPreserved\":"
                   << (pointersPreserved ? "true" : "false")
                   << ",\"sourcePointers\":[";
        for (uint32_t index = 0; index < info->layerCount; ++index) {
            if (index) submission << ',';
            submission << handleValue(info->layers[index]);
        }
        submission << "],\"noCopy\":true,\"noReconstruction\":true,"
                   << "\"quadWidthMeters\":0.001,\"quadHeightMeters\":0.001,"
                   << "\"quadDistanceMeters\":1.0,\"result\":" << result;
        emit("surface_trigger_submission", submission.str());
    }
    if (XR_FAILED(result)) {
        state->triggerReady.store(false, std::memory_order_release);
        const bool logFailure = !state->failureLogged.exchange(
            true, std::memory_order_relaxed);
        if (logFailure) {
            emit("surface_trigger_disabled",
                "\"session\":" + std::to_string(handleValue(session)) +
                ",\"reason\":\"end_frame_rejected\",\"result\":" +
                    std::to_string(result) +
                ",\"futureFramesFailOpen\":true");
        }
    }
    return result;
}

XrResult XRAPI_PTR layerPollEvent(XrInstance instance, XrEventDataBuffer* data) {
    const XrResult result = g.pollEvent(instance, data);
    if (XR_FAILED(result) || !data) return result;
    if (data->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        const auto* event = reinterpret_cast<const XrEventDataSessionStateChanged*>(data);
        const auto state = findSession(event->session);
        bool shouldQueue = false;
        XrSessionState previousState = XR_SESSION_STATE_UNKNOWN;
        if (state) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                previousState = state->state.load(std::memory_order_relaxed);
                state->state.store(event->state, std::memory_order_release);
                shouldQueue = isVisible(event->state) && !state->bufferQueued && state->window;
            }
            if (shouldQueue) queueTriggerBuffer(*state);
#if GXR_AST_DFR_REARM
            if (isVisible(event->state) && !isVisible(previousState)) {
                requestRearm(state, kRearmVisible);
            }
#endif
        }
        const bool triggerReady = state &&
            state->triggerReady.load(std::memory_order_acquire);
        emit("session_state_changed",
            "\"session\":" + std::to_string(handleValue(event->session)) +
            ",\"state\":" + std::to_string(static_cast<int>(event->state)) +
            ",\"triggerReady\":" +
                (triggerReady ? std::string("true") : "false"));
    }
    return result;
}

XrResult XRAPI_PTR layerDestroyInstance(XrInstance instance) {
    std::vector<std::shared_ptr<SessionState>> states;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        for (auto& [session, state] : sessions) states.push_back(state);
        sessions.clear();
    }
    for (const auto& state : states) cleanupTrigger(*state);
    {
        std::lock_guard<std::mutex> lock(swapchainsMutex);
        applicationSwapchains.clear();
#if GXR_AST_DFR_REARM
        applicationSwapchainOwners.clear();
#endif
    }
    emit("destroy_instance");
    const XrResult result = g.destroyInstance(instance);
    g = {};
    applicationVm = nullptr;
    extensionAdvertised = false;
    extensionEnabled = false;
    return result;
}

XrResult XRAPI_PTR layerGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function
) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;
#define ROUTE(n, f) if (std::strcmp(name, n) == 0) \
    *function = reinterpret_cast<PFN_xrVoidFunction>(f)
    ROUTE("xrGetInstanceProcAddr", layerGetInstanceProcAddr);
    else ROUTE("xrDestroyInstance", layerDestroyInstance);
    else ROUTE("xrEnumerateViewConfigurationViews", layerEnumerateViews);
    else ROUTE("xrCreateSession", layerCreateSession);
    else ROUTE("xrDestroySession", layerDestroySession);
    else ROUTE("xrCreateSwapchain", layerCreateSwapchain);
    else ROUTE("xrDestroySwapchain", layerDestroySwapchain);
    else ROUTE("xrEndFrame", layerEndFrame);
    else ROUTE("xrPollEvent", layerPollEvent);
    else return g.getInstanceProcAddr ?
        g.getInstanceProcAddr(instance, name, function) : XR_ERROR_FUNCTION_UNSUPPORTED;
#undef ROUTE
    return XR_SUCCESS;
}

XrResult XRAPI_PTR layerCreateApiLayerInstance(
    const XrInstanceCreateInfo* createInfo,
    const XrApiLayerCreateInfo* layerInfo,
    XrInstance* instance
) {
    if (!createInfo || !layerInfo || !layerInfo->nextInfo || !instance) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    applicationVm = findApplicationVm(createInfo);
    bool appEnabled = false;
    for (uint32_t index = 0; index < createInfo->enabledExtensionCount; ++index) {
        if (std::strcmp(createInfo->enabledExtensionNames[index],
                XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME) == 0) {
            appEnabled = true;
            break;
        }
    }
    extensionAdvertised = runtimeAdvertisesExtension(
        layerInfo->nextInfo->nextGetInstanceProcAddr,
        XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
    // Android XR documentation lists this extension as supported, but Galaxy XR did
    // not enumerate it. The validated runtime nevertheless accepts it when requested.
    // Keep the guarded direct request for that exact behavior; if another runtime
    // rejects it, retry once with Valve's original create-info and change nothing.
    const bool forcedExtensionAttempt = !extensionAdvertised && !appEnabled;
    const bool appended = !appEnabled;
    std::vector<const char*> extensions;
    extensions.reserve(createInfo->enabledExtensionCount + (appended ? 1 : 0));
    for (uint32_t index = 0; index < createInfo->enabledExtensionCount; ++index) {
        extensions.push_back(createInfo->enabledExtensionNames[index]);
    }
    if (appended) extensions.push_back(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
    XrInstanceCreateInfo patched = *createInfo;
    if (appended) {
        patched.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        patched.enabledExtensionNames = extensions.data();
    }
    XrApiLayerCreateInfo next = *layerInfo;
    next.nextInfo = layerInfo->nextInfo->next;
    XrResult result = layerInfo->nextInfo->nextCreateApiLayerInstance(
        appended ? &patched : createInfo, &next, instance);
    const XrResult extensionRequestResult = result;
    bool retriedWithoutExtension = false;
    if (XR_FAILED(result) && appended) {
        retriedWithoutExtension = true;
        result = layerInfo->nextInfo->nextCreateApiLayerInstance(createInfo, &next, instance);
    }
    if (XR_FAILED(result)) return result;

    extensionEnabled = appEnabled || (appended && !retriedWithoutExtension);
    g.instance = *instance;
    g.getInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;
    const bool coreLoaded =
        load("xrDestroyInstance", g.destroyInstance) &&
        load("xrEnumerateViewConfigurationViews", g.enumerateViews) &&
        load("xrGetSystemProperties", g.getSystemProperties) &&
        load("xrCreateSession", g.createSession) &&
        load("xrDestroySession", g.destroySession) &&
        load("xrCreateReferenceSpace", g.createReferenceSpace) &&
        load("xrDestroySpace", g.destroySpace) &&
        load("xrCreateSwapchain", g.createSwapchain) &&
        load("xrDestroySwapchain", g.destroySwapchain) &&
        load("xrEndFrame", g.endFrame) &&
        load("xrPollEvent", g.pollEvent);
    const bool surfaceFunctionLookupAttempted = extensionEnabled;
    const bool surfaceFunctionLoaded = surfaceFunctionLookupAttempted &&
        load("xrCreateSwapchainAndroidSurfaceKHR", g.createAndroidSurfaceSwapchain);
    if (!coreLoaded) {
        if (g.destroyInstance) g.destroyInstance(*instance);
        g = {};
        *instance = XR_NULL_HANDLE;
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (!surfaceFunctionLoaded) extensionEnabled = false;
    emit("layer_initialized",
        "\"layerName\":\"" + std::string(kLayerName) +
        "\",\"extensionAdvertised\":" +
            (extensionAdvertised ? std::string("true") : "false") +
        ",\"extensionAppEnabled\":" + (appEnabled ? std::string("true") : "false") +
        ",\"extensionAppended\":" + (appended ? std::string("true") : "false") +
        ",\"forcedExtensionAttempt\":" +
            (forcedExtensionAttempt ? std::string("true") : "false") +
        ",\"extensionRequestResult\":" + std::to_string(extensionRequestResult) +
        ",\"retriedWithoutExtension\":" +
            (retriedWithoutExtension ? std::string("true") : "false") +
        ",\"extensionEnabled\":" + (extensionEnabled ? std::string("true") : "false") +
        ",\"surfaceFunctionLookupAttempted\":" +
            (surfaceFunctionLookupAttempted ? std::string("true") : "false") +
        ",\"surfaceFunctionLoaded\":" +
            (surfaceFunctionLoaded ? std::string("true") : "false") +
        ",\"hasJavaVm\":" + (applicationVm ? std::string("true") : "false") +
        ",\"preservesValveProjectionLayers\":true," +
        "\"noCopy\":true,\"noReconstruction\":true");
    return XR_SUCCESS;
}

}  // namespace

GXR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* request
) {
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
