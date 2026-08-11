#include <android/log.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#ifdef GXR_COMPOSITOR_QUAD_PROBE
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr_platform.h>
#endif

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <time.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

#ifdef GXR_COMPOSITOR_QUAD_PROBE
constexpr char kLayerName[] = "XR_APILAYER_local_GalaxyXR_compositor_probe";
#else
constexpr char kLayerName[] = "XR_APILAYER_local_GalaxyXR_resolution_trace";
#endif
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
#ifdef GXR_COMPOSITOR_QUAD_PROBE
    PFN_xrGetSystemProperties getSystemProperties{nullptr};
    PFN_xrCreateSession createSession{nullptr};
    PFN_xrDestroySession destroySession{nullptr};
    PFN_xrCreateReferenceSpace createReferenceSpace{nullptr};
    PFN_xrDestroySpace destroySpace{nullptr};
    PFN_xrEnumerateSwapchainFormats enumerateSwapchainFormats{nullptr};
    PFN_xrEnumerateSwapchainImages enumerateSwapchainImages{nullptr};
    PFN_xrAcquireSwapchainImage acquireSwapchainImage{nullptr};
    PFN_xrWaitSwapchainImage waitSwapchainImage{nullptr};
    PFN_xrReleaseSwapchainImage releaseSwapchainImage{nullptr};
#endif
};

Dispatch g_dispatch;
std::mutex g_mutex;
std::unordered_map<XrSwapchain, XrSwapchainCreateInfo> g_swapchains;
std::atomic<uint64_t> g_frameCount{0};
std::string g_lastProjectionSignature;

#ifdef GXR_COMPOSITOR_QUAD_PROBE
XrSession g_quadSession{XR_NULL_HANDLE};
XrSpace g_quadSpace{XR_NULL_HANDLE};
XrSwapchain g_quadSwapchain{XR_NULL_HANDLE};
uint32_t g_maxLayerCount{0};
bool g_quadInitializationAttempted{false};
bool g_quadReady{false};
bool g_quadLimitLogged{false};
bool g_quadImageAcquired{false};
bool g_quadWaitDeferredLogged{false};
GLuint g_quadTexture{0};
#endif

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

#ifdef GXR_COMPOSITOR_QUAD_PROBE
void resetQuadState() {
    g_quadSession = XR_NULL_HANDLE;
    g_quadSpace = XR_NULL_HANDLE;
    g_quadSwapchain = XR_NULL_HANDLE;
    g_quadInitializationAttempted = false;
    g_quadReady = false;
    g_quadLimitLogged = false;
    g_quadImageAcquired = false;
    g_quadWaitDeferredLogged = false;
    g_quadTexture = 0;
}

void destroyQuadResources() {
    if (g_quadSwapchain != XR_NULL_HANDLE && g_dispatch.destroySwapchain) {
        g_dispatch.destroySwapchain(g_quadSwapchain);
    }
    if (g_quadSpace != XR_NULL_HANDLE && g_dispatch.destroySpace) {
        g_dispatch.destroySpace(g_quadSpace);
    }
    resetQuadState();
}

bool finishQuadImage() {
    if (!g_quadImageAcquired || g_quadTexture == 0) return false;

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = 0;
    const XrResult waitResult = g_dispatch.waitSwapchainImage(g_quadSwapchain, &waitInfo);
    if (waitResult == XR_TIMEOUT_EXPIRED) {
        if (!g_quadWaitDeferredLogged) {
            emit("quad_initialization_deferred", "\"reason\":\"image_not_ready\"");
            g_quadWaitDeferredLogged = true;
        }
        return false;
    }
    if (XR_FAILED(waitResult)) {
        emit("quad_initialization_failed", "\"reason\":\"wait_image\",\"result\":" + std::to_string(waitResult));
        destroyQuadResources();
        return false;
    }

    GLint previousFramebuffer = 0;
    GLfloat previousClearColor[4]{};
    GLboolean previousColorMask[4]{};
    const GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_quadTexture, 0);
    const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (framebufferStatus == GL_FRAMEBUFFER_COMPLETE) {
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glClearColor(previousClearColor[0], previousClearColor[1],
                 previousClearColor[2], previousClearColor[3]);
    glColorMask(previousColorMask[0], previousColorMask[1],
                previousColorMask[2], previousColorMask[3]);
    if (previousScissor) glEnable(GL_SCISSOR_TEST);
    glDeleteFramebuffers(1, &framebuffer);

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult releaseResult =
        g_dispatch.releaseSwapchainImage(g_quadSwapchain, &releaseInfo);
    g_quadImageAcquired = false;
    if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE || XR_FAILED(releaseResult)) {
        emit("quad_initialization_failed", "\"reason\":\"render_image\"");
        destroyQuadResources();
        return false;
    }

    g_quadReady = true;
    emit("quad_ready", "\"width\":16,\"height\":16");
    return true;
}

