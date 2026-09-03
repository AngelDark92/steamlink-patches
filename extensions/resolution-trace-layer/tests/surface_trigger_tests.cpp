#include <ctime>
#include <cstdlib>
#include <iostream>
#include <string>

// Exercise the actual shipping source, with only platform/runtime calls replaced.
// No headset or Android runtime is contacted by these host tests.
static int testClockGetTime(int, timespec* value) { *value = {}; return 0; }
#define CLOCK_BOOTTIME 7
#define clock_gettime testClockGetTime
#ifdef _MSC_VER
// GCC symbol visibility has no meaning in this host-only executable.
#define __attribute__(...)
#endif
#include "../src/android_surface_trigger_passthrough_layer.cpp"
#undef clock_gettime
#ifdef _MSC_VER
#undef __attribute__
#endif

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "CHECK failed line " << __LINE__ << ": " #expression "\n"; \
    std::abort(); } } while (false)

namespace {
ANativeWindow testWindow;
JavaVM testVm;
std::array<uint8_t, 16> pixels{};
size_t logs{}, queued{}, released{}, destroyedSwapchains{}, destroyedSpaces{};
size_t createdSpaces{};
std::vector<std::string> logMessages;
XrResult pollResult = XR_EVENT_UNAVAILABLE;
XrEventDataSessionStateChanged nextEvent{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
XrResult endResult = XR_SUCCESS;
const XrFrameEndInfo* originalFrame{};
const SessionState* expectedSession{};
bool expectQuad{};
uint64_t submitCalls{};

template <typename T> T fakeHandle(uintptr_t value) { return reinterpret_cast<T>(value); }

XrResult XRAPI_PTR testCreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* session) {
    *session = fakeHandle<XrSession>(11);
    return XR_SUCCESS;
}
XrResult XRAPI_PTR testGetProperties(XrInstance, XrSystemId, XrSystemProperties* properties) {
    properties->graphicsProperties.maxLayerCount = kRequiredLayerCount;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR testCreateSpace(XrSession, const XrReferenceSpaceCreateInfo*, XrSpace* space) {
    ++createdSpaces;
    *space = fakeHandle<XrSpace>(12);
    return XR_SUCCESS;
}
XrResult XRAPI_PTR testCreateSurface(XrSession, const XrSwapchainCreateInfo* info,
                                    XrSwapchain* swapchain, jobject* surface) {
    CHECK(info->width == 2 && info->height == 2);
    CHECK(info->format == 0 && info->sampleCount == 0 && info->arraySize == 0);
    *swapchain = fakeHandle<XrSwapchain>(13);
    *surface = &testWindow;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR testDestroySwapchain(XrSwapchain) { ++destroyedSwapchains; return XR_SUCCESS; }
XrResult XRAPI_PTR testDestroySpace(XrSpace) { ++destroyedSpaces; return XR_SUCCESS; }
XrResult XRAPI_PTR testDestroySession(XrSession) { return XR_SUCCESS; }
XrResult XRAPI_PTR testDestroyInstance(XrInstance) { return XR_SUCCESS; }
XrResult XRAPI_PTR testPollEvent(XrInstance, XrEventDataBuffer* data) {
    if (pollResult == XR_SUCCESS && data) std::memcpy(data, &nextEvent, sizeof(nextEvent));
    // Deliberately leave old bytes on no-event/error returns. The wrapper must
    // ignore them, regardless of whether Valve happens to reinitialize the buffer.
    return pollResult;
}
XrResult XRAPI_PTR testEndFrame(XrSession, const XrFrameEndInfo* output) {
    ++submitCalls;
    CHECK(output);
    if (!expectQuad) {
        CHECK(output == originalFrame);
        return endResult;
    }
    CHECK(output->layerCount == kRequiredLayerCount);
    CHECK(output->type == originalFrame->type);
    CHECK(output->displayTime == originalFrame->displayTime);
    CHECK(output->environmentBlendMode == originalFrame->environmentBlendMode);
    CHECK(output->next == originalFrame->next);
#if GXR_AST_REPLACE_UNDERSIDE
    CHECK(output != originalFrame && output->layers != originalFrame->layers);
    CHECK(output->layerCount == 3);
    CHECK(output->layers[0] != originalFrame->layers[0]);
    CHECK(output->layers[1] == originalFrame->layers[1]);
    CHECK(output->layers[2] == originalFrame->layers[2]);
    const auto& original = *reinterpret_cast<const XrCompositionLayerProjection*>(
        originalFrame->layers[0]);
    const auto& replacement = *reinterpret_cast<const XrCompositionLayerProjection*>(
        output->layers[0]);
    CHECK(replacement.type == original.type && replacement.next == original.next);
    CHECK(replacement.layerFlags == original.layerFlags && replacement.space == original.space);
    CHECK(replacement.viewCount == 2 && replacement.views != original.views);
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const auto& view = replacement.views[eye];
        CHECK(view.type == original.views[eye].type && view.next == original.views[eye].next);
        CHECK(std::memcmp(&view.pose, &original.views[eye].pose, sizeof(XrPosef)) == 0);
        CHECK(std::memcmp(&view.fov, &original.views[eye].fov, sizeof(XrFovf)) == 0);
        CHECK(view.subImage.swapchain == expectedSession->surfaceSwapchain);
        CHECK(view.subImage.imageRect.offset.x == 0 && view.subImage.imageRect.offset.y == 0);
        CHECK(view.subImage.imageRect.extent.width == 2 && view.subImage.imageRect.extent.height == 2);
        CHECK(view.subImage.imageArrayIndex == 0);
    }
    for (uint32_t i = 0; i < output->layerCount; ++i) {
        CHECK(output->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION);
    }
#else
    for (uint32_t i = 0; i < kSourceProjectionCount; ++i) {
        CHECK(output->layers[i] == originalFrame->layers[i]);
    }
    CHECK(output->layers[kSourceProjectionCount] ==
          reinterpret_cast<const XrCompositionLayerBaseHeader*>(&expectedSession->triggerQuad));
    const auto& quad = expectedSession->triggerQuad;
    CHECK(quad.type == XR_TYPE_COMPOSITION_LAYER_QUAD);
    CHECK(quad.subImage.imageRect.extent.width == 2 && quad.subImage.imageRect.extent.height == 2);
    CHECK(quad.eyeVisibility == XR_EYE_VISIBILITY_BOTH);
    CHECK(quad.layerFlags == XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
    CHECK(quad.size.width == 0.001f && quad.size.height == 0.001f);
    CHECK(quad.pose.position.z == -1.0f);
#endif
    return endResult;
}

void initializeDispatch() {
    g = {};
    g.createSession = testCreateSession;
    g.getSystemProperties = testGetProperties;
    g.createReferenceSpace = testCreateSpace;
    g.createAndroidSurfaceSwapchain = testCreateSurface;
    g.destroySwapchain = testDestroySwapchain;
    g.destroySpace = testDestroySpace;
    g.destroySession = testDestroySession;
    g.destroyInstance = testDestroyInstance;
    g.pollEvent = testPollEvent;
    g.endFrame = testEndFrame;
    extensionEnabled = true;
    applicationVm = &testVm;
}
} // namespace

int __android_log_write(int, const char*, const char* message) {
    ++logs;
    logMessages.emplace_back(message);
    return 0;
}
void ANativeWindow_release(ANativeWindow*) { ++released; }
int ANativeWindow_setBuffersGeometry(ANativeWindow*, int32_t w, int32_t h, int32_t format) {
    CHECK(w == 2 && h == 2 && format == WINDOW_FORMAT_RGBA_8888);
    return 0;
}
int ANativeWindow_lock(ANativeWindow*, ANativeWindow_Buffer* buffer, void*) {
    *buffer = {2, 2, 2, WINDOW_FORMAT_RGBA_8888, pixels.data()};
    return 0;
}
int ANativeWindow_unlockAndPost(ANativeWindow*) { ++queued; return 0; }
ANativeWindow* ANativeWindow_fromSurface(JNIEnv*, jobject) { return &testWindow; }

int main() {
    initializeDispatch();
    XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};
    XrSession handle{};
    CHECK(layerCreateSession({}, &createInfo, &handle) == XR_SUCCESS);
    auto state = findSession(handle);
    CHECK(state && !state->triggerReady.load());
#if GXR_AST_REPLACE_UNDERSIDE
    CHECK(createdSpaces == 0 && state->viewSpace == XR_NULL_HANDLE);
    CHECK(state->triggerQuad.type == XR_TYPE_UNKNOWN);
    CHECK(std::string(kModeName) == "android_surface_underside_projection_v1");
    CHECK(std::string(kBuildId) == "android-surface-underside-5002322-v1.0-20260903");
#endif
    CHECK(queued == 0); // Must not write the Surface before VISIBLE/FOCUSED.
    nextEvent.session = handle;
    nextEvent.state = XR_SESSION_STATE_FOCUSED;
    XrEventDataBuffer eventBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    pollResult = XR_SUCCESS;
    CHECK(layerPollEvent({}, &eventBuffer) == XR_SUCCESS);
    CHECK(state->triggerReady.load() && queued == 1);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(pixels[4*i] == 0 && pixels[4*i+1] == 0 && pixels[4*i+2] == 0);
#if GXR_AST_REPLACE_UNDERSIDE
        CHECK(pixels[4*i+3] == 255);
#else
        CHECK(pixels[4*i+3] == 1);
#endif
    }

