#include <android/log.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <time.h>
#include <unordered_map>
#include <unistd.h>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

constexpr char kLayerName[] = "XR_APILAYER_local_GalaxyXR_resolution_trace";
constexpr char kLogTag[] = "GXRResolutionTrace";

struct Dispatch {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrGetInstanceProcAddr getInstanceProcAddr{nullptr};
    PFN_xrDestroyInstance destroyInstance{nullptr};
    PFN_xrEnumerateViewConfigurationViews enumerateViews{nullptr};
    PFN_xrCreateSwapchain createSwapchain{nullptr};
    PFN_xrDestroySwapchain destroySwapchain{nullptr};
    PFN_xrEndFrame endFrame{nullptr};
    PFN_xrPollEvent pollEvent{nullptr};
};

Dispatch g_dispatch;
std::mutex g_mutex;
std::unordered_map<XrSwapchain, XrSwapchainCreateInfo> g_swapchains;
std::atomic<uint64_t> g_frameCount{0};
std::string g_lastProjectionSignature;

std::string runId() {
    return "pid-" + std::to_string(getpid());
}

uint64_t elapsedMs() {
    timespec value{};
    clock_gettime(CLOCK_BOOTTIME, &value);
    return static_cast<uint64_t>(value.tv_sec) * 1000ULL +
           static_cast<uint64_t>(value.tv_nsec) / 1000000ULL;
}

void emit(const char* event, const std::string& fields = {}) {
    std::ostringstream out;
    out << "{\"schema\":1,\"runId\":\"" << runId()
        << "\",\"source\":\"openxr\",\"elapsedMs\":" << elapsedMs()
        << ",\"event\":\"" << event << "\"";
    if (!fields.empty()) out << ',' << fields;
    out << '}';
    __android_log_write(ANDROID_LOG_INFO, kLogTag, out.str().c_str());
}

template <typename Function>
void load(const char* name, Function& function) {
    PFN_xrVoidFunction address = nullptr;
    if (g_dispatch.getInstanceProcAddr &&
        XR_SUCCEEDED(g_dispatch.getInstanceProcAddr(g_dispatch.instance, name, &address))) {
        function = reinterpret_cast<Function>(address);
    }
}

XrResult XRAPI_PTR traceDestroyInstance(XrInstance instance) {
    emit("destroy_instance");
    PFN_xrDestroyInstance next = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        next = g_dispatch.destroyInstance;
        g_swapchains.clear();
        g_lastProjectionSignature.clear();
    }
    const XrResult result = next ? next(instance) : XR_ERROR_FUNCTION_UNSUPPORTED;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dispatch = {};
    return result;
}

XrResult XRAPI_PTR traceEnumerateViewConfigurationViews(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t capacity,
    uint32_t* count,
    XrViewConfigurationView* views) {
    const XrResult result = g_dispatch.enumerateViews(
        instance, systemId, viewConfigurationType, capacity, count, views);
    if (XR_SUCCEEDED(result) && count && views && capacity >= *count) {
        for (uint32_t i = 0; i < *count; ++i) {
            std::ostringstream fields;
            fields << "\"view\":" << i
                   << ",\"viewType\":" << static_cast<int>(viewConfigurationType)
                   << ",\"recommendedWidth\":" << views[i].recommendedImageRectWidth
                   << ",\"recommendedHeight\":" << views[i].recommendedImageRectHeight
                   << ",\"maxWidth\":" << views[i].maxImageRectWidth
                   << ",\"maxHeight\":" << views[i].maxImageRectHeight
                   << ",\"recommendedSamples\":" << views[i].recommendedSwapchainSampleCount
                   << ",\"maxSamples\":" << views[i].maxSwapchainSampleCount;
            emit("view_configuration", fields.str());
        }
    }
    return result;
}

XrResult XRAPI_PTR traceCreateSwapchain(
    XrSession session,
    const XrSwapchainCreateInfo* createInfo,
    XrSwapchain* swapchain) {
    const XrResult result = g_dispatch.createSwapchain(session, createInfo, swapchain);
    if (XR_SUCCEEDED(result) && createInfo && swapchain) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_swapchains[*swapchain] = *createInfo;
        }
        std::ostringstream fields;
        fields << "\"swapchain\":\"0x" << std::hex
               << reinterpret_cast<uintptr_t>(*swapchain) << std::dec << "\""
               << ",\"width\":" << createInfo->width
               << ",\"height\":" << createInfo->height
               << ",\"arraySize\":" << createInfo->arraySize
               << ",\"sampleCount\":" << createInfo->sampleCount
               << ",\"faceCount\":" << createInfo->faceCount
               << ",\"mipCount\":" << createInfo->mipCount
               << ",\"format\":" << createInfo->format
               << ",\"usageFlags\":" << createInfo->usageFlags;
        emit("create_swapchain", fields.str());
    }
    return result;
}

XrResult XRAPI_PTR traceDestroySwapchain(XrSwapchain swapchain) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_swapchains.erase(swapchain);
    }
    return g_dispatch.destroySwapchain(swapchain);
}