bool initializeQuad(XrSession session) {
    if (g_quadReady) return true;
    if (session == XR_NULL_HANDLE || session != g_quadSession) return false;
    if (g_quadInitializationAttempted) return finishQuadImage();
    g_quadInitializationAttempted = true;

    if (!g_dispatch.createReferenceSpace || !g_dispatch.enumerateSwapchainFormats ||
        !g_dispatch.createSwapchain || !g_dispatch.enumerateSwapchainImages ||
        !g_dispatch.acquireSwapchainImage || !g_dispatch.waitSwapchainImage ||
        !g_dispatch.releaseSwapchainImage) {
        emit("quad_initialization_failed", "\"reason\":\"missing_dispatch\"");
        return false;
    }

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    XrResult result = g_dispatch.createReferenceSpace(session, &spaceInfo, &g_quadSpace);
    if (XR_FAILED(result)) {
        emit("quad_initialization_failed", "\"reason\":\"create_space\",\"result\":" + std::to_string(result));
        return false;
    }

    uint32_t formatCount = 0;
    result = g_dispatch.enumerateSwapchainFormats(session, 0, &formatCount, nullptr);
    if (XR_FAILED(result) || formatCount == 0) {
        emit("quad_initialization_failed", "\"reason\":\"enumerate_formats\",\"result\":" + std::to_string(result));
        destroyQuadResources();
        return false;
    }
    std::vector<int64_t> formats(formatCount);
    result = g_dispatch.enumerateSwapchainFormats(
        session, formatCount, &formatCount, formats.data());
    if (XR_FAILED(result)) {
        emit("quad_initialization_failed", "\"reason\":\"read_formats\",\"result\":" + std::to_string(result));
        destroyQuadResources();
        return false;
    }
    int64_t selectedFormat = 0;
    for (const int64_t candidate : formats) {
        if (candidate == GL_SRGB8_ALPHA8) {
            selectedFormat = candidate;
            break;
        }
    }
    if (selectedFormat == 0) {
        for (const int64_t candidate : formats) {
            if (candidate == GL_RGBA8) {
                selectedFormat = candidate;
                break;
            }
        }
    }
    if (selectedFormat == 0) {
        emit("quad_initialization_failed", "\"reason\":\"rgba_format_unavailable\"");
        destroyQuadResources();
        return false;
    }

    XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                               XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainInfo.format = selectedFormat;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = 16;
    swapchainInfo.height = 16;
    swapchainInfo.faceCount = 1;
    swapchainInfo.arraySize = 1;
    swapchainInfo.mipCount = 1;
    result = g_dispatch.createSwapchain(session, &swapchainInfo, &g_quadSwapchain);
    if (XR_FAILED(result)) {
        emit("quad_initialization_failed", "\"reason\":\"create_swapchain\",\"result\":" + std::to_string(result));
        destroyQuadResources();
        return false;
    }

    uint32_t imageCount = 0;
    result = g_dispatch.enumerateSwapchainImages(g_quadSwapchain, 0, &imageCount, nullptr);
    if (XR_FAILED(result) || imageCount == 0) {
        emit("quad_initialization_failed", "\"reason\":\"enumerate_images\",\"result\":" + std::to_string(result));
        destroyQuadResources();
        return false;
    }
    std::vector<XrSwapchainImageOpenGLESKHR> images(
        imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    result = g_dispatch.enumerateSwapchainImages(
        g_quadSwapchain, imageCount, &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    if (XR_FAILED(result)) {
        emit("quad_initialization_failed", "\"reason\":\"read_images\",\"result\":" + std::to_string(result));
        destroyQuadResources();
        return false;
    }

    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    result = g_dispatch.acquireSwapchainImage(g_quadSwapchain, &acquireInfo, &imageIndex);
    if (XR_FAILED(result)) {
        emit("quad_initialization_failed", "\"reason\":\"acquire_image\",\"result\":" + std::to_string(result));
        destroyQuadResources();
        return false;
    }
    g_quadTexture = images[imageIndex].image;
    g_quadImageAcquired = true;
    return finishQuadImage();
}
#endif

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
#ifdef GXR_COMPOSITOR_QUAD_PROBE
        destroyQuadResources();
#endif
        g_swapchains.clear();
        g_lastProjectionSignature.clear();
    }
    const XrResult result = next ? next(instance) : XR_ERROR_FUNCTION_UNSUPPORTED;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dispatch = {};
    return result;
}