    const auto logsBeforeEmptyPoll = logs;
    pollResult = XR_EVENT_UNAVAILABLE;
    for (int i = 0; i < 1000; ++i) CHECK(layerPollEvent({}, &eventBuffer) == XR_EVENT_UNAVAILABLE);
    CHECK(logs == logsBeforeEmptyPoll && queued == 1);
    pollResult = XR_ERROR_RUNTIME_FAILURE;
    CHECK(layerPollEvent({}, &eventBuffer) == XR_ERROR_RUNTIME_FAILURE);
    CHECK(logs == logsBeforeEmptyPoll);
    pollResult = XR_SUCCESS;
    CHECK(layerPollEvent({}, nullptr) == XR_SUCCESS);
    CHECK(logs == logsBeforeEmptyPoll);

    std::array<std::array<XrCompositionLayerProjectionView, 2>, kSourceProjectionCount> views{};
    std::array<XrCompositionLayerProjection, kSourceProjectionCount> projections{};
    std::array<const XrCompositionLayerBaseHeader*, kSourceProjectionCount> pointers{};
    for (uint32_t i = 0; i < kSourceProjectionCount; ++i) {
        projections[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        projections[i].viewCount = 2;
        projections[i].views = views[i].data();
#if GXR_AST_REPLACE_UNDERSIDE
        projections[i].space = fakeHandle<XrSpace>(20 + i);
        projections[i].layerFlags = i == 2 ? 6 : 0;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            auto& view = views[i][eye];
            view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            view.pose.orientation = {0.01f * i, 0.02f * eye, -0.03f, 0.99f};
            view.pose.position = {0.03f * eye, 1.2f, -0.2f * i};
            view.fov = {-0.7f - eye * 0.1f, 0.6f, 0.8f, -0.9f};
            view.subImage = {fakeHandle<XrSwapchain>(30 + 2 * i + eye),
                {{static_cast<int32_t>(i), static_cast<int32_t>(eye)}, {3552, 3840}}, eye};
        }
#endif
        pointers[i] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projections[i]);
    }
