#include <android/log.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <time.h>
#include <unistd.h>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

constexpr char kLayerName[] = "XR_APILAYER_local_GalaxyXR_permission_surface_trace_v1";
constexpr char kBuildId[] = "permission-surface-trace-v1.1-20260901";
constexpr char kModeName[] = "permission_surface_matrix_v1";
constexpr char kLogTag[] = "GXRSurfaceTrace";

struct Dispatch {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrGetInstanceProcAddr getInstanceProcAddr{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrEnumerateViewConfigurationViews enumerateViews{};
    PFN_xrCreateSession createSession{};
    PFN_xrDestroySession destroySession{};
    PFN_xrCreateSwapchain createSwapchain{};
    PFN_xrDestroySwapchain destroySwapchain{};
    PFN_xrWaitFrame waitFrame{};
    PFN_xrEndFrame endFrame{};
    PFN_xrPollEvent pollEvent{};
};

Dispatch g;
std::atomic<uint64_t> frameCounter{0};
std::atomic<uint64_t> waitCounter{0};
std::atomic<int> currentSessionState{XR_SESSION_STATE_UNKNOWN};
XrSystemId stereoSystem{XR_NULL_SYSTEM_ID};
XrViewConfigurationType stereoType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
bool recommendedResolutionEnabled{};

struct SwapchainContract {
    uint32_t width{};
    uint32_t height{};
    uint32_t arraySize{};
    uint32_t faceCount{};
    uint32_t mipCount{};
    uint32_t sampleCount{};
    int64_t format{};
    XrSwapchainCreateFlags createFlags{};
    XrSwapchainUsageFlags usageFlags{};
};

std::mutex swapchainMutex;
std::unordered_map<uint64_t, SwapchainContract> activeSwapchains;

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

bool sample(uint64_t value) {
    return value <= 12 || value % 90 == 0;
}

void emit(const char* event, const std::string& fields = {}) {
    std::ostringstream output;
    output << "{\"schema\":1,\"runId\":\"pid-" << getpid()
           << "\",\"mode\":\"" << kModeName
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

std::string viewConfigurationFields(
    uint32_t eye,
    uint32_t count,
    const XrViewConfigurationView& view
) {
    return "\"eye\":" + std::to_string(eye) +
        ",\"viewCount\":" + std::to_string(count) +
        ",\"recommendedWidth\":" + std::to_string(view.recommendedImageRectWidth) +
        ",\"recommendedHeight\":" + std::to_string(view.recommendedImageRectHeight) +
        ",\"maxWidth\":" + std::to_string(view.maxImageRectWidth) +
        ",\"maxHeight\":" + std::to_string(view.maxImageRectHeight) +
        ",\"recommendedSampleCount\":" +
            std::to_string(view.recommendedSwapchainSampleCount) +
        ",\"maxSampleCount\":" + std::to_string(view.maxSwapchainSampleCount);
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
    if (XR_SUCCEEDED(result) && type == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && count &&
        views && capacity >= *count) {
        stereoSystem = system;
        stereoType = type;
        for (uint32_t eye = 0; eye < *count; ++eye) {
            emit("view_configuration_eye", viewConfigurationFields(eye, *count, views[eye]));
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
    emit("session_created", "\"result\":" + std::to_string(result) +
        (session ? ",\"session\":" + std::to_string(handleValue(*session)) : std::string{}));
    return result;
}

XrResult XRAPI_PTR layerDestroySession(XrSession session) {
    emit("session_destroyed", "\"session\":" + std::to_string(handleValue(session)));
    return g.destroySession(session);
}

XrResult XRAPI_PTR layerCreateSwapchain(
    XrSession session,
    const XrSwapchainCreateInfo* info,
    XrSwapchain* swapchain
) {
    const XrResult result = g.createSwapchain(session, info, swapchain);
    if (info) {
        emit("create_swapchain", "\"session\":" + std::to_string(handleValue(session)) +
            ",\"result\":" + std::to_string(result) +
            ",\"width\":" + std::to_string(info->width) +
            ",\"height\":" + std::to_string(info->height) +
            ",\"arraySize\":" + std::to_string(info->arraySize) +
            ",\"faceCount\":" + std::to_string(info->faceCount) +
            ",\"mipCount\":" + std::to_string(info->mipCount) +
            ",\"sampleCount\":" + std::to_string(info->sampleCount) +
            ",\"format\":" + std::to_string(info->format) +
            ",\"createFlags\":" + std::to_string(info->createFlags) +
            ",\"usageFlags\":" + std::to_string(info->usageFlags) +
            (swapchain ? ",\"swapchain\":" + std::to_string(handleValue(*swapchain)) :
                std::string{}));
        if (XR_SUCCEEDED(result) && swapchain && *swapchain != XR_NULL_HANDLE) {
            std::lock_guard<std::mutex> lock(swapchainMutex);
            activeSwapchains[handleValue(*swapchain)] = {
                info->width,
                info->height,
                info->arraySize,
                info->faceCount,
                info->mipCount,
                info->sampleCount,
                info->format,
                info->createFlags,
                info->usageFlags,
            };
        }
    }
    return result;
}

XrResult XRAPI_PTR layerDestroySwapchain(XrSwapchain swapchain) {
    const XrResult result = g.destroySwapchain(swapchain);
    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(swapchainMutex);
        activeSwapchains.erase(handleValue(swapchain));
    }
    return result;
}

void emitCurrentViews(const char* event, const char* eyeEvent) {
    std::vector<XrViewConfigurationView> views(2, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    uint32_t count{};
    const XrResult result = stereoSystem != XR_NULL_SYSTEM_ID ?
        g.enumerateViews(g.instance, stereoSystem, stereoType,
            static_cast<uint32_t>(views.size()), &count, views.data()) :
        XR_ERROR_SYSTEM_INVALID;
    emit(event, "\"result\":" + std::to_string(result) +
        ",\"viewCount\":" + std::to_string(count));
    if (XR_SUCCEEDED(result) && count <= views.size()) {
        for (uint32_t eye = 0; eye < count; ++eye) {
            emit(eyeEvent, viewConfigurationFields(eye, count, views[eye]));
        }
    }
}

void emitActiveSwapchains() {
    std::vector<std::pair<uint64_t, SwapchainContract>> snapshot;
    {
        std::lock_guard<std::mutex> lock(swapchainMutex);
        snapshot.assign(activeSwapchains.begin(), activeSwapchains.end());
    }
    std::sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    emit("sampled_swapchain_set", "\"count\":" + std::to_string(snapshot.size()));
    for (const auto& [handle, contract] : snapshot) {
        emit("sampled_swapchain_contract",
            "\"swapchain\":" + std::to_string(handle) +
            ",\"width\":" + std::to_string(contract.width) +
            ",\"height\":" + std::to_string(contract.height) +
            ",\"arraySize\":" + std::to_string(contract.arraySize) +
            ",\"faceCount\":" + std::to_string(contract.faceCount) +
            ",\"mipCount\":" + std::to_string(contract.mipCount) +
            ",\"sampleCount\":" + std::to_string(contract.sampleCount) +
            ",\"format\":" + std::to_string(contract.format) +
            ",\"createFlags\":" + std::to_string(contract.createFlags) +
            ",\"usageFlags\":" + std::to_string(contract.usageFlags));
    }
}

XrResult XRAPI_PTR layerWaitFrame(
    XrSession session,
    const XrFrameWaitInfo* info,
    XrFrameState* state
) {
    const XrResult result = g.waitFrame(session, info, state);
    const uint64_t wait = ++waitCounter;
    const bool sampled = sample(wait);
    if (XR_FAILED(result) || sampled) {
        emit("wait_frame", "\"session\":" + std::to_string(handleValue(session)) +
            ",\"wait\":" + std::to_string(wait) +
            ",\"result\":" + std::to_string(result) +
            ",\"sessionState\":" + std::to_string(currentSessionState.load()) +
            (state ? ",\"predictedDisplayTime\":" + std::to_string(state->predictedDisplayTime) +
                ",\"predictedDisplayPeriod\":" + std::to_string(state->predictedDisplayPeriod) +
                ",\"shouldRender\":" + std::string(state->shouldRender ? "true" : "false") :
                std::string{}));
    }
    if (sampled) {
        emitCurrentViews("sampled_view_configuration", "sampled_view_configuration_eye");
        emitActiveSwapchains();
    }
    return result;
}

std::string frameContract(const XrFrameEndInfo* info, uint64_t frame) {
    std::ostringstream output;
    output << "\"frame\":" << frame;
    if (!info) {
        output << ",\"validFrameEndInfo\":false";
        return output.str();
    }
    output << ",\"validFrameEndInfo\":true,\"displayTime\":" << info->displayTime
           << ",\"environmentBlendMode\":" << static_cast<int>(info->environmentBlendMode)
           << ",\"layerCount\":" << info->layerCount << ",\"layers\":[";
    for (uint32_t layerIndex = 0; layerIndex < info->layerCount; ++layerIndex) {
        if (layerIndex) output << ',';
        const auto* base = info->layers ? info->layers[layerIndex] : nullptr;
        if (!base) {
            output << "{\"index\":" << layerIndex << ",\"null\":true}";
            continue;
        }
        output << "{\"index\":" << layerIndex << ",\"type\":" << base->type
               << ",\"flags\":" << base->layerFlags;
        if (base->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(base);
            output << ",\"space\":" << handleValue(projection->space)
                   << ",\"viewCount\":" << projection->viewCount << ",\"views\":[";
            for (uint32_t viewIndex = 0; viewIndex < projection->viewCount; ++viewIndex) {
                if (viewIndex) output << ',';
                const auto& view = projection->views[viewIndex];
                const auto& rectangle = view.subImage.imageRect;
                output << "{\"index\":" << viewIndex
                       << ",\"swapchain\":" << handleValue(view.subImage.swapchain)
                       << ",\"arrayIndex\":" << view.subImage.imageArrayIndex
                       << ",\"offsetX\":" << rectangle.offset.x
                       << ",\"offsetY\":" << rectangle.offset.y
                       << ",\"width\":" << rectangle.extent.width
                       << ",\"height\":" << rectangle.extent.height
                       << ",\"fovLeft\":" << view.fov.angleLeft
                       << ",\"fovRight\":" << view.fov.angleRight
                       << ",\"fovUp\":" << view.fov.angleUp
                       << ",\"fovDown\":" << view.fov.angleDown << '}';
            }
            output << "]";
        }
        output << '}';
    }
    output << ']';
    return output.str();
}

XrResult XRAPI_PTR layerEndFrame(XrSession session, const XrFrameEndInfo* info) {
    const uint64_t frame = ++frameCounter;
    const bool sampled = sample(frame);
    if (sampled) emit("submitted_frame", "\"session\":" +
        std::to_string(handleValue(session)) +
        ",\"sessionState\":" + std::to_string(currentSessionState.load()) + ',' +
        frameContract(info, frame));
    const XrResult result = g.endFrame(session, info);
    if (sampled || XR_FAILED(result)) {
        emit("end_frame_result", "\"session\":" + std::to_string(handleValue(session)) +
            ",\"frame\":" + std::to_string(frame) +
            ",\"result\":" + std::to_string(result));
    }
    return result;
}

void emitReenumeratedViews(const char* event) {
    emitCurrentViews(event, "recommended_resolution_reenumerated_eye");
}

XrResult XRAPI_PTR layerPollEvent(XrInstance instance, XrEventDataBuffer* data) {
    const XrResult result = g.pollEvent(instance, data);
    if (XR_FAILED(result) || !data) return result;
    if (data->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        const auto* event = reinterpret_cast<const XrEventDataSessionStateChanged*>(data);
        currentSessionState.store(static_cast<int>(event->state));
        emit("session_state_changed", "\"session\":" +
            std::to_string(handleValue(event->session)) +
            ",\"state\":" + std::to_string(static_cast<int>(event->state)) +
            ",\"time\":" + std::to_string(event->time));
    }
    if (recommendedResolutionEnabled &&
        data->type == XR_TYPE_EVENT_DATA_RECOMMENDED_RESOLUTION_CHANGED_ANDROID) {
        emitReenumeratedViews("recommended_resolution_changed");
    }
    return result;
}

XrResult XRAPI_PTR layerDestroyInstance(XrInstance instance) {
    emit("destroy_instance");
    const XrResult result = g.destroyInstance(instance);
    g = {};
    stereoSystem = XR_NULL_SYSTEM_ID;
    recommendedResolutionEnabled = false;
    {
        std::lock_guard<std::mutex> lock(swapchainMutex);
        activeSwapchains.clear();
    }
    frameCounter.store(0);
    waitCounter.store(0);
    currentSessionState.store(XR_SESSION_STATE_UNKNOWN);
    return result;
}

XrResult XRAPI_PTR layerGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function
) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;
#define ROUTE(n, f) if (std::strcmp(name, n) == 0) *function = reinterpret_cast<PFN_xrVoidFunction>(f)
    ROUTE("xrGetInstanceProcAddr", layerGetInstanceProcAddr);
    else ROUTE("xrDestroyInstance", layerDestroyInstance);
    else ROUTE("xrEnumerateViewConfigurationViews", layerEnumerateViews);
    else ROUTE("xrCreateSession", layerCreateSession);
    else ROUTE("xrDestroySession", layerDestroySession);
    else ROUTE("xrCreateSwapchain", layerCreateSwapchain);
    else ROUTE("xrDestroySwapchain", layerDestroySwapchain);
    else ROUTE("xrWaitFrame", layerWaitFrame);
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
    bool appRequestedRecommendedResolution = false;
    for (uint32_t index = 0; index < createInfo->enabledExtensionCount; ++index) {
        if (std::strcmp(createInfo->enabledExtensionNames[index],
                XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME) == 0) {
            appRequestedRecommendedResolution = true;
            break;
        }
    }
    XrApiLayerCreateInfo next = *layerInfo;
    next.nextInfo = layerInfo->nextInfo->next;
    const XrResult result = layerInfo->nextInfo->nextCreateApiLayerInstance(
        createInfo, &next, instance);
    if (XR_FAILED(result)) return result;
    g.instance = *instance;
    g.getInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;
    const bool loaded =
        load("xrDestroyInstance", g.destroyInstance) &&
        load("xrEnumerateViewConfigurationViews", g.enumerateViews) &&
        load("xrCreateSession", g.createSession) &&
        load("xrDestroySession", g.destroySession) &&
        load("xrCreateSwapchain", g.createSwapchain) &&
        load("xrDestroySwapchain", g.destroySwapchain) &&
        load("xrWaitFrame", g.waitFrame) &&
        load("xrEndFrame", g.endFrame) &&
        load("xrPollEvent", g.pollEvent);
    if (!loaded) {
        if (g.destroyInstance) g.destroyInstance(*instance);
        g = {};
        *instance = XR_NULL_HANDLE;
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    recommendedResolutionEnabled = appRequestedRecommendedResolution;
    emit("layer_initialized", "\"layerName\":\"" + std::string(kLayerName) +
        "\",\"recommendedResolutionAppEnabled\":" +
        std::string(appRequestedRecommendedResolution ? "true" : "false") +
        ",\"createInfoMutation\":false" +
        ",\"submissionMutation\":false");
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