#ifdef GXR_COMPOSITOR_QUAD_PROBE
XrResult XRAPI_PTR traceGetSystemProperties(
    XrInstance instance, XrSystemId systemId, XrSystemProperties* properties) {
    const XrResult result = g_dispatch.getSystemProperties(instance, systemId, properties);
    if (XR_SUCCEEDED(result) && properties) {
        g_maxLayerCount = properties->graphicsProperties.maxLayerCount;
        emit("system_properties", "\"maxLayerCount\":" + std::to_string(g_maxLayerCount));
    }
    return result;
}

XrResult XRAPI_PTR traceCreateSession(
    XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
    const XrResult result = g_dispatch.createSession(instance, createInfo, session);
    if (XR_SUCCEEDED(result) && session) {
        destroyQuadResources();
        g_quadSession = *session;
        emit("quad_session_created");
    }
    return result;
}

XrResult XRAPI_PTR traceDestroySession(XrSession session) {
    if (session == g_quadSession) {
        destroyQuadResources();
        emit("quad_session_destroyed");
    }
    return g_dispatch.destroySession(session);
}
#endif

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

#ifdef GXR_COMPOSITOR_QUAD_PROBE
    if (frameEndInfo && frameEndInfo->layers && projectionCount > 0 && initializeQuad(session)) {
        if (g_maxLayerCount != 0 && frameEndInfo->layerCount >= g_maxLayerCount) {
            if (!g_quadLimitLogged) {
                emit("quad_skipped", "\"reason\":\"max_layer_count\"");
                g_quadLimitLogged = true;
            }
        } else {
            XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
            quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quad.space = g_quadSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = g_quadSwapchain;
            quad.subImage.imageRect.offset = {0, 0};
            quad.subImage.imageRect.extent = {16, 16};
            quad.subImage.imageArrayIndex = 0;
            quad.pose.orientation.w = 1.0f;
            quad.pose.position = {0.25f, -0.20f, -1.0f};
            quad.size = {0.04f, 0.04f};

            std::vector<const XrCompositionLayerBaseHeader*> layers;
            layers.reserve(frameEndInfo->layerCount + 1);
            for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) {
                layers.push_back(frameEndInfo->layers[i]);
            }
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad));
            XrFrameEndInfo modified = *frameEndInfo;
            modified.layerCount = static_cast<uint32_t>(layers.size());
            modified.layers = layers.data();
            if (frame <= 5 || frame % 300 == 0) {
                emit("quad_appended", "\"frame\":" + std::to_string(frame));
            }
            return g_dispatch.endFrame(session, &modified);
        }
    }
#endif
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
#ifdef GXR_COMPOSITOR_QUAD_PROBE
    else if (std::strcmp(name, "xrGetSystemProperties") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceGetSystemProperties);
    else if (std::strcmp(name, "xrCreateSession") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceCreateSession);
    else if (std::strcmp(name, "xrDestroySession") == 0)
        *function = reinterpret_cast<PFN_xrVoidFunction>(traceDestroySession);
#endif
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
#ifdef GXR_COMPOSITOR_QUAD_PROBE
        load("xrGetSystemProperties", g_dispatch.getSystemProperties);
        load("xrCreateSession", g_dispatch.createSession);
        load("xrDestroySession", g_dispatch.destroySession);
        load("xrCreateReferenceSpace", g_dispatch.createReferenceSpace);
        load("xrDestroySpace", g_dispatch.destroySpace);
        load("xrEnumerateSwapchainFormats", g_dispatch.enumerateSwapchainFormats);
        load("xrEnumerateSwapchainImages", g_dispatch.enumerateSwapchainImages);
        load("xrAcquireSwapchainImage", g_dispatch.acquireSwapchainImage);
        load("xrWaitSwapchainImage", g_dispatch.waitSwapchainImage);
        load("xrReleaseSwapchainImage", g_dispatch.releaseSwapchainImage);
#endif
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
             (recommendedResolutionEnabled ? "true" : "false") +
#ifdef GXR_COMPOSITOR_QUAD_PROBE
             ",\"quadProbeEnabled\":true");
#else
             ",\"quadProbeEnabled\":false");
#endif
    return XR_SUCCESS;
}

}  // namespace

GXR_EXPORT jboolean JNICALL
Java_com_valvesoftware_steamlink_GxrResolutionProbe_hasOpenXrFrameNative(
    JNIEnv*, jclass) {
    return g_frameCount.load() > 0 ? JNI_TRUE : JNI_FALSE;
}

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