#if GXR_AST_REPLACE_UNDERSIDE
    // The verified 5002322 renderer attaches this node even with no FB flags.
    XrCompositionLayerSettingsFB undersideSettings{XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB};
    projections[0].next = &undersideSettings;
#endif
    XrFrameEndInfo frame{XR_TYPE_FRAME_END_INFO};
    frame.layerCount = kSourceProjectionCount;
    frame.layers = pointers.data();
    frame.displayTime = 123456;
    frame.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    originalFrame = &frame;
    expectedSession = state.get();
    expectQuad = true;
#if GXR_AST_REPLACE_UNDERSIDE
    const auto originalViews = views;
    const auto originalProjections = projections;
    const auto originalPointers = pointers;
    const auto frameSnapshot = frame;
    const auto originalSettings = undersideSettings;
#endif
    for (int i = 0; i < 3; ++i) CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    const auto steadyLogs = logs;
    for (int i = 0; i < 1000; ++i) CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    CHECK(logs == steadyLogs && queued == 1 && state->appendedFrames == 1003);
#if GXR_AST_REPLACE_UNDERSIDE
    CHECK(std::memcmp(views.data(), originalViews.data(), sizeof(views)) == 0);
    CHECK(std::memcmp(projections.data(), originalProjections.data(), sizeof(projections)) == 0);
    CHECK(pointers == originalPointers);
    CHECK(std::memcmp(&frame, &frameSnapshot, sizeof(frame)) == 0);
    CHECK(std::memcmp(&undersideSettings, &originalSettings, sizeof(undersideSettings)) == 0);
    size_t frameLogs = 0, submissionLogs = 0;
    for (const auto& message : logMessages) {
        if (message.find("\"event\":\"surface_underside_frame\"") != std::string::npos) ++frameLogs;
        if (message.find("\"event\":\"surface_underside_submission\"") != std::string::npos) {
            ++submissionLogs;
            CHECK(message.find("\"replacementRGBA\":[0,0,0,255]") != std::string::npos);
            CHECK(message.find("\"baseFoveaPointersPreserved\":true") != std::string::npos);
            CHECK(message.find("\"originalPointersPreserved\":false") != std::string::npos);
            CHECK(message.find("\"outputViewPosesPreserved\":true") != std::string::npos);
            CHECK(message.find("\"outputViewFovsPreserved\":true") != std::string::npos);
            CHECK(message.find("\"sourceFrameContract\":{") != std::string::npos);
        }
        CHECK(message.find("\"event\":\"surface_trigger_submission\"") == std::string::npos);
    }
    CHECK(frameLogs == 3 && submissionLogs == 3);