XrResult XRAPI_PTR tracePollEvent(XrInstance instance, XrEventDataBuffer* eventData) {
    const XrResult result = g_dispatch.pollEvent(instance, eventData);
    if (XR_SUCCEEDED(result) && eventData &&
        eventData->type == XR_TYPE_EVENT_DATA_RECOMMENDED_RESOLUTION_CHANGED_ANDROID) {
        emit("recommended_resolution_changed");
    }
    return result;
}

XrResult XRAPI_PTR traceEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    const uint64_t frame = ++g_frameCount;
    std::ostringstream signature;
    std::ostringstream fields;
    uint32_t projectionCount = 0;
    fields << "\"frame\":" << frame << ",\"views\":[";

    if (frameEndInfo && frameEndInfo->layers) {
        for (uint32_t layerIndex = 0; layerIndex < frameEndInfo->layerCount; ++layerIndex) {
            const XrCompositionLayerBaseHeader* base = frameEndInfo->layers[layerIndex];
            if (!base || base->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
            const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(base);
            for (uint32_t viewIndex = 0; viewIndex < projection->viewCount; ++viewIndex) {
                const XrSwapchainSubImage& sub = projection->views[viewIndex].subImage;
                if (projectionCount++) fields << ',';
                const uintptr_t handle = reinterpret_cast<uintptr_t>(sub.swapchain);
                fields << "{\"layer\":" << layerIndex
                       << ",\"view\":" << viewIndex
                       << ",\"swapchain\":\"0x" << std::hex << handle << std::dec << "\""
                       << ",\"x\":" << sub.imageRect.offset.x
                       << ",\"y\":" << sub.imageRect.offset.y
                       << ",\"width\":" << sub.imageRect.extent.width
                       << ",\"height\":" << sub.imageRect.extent.height
                       << ",\"arrayIndex\":" << sub.imageArrayIndex << '}';
                signature << handle << ':' << sub.imageRect.offset.x << ':' << sub.imageRect.offset.y
                          << ':' << sub.imageRect.extent.width << ':' << sub.imageRect.extent.height
                          << ':' << sub.imageArrayIndex << ';';
            }
        }
    }
    fields << ']';

    bool shouldLog = frame <= 5 || frame % 300 == 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (signature.str() != g_lastProjectionSignature) {
            g_lastProjectionSignature = signature.str();
            shouldLog = true;
        }
    }
    if (shouldLog) emit("end_frame", fields.str());
    return g_dispatch.endFrame(session, frameEndInfo);
}

XrResult XRAPI_PTR traceGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;
    if (std::strcmp(name, "xrGetInstanceProcAddr") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceGetInstanceProcAddr);
    else if (std::strcmp(name, "xrDestroyInstance") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceDestroyInstance);
    else if (std::strcmp(name, "xrEnumerateViewConfigurationViews") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceEnumerateViewConfigurationViews);
    else if (std::strcmp(name, "xrCreateSwapchain") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceCreateSwapchain);
    else if (std::strcmp(name, "xrDestroySwapchain") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceDestroySwapchain);
    else if (std::strcmp(name, "xrEndFrame") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceEndFrame);
    else if (std::strcmp(name, "xrPollEvent") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(tracePollEvent);
    else
        return g_dispatch.getInstanceProcAddr
            ? g_dispatch.getInstanceProcAddr(instance, name, function)
            : XR_ERROR_FUNCTION_UNSUPPORTED;
    return XR_SUCCESS;
}

XrResult XRAPI_PTR traceCreateApiLayerInstance(
    const XrInstanceCreateInfo* instanceCreateInfo,
    const XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance) {
    if (!apiLayerInfo || !apiLayerInfo->nextInfo) return XR_ERROR_INITIALIZATION_FAILED;
    XrApiLayerCreateInfo nextInfo = *apiLayerInfo;
    nextInfo.nextInfo = apiLayerInfo->nextInfo->next;
    const XrResult result = apiLayerInfo->nextInfo->nextCreateApiLayerInstance(
        instanceCreateInfo, &nextInfo, instance);
    if (XR_FAILED(result)) return result;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_dispatch.instance = *instance;
        g_dispatch.getInstanceProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;
        load("xrDestroyInstance", g_dispatch.destroyInstance);
        load("xrEnumerateViewConfigurationViews", g_dispatch.enumerateViews);
        load("xrCreateSwapchain", g_dispatch.createSwapchain);
        load("xrDestroySwapchain", g_dispatch.destroySwapchain);
        load("xrEndFrame", g_dispatch.endFrame);
        load("xrPollEvent", g_dispatch.pollEvent);
    }

    bool recommendedResolutionEnabled = false;
    if (instanceCreateInfo) {
        for (uint32_t i = 0; i < instanceCreateInfo->enabledExtensionCount; ++i) {
            if (std::strcmp(instanceCreateInfo->enabledExtensionNames[i],
                            XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME) == 0) {
                recommendedResolutionEnabled = true;
                break;
            }
        }
    }
    emit("layer_initialized",
         std::string("\"recommendedResolutionExtensionEnabled\":") +
             (recommendedResolutionEnabled ? "true" : "false"));
    return XR_SUCCESS;
}

}  // namespace

GXR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* request) {
    if (!loaderInfo || !layerName || !request || std::strcmp(layerName, kLayerName) != 0)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = traceGetInstanceProcAddr;
    request->createApiLayerInstance = traceCreateApiLayerInstance;
    return XR_SUCCESS;
}
