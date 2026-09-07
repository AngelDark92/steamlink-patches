// Experimental: replace the terminal foveal projection's image source with a real
// Android Surface. This is deliberately separate from the shipping 2x2 quad fix.
#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "session_registry.h"

namespace {
constexpr char kLayerName[] = "XR_APILAYER_local_GalaxyXR_android_surface_fovea_v1";
constexpr char kBuildId[] = "android-surface-fovea-v1-20260907";
struct Dispatch {
    XrInstance instance{};
    PFN_xrGetInstanceProcAddr get{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrCreateSession createSession{};
    PFN_xrDestroySession destroySession{};
    PFN_xrCreateSwapchain createSwapchain{};
    PFN_xrDestroySwapchain destroySwapchain{};
    PFN_xrEnumerateSwapchainImages enumerateImages{};
    PFN_xrAcquireSwapchainImage acquire{};
    PFN_xrWaitSwapchainImage wait{};
    PFN_xrReleaseSwapchainImage release{};
    PFN_xrCreateSwapchainAndroidSurfaceKHR createSurface{};
    PFN_xrEndFrame endFrame{};
    PFN_xrPollEvent poll{};
} g;
JavaVM* vm{};
bool enabled{};
void log(const char* event, const std::string& fields = {}) {
    const auto text = std::string("{\"schema\":1,\"mode\":\"android_surface_fovea_v1\",\"buildId\":\"") +
        kBuildId + "\",\"event\":\"" + event + "\"" + (fields.empty() ? "" : "," + fields) + "}";
    __android_log_write(ANDROID_LOG_INFO, "GXRFoveaSurface", text.c_str());
}
struct ImageState {
    XrSwapchainCreateInfo info{};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    struct Acquired { uint32_t index; bool waited; };
    std::deque<Acquired> acquired;
    GLuint snapshot{};
    uint64_t serial{}, submitted{};
    bool valid{};
};
struct Session {
    std::mutex mutex;
    XrSession handle{};
    std::atomic<XrSessionState> state{XR_SESSION_STATE_UNKNOWN};
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext context{EGL_NO_CONTEXT}, cleanupContext{EGL_NO_CONTEXT};
    EGLConfig config{};
    EGLSurface cleanupSurface{EGL_NO_SURFACE}, surface{EGL_NO_SURFACE};
    XrSwapchain output{};
    ANativeWindow* window{};
    uint32_t width{}, height{};
    int64_t format{};
    std::array<XrSwapchainSubImage, 2> expected{};
    bool learned{}, disabled{};
    uint64_t frames{};
    std::map<XrSwapchain, ImageState> sources;
};
gxr::SessionRegistry<XrSession, Session> sessions;
thread_local decltype(sessions)::RenderCache cache;
std::mutex ownersMutex;
std::map<XrSwapchain, XrSession> owners;
std::shared_ptr<Session> owner(XrSwapchain chain) {
    std::lock_guard<std::mutex> lock(ownersMutex);
    const auto it = owners.find(chain);
    return it == owners.end() ? nullptr : sessions.find(it->second);
}
bool visible(const Session& s) {
    const auto state = s.state.load();
    return state == XR_SESSION_STATE_VISIBLE || state == XR_SESSION_STATE_FOCUSED;
}
void fail(Session& s, const char* reason) {
    if (!s.disabled) log("fallback", std::string("\"reason\":\"") + reason + "\",\"originalLayersPreserved\":true");
    s.disabled = true;
}
struct EglCurrent {
    EGLDisplay display{eglGetCurrentDisplay()};
    EGLContext context{eglGetCurrentContext()};
    EGLSurface draw{eglGetCurrentSurface(EGL_DRAW)}, read{eglGetCurrentSurface(EGL_READ)};
    bool restore(EGLDisplay changedDisplay) const {
        return eglMakeCurrent(display == EGL_NO_DISPLAY ? changedDisplay : display, draw, read, context) == EGL_TRUE;
    }
};
// Blits touch only framebuffer bindings and scissor state; no shaders, textures,
// viewport, blending, or color mask are changed. Texture allocation restores binding.
struct BlitState {
    GLint read{}, draw{};
    GLboolean scissor{glIsEnabled(GL_SCISSOR_TEST)};
    GLuint fb[2]{};
    BlitState() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGenFramebuffers(2, fb);
        glDisable(GL_SCISSOR_TEST);
    }
    ~BlitState() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        glDeleteFramebuffers(2, fb);
    }
};
bool current(const Session& s) {
    return eglGetCurrentDisplay() == s.display && eglGetCurrentContext() == s.context;
}
bool glSucceeded(const char* stage) {
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) return true;
    // Experimental validation consumes GL error flags. Check before our work too:
    // inherited application errors disable this experiment rather than permitting
    // ambiguous success or submitting uninitialized/stale pixels.
    log("gl_error", std::string("\"stage\":\"") + stage + "\",\"error\":" + std::to_string(error));
    return false;
}
bool initializeCleanup(Session& s) {
    if (s.cleanupContext != EGL_NO_CONTEXT) return true;
    const EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLint pbuffer[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    s.cleanupContext = eglCreateContext(s.display, s.config, s.context, attributes);
    s.cleanupSurface = eglCreatePbufferSurface(s.display, s.config, pbuffer);
    return s.cleanupContext != EGL_NO_CONTEXT && s.cleanupSurface != EGL_NO_SURFACE;
}
void destroyOutput(Session& s) {
    if (s.surface != EGL_NO_SURFACE) eglDestroySurface(s.display, s.surface);
    s.surface = EGL_NO_SURFACE;
    if (s.window) ANativeWindow_release(s.window);
    s.window = nullptr;
    if (s.output) g.destroySwapchain(s.output);
    s.output = XR_NULL_HANDLE;
}
void cleanup(Session& s) {
    EglCurrent saved;
    if (s.cleanupContext != EGL_NO_CONTEXT && s.cleanupSurface != EGL_NO_SURFACE &&
        eglMakeCurrent(s.display, s.cleanupSurface, s.cleanupSurface, s.cleanupContext)) {
        for (auto& pair : s.sources) if (pair.second.snapshot) glDeleteTextures(1, &pair.second.snapshot);
        saved.restore(s.display);
    }
    destroyOutput(s);
    if (s.cleanupSurface != EGL_NO_SURFACE) eglDestroySurface(s.display, s.cleanupSurface);
    if (s.cleanupContext != EGL_NO_CONTEXT) eglDestroyContext(s.display, s.cleanupContext);
    s.sources.clear();
}
bool supported(const ImageState& image) {
    const auto& c = image.info;
    return (c.format == GL_RGBA8 || c.format == GL_SRGB8_ALPHA8) && c.sampleCount == 1 &&
        c.faceCount == 1 && c.mipCount == 1 && c.arraySize > 0 && c.arraySize <= 2 &&
        !(c.createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT);
}
bool snapshot(Session& s, ImageState& image, uint32_t index) {
    image.valid = false;
    if (!current(s) || index >= image.images.size() || !supported(image)) return false;
    if (!glSucceeded("before_snapshot")) return false;
    if (!initializeCleanup(s)) return false;
    const auto& c = image.info;
    if (!image.snapshot) {
        GLint binding{};
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &binding);
        glGenTextures(1, &image.snapshot);
        glBindTexture(GL_TEXTURE_2D_ARRAY, image.snapshot);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, static_cast<GLenum>(c.format), c.width, c.height, c.arraySize);
        glBindTexture(GL_TEXTURE_2D_ARRAY, binding);
        if (!glSucceeded("allocate_snapshot")) return false;
    }
    BlitState saved;
    for (uint32_t layer = 0; layer < c.arraySize; ++layer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, saved.fb[0]);
        if (c.arraySize == 1) glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, image.images[index].image, 0);
        else glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, image.images[index].image, 0, layer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved.fb[1]);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, image.snapshot, 0, layer);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
            glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        glBlitFramebuffer(0, 0, c.width, c.height, 0, 0, c.width, c.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (!glSucceeded("snapshot_blit")) return false;
    }
    // GL commands are ordered in Valve's context. Issue them before forwarding the
    // real release; never sample an OpenXR image once ownership passed to runtime.
    image.valid = true;
    return true;
}
bool obtainWindow(jobject surface, ANativeWindow*& window) {
    if (!vm || !surface) return false;
    JNIEnv* env{};
    const auto status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    const bool attach = status == JNI_EDETACHED;
    if (attach && vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
    if (!env) return false;
    window = ANativeWindow_fromSurface(env, surface);
    env->DeleteLocalRef(surface);
    if (attach) vm->DetachCurrentThread();
    return window != nullptr;
}
bool createOutput(Session& s, uint32_t width, uint32_t height, int64_t format) {
    if (s.output && s.width == width && s.height == height && s.format == format) return true;
    destroyOutput(s);
    // The producer EGL config, not a requested OpenXR format alone, determines
    // the actual Android buffer precision. Require all4 channels and no MSAA.
    for (EGLint attribute : {EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE}) {
        EGLint bits{};
        if (!eglGetConfigAttrib(s.display, s.config, attribute, &bits) || bits != 8) return false;
    }
    EGLint samples{};
    GLint maximum{};
    if (!eglGetConfigAttrib(s.display, s.config, EGL_SAMPLES, &samples) || samples != 0) return false;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximum);
    if (maximum <= 0 || width > static_cast<uint32_t>(maximum) || height > static_cast<uint32_t>(maximum)) return false;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    // KHR_android_surface_swapchain requires format/sampleCount/faceCount/
    // arraySize/mipCount to remain0: the Android producer config defines them.
    info.width = width;
    info.height = height;
    jobject surface{};
    const auto result = g.createSurface(s.handle, &info, &s.output, &surface);
    if (XR_FAILED(result) || !obtainWindow(surface, s.window)) return false;
    // Require an explicit matching EGL colorspace. Reject unsupported sRGB window
    // surfaces instead of silently changing the projection's transfer function.
    const EGLint attrs[] = {EGL_GL_COLORSPACE_KHR,
        format == GL_SRGB8_ALPHA8 ? EGL_GL_COLORSPACE_SRGB_KHR : EGL_GL_COLORSPACE_LINEAR_KHR, EGL_NONE};
    s.surface = eglCreateWindowSurface(s.display, s.config, s.window, attrs);
    if (s.surface == EGL_NO_SURFACE) return false;
    EGLint actualWidth{}, actualHeight{}, colorspace{};
    if (!eglQuerySurface(s.display, s.surface, EGL_WIDTH, &actualWidth) ||
        !eglQuerySurface(s.display, s.surface, EGL_HEIGHT, &actualHeight) ||
        !eglQuerySurface(s.display, s.surface, EGL_GL_COLORSPACE_KHR, &colorspace) ||
        actualWidth != static_cast<EGLint>(width) || actualHeight != static_cast<EGLint>(height) ||
        colorspace != attrs[1]) return false;
    s.width = width; s.height = height; s.format = format;
    log("surface_created", "\"width\":" + std::to_string(width) + ",\"height\":" +
        std::to_string(height) + ",\"format\":" + std::to_string(format) +
        ",\"bitDepth\":8,\"eglRgbaBits\":[8,8,8,8],\"eglColorspace\":" + std::to_string(colorspace));
    return true;
}
bool sameSubImage(const XrSwapchainSubImage& a, const XrSwapchainSubImage& b) {
    return a.swapchain == b.swapchain && a.imageArrayIndex == b.imageArrayIndex &&
        a.imageRect.offset.x == b.imageRect.offset.x && a.imageRect.offset.y == b.imageRect.offset.y &&
        a.imageRect.extent.width == b.imageRect.extent.width && a.imageRect.extent.height == b.imageRect.extent.height;
}
bool validRect(const XrSwapchainSubImage& sub, const ImageState& source) {
    const auto& r = sub.imageRect;
    return r.offset.x >= 0 && r.offset.y >= 0 && r.extent.width > 0 && r.extent.height > 0 &&
        static_cast<uint64_t>(r.offset.x) + r.extent.width <= source.info.width &&
        static_cast<uint64_t>(r.offset.y) + r.extent.height <= source.info.height &&
        sub.imageArrayIndex < source.info.arraySize;
}
XrResult XRAPI_PTR endFrame(XrSession handle, const XrFrameEndInfo* info) {
    auto* s = sessions.findForFrame(handle, cache);
    if (!s || s->disabled || !enabled || !visible(*s) || !info || info->layerCount != 3 || !info->layers)
        return g.endFrame(handle, info);
    std::lock_guard<std::mutex> lock(s->mutex);
    const XrCompositionLayerProjection* fovea{};
    for (uint32_t i = 0; i < 3; ++i) {
        if (!info->layers[i] || info->layers[i]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) return g.endFrame(handle, info);
        const auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[i]);
        if (p->viewCount != 2 || !p->views) return g.endFrame(handle, info);
        if (i < 2 && p->layerFlags != 0) return g.endFrame(handle, info);
        if (i == 2) fovea = p;
    }
    // Exact 5002322 topology: terminal blend flags6 identify the narrow fovea.
    if (fovea->next || fovea->layerFlags != (XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
        XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT)) return g.endFrame(handle, info);
    bool changed = !s->learned;
    std::array<ImageState*, 2> source{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const auto& sub = fovea->views[eye].subImage;
        const auto it = s->sources.find(sub.swapchain);
        if (it == s->sources.end() || !supported(it->second) || !validRect(sub, it->second) ||
            fovea->views[eye].next) {
            fail(*s, "unsupported_source_format_geometry_or_view_extension_RGB10_not_supported");
            return g.endFrame(handle, info);
        }
        source[eye] = &it->second;
        changed |= !sameSubImage(s->expected[eye], sub);
    }
    if (changed) {
        for (uint32_t eye = 0; eye < 2; ++eye) {
            s->expected[eye] = fovea->views[eye].subImage;
            source[eye]->valid = false;
        }
        s->learned = true;
        log("fovea_contract_learned", "\"projectionIndex\":2,\"sourceFormat\":" +
            std::to_string(source[0]->info.format) + ",\"sourceWidth\":" +
            std::to_string(source[0]->info.width) + ",\"sourceHeight\":" + std::to_string(source[0]->info.height));
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const auto& sub = s->expected[eye];
            log("fovea_eye_contract", "\"eye\":" + std::to_string(eye) +
                ",\"arrayIndex\":" + std::to_string(sub.imageArrayIndex) +
                ",\"sourceFormat\":" + std::to_string(source[eye]->info.format) +
                ",\"x\":" + std::to_string(sub.imageRect.offset.x) + ",\"y\":" + std::to_string(sub.imageRect.offset.y) +
                ",\"width\":" + std::to_string(sub.imageRect.extent.width) +
                ",\"height\":" + std::to_string(sub.imageRect.extent.height));
        }
        return g.endFrame(handle, info);
    }
    for (auto* image : source) if (!image->valid) return g.endFrame(handle, info);
    const auto& left = s->expected[0].imageRect;
    const auto& right = s->expected[1].imageRect;
    if (left.extent.height != right.extent.height || source[0]->info.format != source[1]->info.format || !current(*s)) {
        fail(*s, "incompatible_eye_contract_or_context"); return g.endFrame(handle, info);
    }
    const uint32_t width = static_cast<uint32_t>(left.extent.width) + right.extent.width;
    if (!createOutput(*s, width, left.extent.height, source[0]->info.format)) {
        fail(*s, "android_surface_or_matching_colorspace_rejected"); return g.endFrame(handle, info);
    }
    // A released image can legally be reused on several xrEndFrame calls. Keep
    // the Android Surface in the projection on these frames too: omitting it just
    // because Valve did not render new pixels would undo the compositor trigger.
    const bool redraw = source[0]->serial != source[0]->submitted || source[1]->serial != source[1]->submitted;
    if (redraw) {
    EglCurrent previous;
    if (!glSucceeded("before_surface_blit")) {
        fail(*s, "inherited_GL_error"); return g.endFrame(handle, info);
    }
    if (!eglMakeCurrent(s->display, s->surface, s->surface, s->context)) {
        fail(*s, "egl_make_current_failed"); return g.endFrame(handle, info);
    }
    bool complete = true;
    {
        BlitState saved;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            const auto& sub = s->expected[eye];
            const auto& r = sub.imageRect;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, saved.fb[0]);
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, source[eye]->snapshot, 0, sub.imageArrayIndex);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { complete = false; break; }
            const GLint x = eye == 0 ? 0 : left.extent.width;
            glBlitFramebuffer(r.offset.x, r.offset.y, r.offset.x + r.extent.width, r.offset.y + r.extent.height,
                x, 0, x + r.extent.width, r.extent.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            if (!glSucceeded("surface_blit")) { complete = false; break; }
        }
    }
    if (complete) complete = eglSwapBuffers(s->display, s->surface) == EGL_TRUE;
    const bool restored = previous.restore(s->display);
    if (!complete || !restored) {
        fail(*s, "surface_blit_swap_or_restore_failed"); return g.endFrame(handle, info);
    }
    }
    std::array<XrCompositionLayerProjectionView, 2> views{fovea->views[0], fovea->views[1]};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        views[eye].subImage.swapchain = s->output;
        views[eye].subImage.imageArrayIndex = 0;
        views[eye].subImage.imageRect.offset = {eye == 0 ? 0 : left.extent.width, 0};
        source[eye]->submitted = source[eye]->serial;
    }
    auto projection = *fovea;
    projection.views = views.data();
    std::array<const XrCompositionLayerBaseHeader*, 3> layers{info->layers[0], info->layers[1],
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection)};
    auto replaced = *info; replaced.layers = layers.data();
    const auto result = g.endFrame(handle, &replaced);
    if (XR_FAILED(result)) fail(*s, "runtime_rejected_surface_projection_next_frames_passthrough");
    else if (s->frames++ < 3) log("fovea_surface_submitted", std::string("\"layerCount\":3,\"dummyQuad\":false,\"copyPasses\":") +
        (redraw ? "2" : "0") + ",\"bitDepth\":8,\"surfaceRequeued\":" + (redraw ? "true" : "false") +
        ",\"leftSourceReleaseSerial\":" + std::to_string(source[0]->serial) +
        ",\"rightSourceReleaseSerial\":" + std::to_string(source[1]->serial) +
        ",\"outputWidth\":" + std::to_string(s->width) + ",\"outputHeight\":" + std::to_string(s->height));
    return result;
}
XrResult XRAPI_PTR createSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
    const auto result = g.createSession(instance, info, session);
    if (XR_FAILED(result)) return result;
    auto s = std::make_shared<Session>(); s->handle = *session;
    for (auto* next = static_cast<const XrBaseInStructure*>(info->next); next; next = next->next) {
        if (next->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR) {
            const auto* binding = reinterpret_cast<const XrGraphicsBindingOpenGLESAndroidKHR*>(next);
            s->display = binding->display; s->context = binding->context; s->config = binding->config;
        }
    }
    if (s->context == EGL_NO_CONTEXT) fail(*s, "requires_OpenGL_ES");
    sessions.insert(*session, std::move(s)); return result;
}
XrResult XRAPI_PTR destroySession(XrSession session) {
    auto s = sessions.erase(session);
    if (s) { std::lock_guard<std::mutex> lock(s->mutex); cleanup(*s); }
    { std::lock_guard<std::mutex> lock(ownersMutex);
      for (auto it = owners.begin(); it != owners.end();) if (it->second == session) it = owners.erase(it); else ++it; }
    return g.destroySession(session);
}
XrResult XRAPI_PTR createSwapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* chain) {
    const auto result = g.createSwapchain(session, info, chain);
    auto s = sessions.find(session);
    if (XR_SUCCEEDED(result) && s && !s->disabled) {
        std::lock_guard<std::mutex> lock(s->mutex);
        auto& image = s->sources[*chain]; image.info = *info; image.info.next = nullptr;
        uint32_t count{};
        if (XR_SUCCEEDED(g.enumerateImages(*chain, 0, &count, nullptr))) {
            image.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            if (XR_FAILED(g.enumerateImages(*chain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(image.images.data())))) image.images.clear();
        }
        std::lock_guard<std::mutex> ownersLock(ownersMutex); owners[*chain] = session;
    }
    return result;
}
XrResult XRAPI_PTR destroySwapchain(XrSwapchain chain) {
    auto s = owner(chain);
    if (s) { std::lock_guard<std::mutex> lock(s->mutex);
        auto it = s->sources.find(chain);
        if (it != s->sources.end()) {
            if (it->second.snapshot) {
                EglCurrent saved;
                if (eglMakeCurrent(s->display, s->cleanupSurface, s->cleanupSurface, s->cleanupContext)) {
                    glDeleteTextures(1, &it->second.snapshot); saved.restore(s->display);
                }
            }
            s->sources.erase(it); s->learned = false;
        }
    }
    { std::lock_guard<std::mutex> lock(ownersMutex); owners.erase(chain); }
    return g.destroySwapchain(chain);
}
XrResult XRAPI_PTR acquire(XrSwapchain chain, const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
    const auto result = g.acquire(chain, info, index);
    auto s = owner(chain);
    if (XR_SUCCEEDED(result) && s) { std::lock_guard<std::mutex> lock(s->mutex); s->sources.at(chain).acquired.push_back({*index, false}); }
    return result;
}
XrResult XRAPI_PTR wait(XrSwapchain chain, const XrSwapchainImageWaitInfo* info) {
    const auto result = g.wait(chain, info);
    auto s = owner(chain);
    if (result == XR_SUCCESS && s) { std::lock_guard<std::mutex> lock(s->mutex);
        for (auto& image : s->sources.at(chain).acquired) if (!image.waited) { image.waited = true; break; } }
    return result;
}
XrResult XRAPI_PTR release(XrSwapchain chain, const XrSwapchainImageReleaseInfo* info) {
    auto s = owner(chain);
    if (!s) return g.release(chain, info);
    std::lock_guard<std::mutex> lock(s->mutex);
    auto& image = s->sources.at(chain);
    const bool selected = s->learned && (s->expected[0].swapchain == chain || s->expected[1].swapchain == chain);
    if (selected) image.valid = false;
    if (selected && !s->disabled && visible(*s) && !image.acquired.empty() && image.acquired.front().waited &&
        !snapshot(*s, image, image.acquired.front().index)) fail(*s, "owned_image_snapshot_failed");
    const auto result = g.release(chain, info);
    if (XR_SUCCEEDED(result)) { if (!image.acquired.empty()) image.acquired.pop_front(); ++image.serial; }
    else image.valid = false;
    return result;
}
XrResult XRAPI_PTR poll(XrInstance instance, XrEventDataBuffer* event) {
    const auto result = g.poll(instance, event);
    if (result == XR_SUCCESS && event && event->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(event);
        auto s = sessions.find(changed->session);
        if (s) s->state.store(changed->state);
    }
    return result;
}
XrResult XRAPI_PTR destroyInstance(XrInstance instance) {
    for (auto& s : sessions.clear()) cleanup(*s);
    { std::lock_guard<std::mutex> lock(ownersMutex); owners.clear(); }
    const auto result = g.destroyInstance(instance); g = {}; vm = nullptr; enabled = false; return result;
}
XrResult XRAPI_PTR getProc(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;
#define ROUTE(n, f) if (std::strcmp(name, n) == 0) *function = reinterpret_cast<PFN_xrVoidFunction>(f)
    ROUTE("xrGetInstanceProcAddr", getProc);
    else ROUTE("xrDestroyInstance", destroyInstance);
    else ROUTE("xrCreateSession", createSession);
    else ROUTE("xrDestroySession", destroySession);
    else ROUTE("xrCreateSwapchain", createSwapchain);
    else ROUTE("xrDestroySwapchain", destroySwapchain);
    else ROUTE("xrAcquireSwapchainImage", acquire);
    else ROUTE("xrWaitSwapchainImage", wait);
    else ROUTE("xrReleaseSwapchainImage", release);
    else ROUTE("xrEndFrame", endFrame);
    else ROUTE("xrPollEvent", poll);
    else return g.get ? g.get(instance, name, function) : XR_ERROR_FUNCTION_UNSUPPORTED;
#undef ROUTE
    return XR_SUCCESS;
}
template<class T> bool load(const char* name, T& pointer) {
    PFN_xrVoidFunction address{};
    const auto result = g.get(g.instance, name, &address);
    pointer = reinterpret_cast<T>(address); return XR_SUCCEEDED(result) && pointer;
}
XrResult XRAPI_PTR createInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer, XrInstance* instance) {
    if (!info || !layer || !layer->nextInfo || !instance) return XR_ERROR_INITIALIZATION_FAILED;
    for (auto* next = static_cast<const XrBaseInStructure*>(info->next); next; next = next->next)
        if (next->type == XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR)
            vm = static_cast<JavaVM*>(reinterpret_cast<const XrInstanceCreateInfoAndroidKHR*>(next)->applicationVM);
    std::vector<const char*> extensions;
    bool appEnabled{};
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        extensions.push_back(info->enabledExtensionNames[i]);
        appEnabled |= std::strcmp(info->enabledExtensionNames[i], XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME) == 0;
    }
    if (!appEnabled) extensions.push_back(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
    auto patched = *info; patched.enabledExtensionCount = extensions.size(); patched.enabledExtensionNames = extensions.data();
    auto next = *layer; next.nextInfo = layer->nextInfo->next;
    auto result = layer->nextInfo->nextCreateApiLayerInstance(&patched, &next, instance);
    enabled = XR_SUCCEEDED(result);
    if (XR_FAILED(result) && !appEnabled) result = layer->nextInfo->nextCreateApiLayerInstance(info, &next, instance);
    if (XR_FAILED(result)) return result;
    g.instance = *instance; g.get = layer->nextInfo->nextGetInstanceProcAddr;
    const bool loaded = load("xrDestroyInstance", g.destroyInstance) && load("xrCreateSession", g.createSession) &&
        load("xrDestroySession", g.destroySession) && load("xrCreateSwapchain", g.createSwapchain) &&
        load("xrDestroySwapchain", g.destroySwapchain) && load("xrEnumerateSwapchainImages", g.enumerateImages) &&
        load("xrAcquireSwapchainImage", g.acquire) && load("xrWaitSwapchainImage", g.wait) &&
        load("xrReleaseSwapchainImage", g.release) && load("xrEndFrame", g.endFrame) && load("xrPollEvent", g.poll);
    if (!loaded) { if (g.destroyInstance) g.destroyInstance(*instance); *instance = XR_NULL_HANDLE; return XR_ERROR_INITIALIZATION_FAILED; }
    enabled = enabled && load("xrCreateSwapchainAndroidSurfaceKHR", g.createSurface);
    log("layer_initialized", std::string("\"extensionEnabled\":") + (enabled ? "true" : "false") +
        ",\"sourceProjectionCount\":3,\"foveaProjectionIndex\":2,\"dummyQuad\":false,\"RGB10Supported\":false");
    return XR_SUCCESS;
}
} // namespace
extern "C" __attribute__((visibility("default"))) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader, const char* name, XrNegotiateApiLayerRequest* request) {
    if (!loader || !name || !request || std::strcmp(name, kLayerName) ||
        loader->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION || loader->maxApiVersion < XR_CURRENT_API_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = getProc; request->createApiLayerInstance = createInstance;
    return XR_SUCCESS;
}