#endif

    // The topology guard still rejects unexpected source layouts without rewriting.
    expectQuad = false;
    frame.layerCount = kSourceProjectionCount - 1;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    frame.layerCount = kSourceProjectionCount;
    projections[0].viewCount = 1;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    projections[0].viewCount = 2;
#if GXR_AST_REPLACE_UNDERSIDE
    frame.layerCount = kSourceProjectionCount + 1;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    frame.layerCount = kSourceProjectionCount;
    projections[0].next = &frame;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    projections[0].next = &undersideSettings;
    undersideSettings.layerFlags = 1;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    undersideSettings.layerFlags = 0;
    XrCompositionLayerSettingsFB extraSettings{XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB};
    undersideSettings.next = &extraSettings;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    undersideSettings.next = nullptr;
    // A missing settings chain also retains the same supported image contract.
    projections[0].next = nullptr;
    expectQuad = true;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    expectQuad = false;
    projections[0].next = &undersideSettings;
    for (uint32_t i = 0; i < 3; ++i) {
        projections[i].layerFlags ^= XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
        projections[i].layerFlags = originalProjections[i].layerFlags;
        projections[i].space = XR_NULL_HANDLE;
        CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
        projections[i].space = originalProjections[i].space;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            views[i][eye].next = &frame;
            CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
            views[i][eye].next = nullptr;
            views[i][eye].type = XR_TYPE_UNKNOWN;
            CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
            views[i][eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            views[i][eye].subImage.swapchain = XR_NULL_HANDLE;
            CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
            views[i][eye].subImage.swapchain = originalViews[i][eye].subImage.swapchain;
        }
    }
    state->maxLayerCount = 2;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    state->maxLayerCount = kRequiredLayerCount;
#endif
    state->state.store(XR_SESSION_STATE_SYNCHRONIZED);
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
    state->state.store(XR_SESSION_STATE_FOCUSED);

    // Runtime rejection is forwarded once; later frames remain Valve-only.
    expectQuad = true;
    endResult = XR_ERROR_LAYER_INVALID;
    const auto beforeFailure = submitCalls;
    CHECK(layerEndFrame(handle, &frame) == XR_ERROR_LAYER_INVALID);
    CHECK(submitCalls == beforeFailure + 1 && !state->triggerReady.load());
    expectQuad = false;
    endResult = XR_SUCCESS;
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);

#if GXR_AST_REPLACE_UNDERSIDE
    // A later visibility event cannot re-enable a failed submission or repost.
    pollResult = XR_SUCCESS;
    CHECK(layerPollEvent({}, &eventBuffer) == XR_SUCCESS);
    CHECK(!state->triggerReady.load() && queued == 1);
    CHECK(layerEndFrame(handle, &frame) == XR_SUCCESS);
#endif

    const auto oldAddress = state.get();
    CHECK(layerDestroySession(handle) == XR_SUCCESS);
    CHECK(released == 1 && destroyedSwapchains == 1);
    CHECK(destroyedSpaces == (GXR_AST_REPLACE_UNDERSIDE ? 0 : 1));
    CHECK(sessions.findForFrame(handle, renderSessionCache) == nullptr);
    // Keep 1 cold reader alive to prove a reused numeric handle finds a new object.
    CHECK(layerCreateSession({}, &createInfo, &handle) == XR_SUCCESS);
    CHECK(sessions.findForFrame(handle, renderSessionCache) != oldAddress);
    CHECK(!state->triggerReady.load());
    state.reset();
    CHECK(layerDestroyInstance({}) == XR_SUCCESS);
    CHECK(released == 2 && destroyedSwapchains == 2);
    CHECK(destroyedSpaces == (GXR_AST_REPLACE_UNDERSIDE ? 0 : 2));
    CHECK(sessions.findForFrame(handle, renderSessionCache) == nullptr);
    initializeDispatch();
    CHECK(layerCreateSession({}, &createInfo, &handle) == XR_SUCCESS);
    CHECK(sessions.findForFrame(handle, renderSessionCache) != nullptr);
    CHECK(layerDestroyInstance({}) == XR_SUCCESS);
    CHECK(released == 3 && destroyedSwapchains == 3);
    CHECK(destroyedSpaces == (GXR_AST_REPLACE_UNDERSIDE ? 0 : 3));
    std::cout << kModeName << ": " << kSourceProjectionCount << " projections PASS\n";
}
