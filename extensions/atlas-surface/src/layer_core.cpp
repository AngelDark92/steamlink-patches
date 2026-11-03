// Experimental resolution API layers for Steam Link.
//
// GXR_LAYER_MODE_ENLARGE: grows Valve's projection swapchains by 1.5x (never
// shrinking) without adding a composition layer. The app keeps its own viewport
// and imageRect, so this is a pure extent-sensitivity preflight.
//
// GXR_LAYER_MODE_ATLAS: copies each projection view's source sub-rect into a
// runtime Android Surface swapchain sized to the runtime's max per-eye extent,
// re-points the existing projection views at those images, and adds no layer.
// It is a presentation supersample, not a decoder redirect.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <android/data_space.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "layer_frame.h"
#include "layer_policy.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

#if defined(GXR_LAYER_MODE_ENLARGE) && defined(GXR_LAYER_MODE_ATLAS)
#error Select exactly one layer mode
#elif defined(GXR_LAYER_MODE_ENLARGE)
namespace {
constexpr bool atlasMode = false;
constexpr char layerName[] = "XR_APILAYER_local_GalaxyXR_swapchain_enlarge_v1";
constexpr char tag[] = "GXRSwapchainEnlarge";
constexpr char buildId[] = "swapchain-enlarge-v1.0-20261027";
}
#elif defined(GXR_LAYER_MODE_ATLAS)
namespace {
constexpr bool atlasMode = true;
constexpr char layerName[] = "XR_APILAYER_local_GalaxyXR_atlas_surface_v1";
constexpr char tag[] = "GXRAtlasSurface";
constexpr char buildId[] = "atlas-surface-v1.0-20261027";
}
#else
#error Define GXR_LAYER_MODE_ENLARGE or GXR_LAYER_MODE_ATLAS
#endif

namespace {
constexpr uint32_t maxProjections = 3;
constexpr int64_t srgb8 = 0x8c43;
constexpr uint32_t fallbackMaxExtent = 4096;

std::recursive_mutex mutex;
JavaVM* vm{};
bool enabled{};

struct Dispatch {
    XrInstance instance{};
    PFN_xrGetInstanceProcAddr get{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrCreateSession createSession{};
    PFN_xrDestroySession destroySession{};
    PFN_xrEndSession endSession{};
    PFN_xrCreateReferenceSpace createReferenceSpace{};
    PFN_xrDestroySpace destroySpace{};
    PFN_xrCreateSwapchain create{};
    PFN_xrCreateSwapchainAndroidSurfaceKHR createSurface{};
    PFN_xrDestroySwapchain destroy{};
    PFN_xrEnumerateSwapchainImages enumerate{};
    PFN_xrAcquireSwapchainImage acquire{};
    PFN_xrWaitSwapchainImage wait{};
    PFN_xrReleaseSwapchainImage release{};
    PFN_xrEndFrame endFrame{};
    PFN_xrPollEvent poll{};
    PFN_xrEnumerateViewConfigurationViews enumerateViews{};
} g;

struct Surface {
    XrSwapchain xr{};
    ANativeWindow* window{};
    EGLSurface egl{EGL_NO_SURFACE};
    gxr::layer::Extent extent{};
};

struct Session {
    XrSession xr{};
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext appContext{EGL_NO_CONTEXT}, context{EGL_NO_CONTEXT};
    EGLSurface pbuffer{EGL_NO_SURFACE};
    EGLConfig config{};
    GLuint program{}, vao{}, sampler{};
    bool visible{}, failed{};
    uint64_t frames{}, copies{};
    gxr::layer::Extent maxExtent{fallbackMaxExtent, fallbackMaxExtent};
};

struct Swapchain {
    XrSession session{};
    XrSwapchainCreateInfo info{};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    gxr::layer::ImageQueue queue;
    bool supported{}, enlarged{}, ready{};
    Surface target;
    XrRect2Di lastRect{};
    bool hasRect{};
    uint32_t failures{};
};

std::map<XrSession, Session> sessions;
std::map<XrSwapchain, Swapchain> chains;

void log(const char* event, int value = 0) {
    __android_log_print(ANDROID_LOG_INFO, tag,
        "event=%s value=%d atlas=%d build=%s", event, value, atlasMode ? 1 : 0, buildId);
}

bool extension(const char* list, const char* name) {
    if (!list) return false;
    const std::string haystack = std::string(" ") + list + " ";
    return haystack.find(std::string(" ") + name + " ") != std::string::npos;
}

bool supportedInfo(const XrSwapchainCreateInfo& info) {
    return !info.next && !info.createFlags &&
        (info.usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT) &&
        (info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) &&
        info.format == srgb8 &&
        gxr::layer::dimensionsValid(info.width, info.height, info.arraySize,
                                     info.sampleCount, info.faceCount, info.mipCount);
}

// Separate shared GL context: never change Valve's FBO, program, sampler or VAO.
// Restore the exact context plus separate read/draw surfaces on every exit.
struct Current {
    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    EGLSurface draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
    EGLenum api = eglQueryAPI();
    EGLDisplay temporary{EGL_NO_DISPLAY};
    bool enter(Session& s, EGLSurface target) {
        temporary = s.display;
        return eglBindAPI(EGL_OPENGL_ES_API) &&
            eglMakeCurrent(s.display, target, target, s.context);
    }
    ~Current() {
        if (context != EGL_NO_CONTEXT) {
            if (!eglMakeCurrent(display, draw, read, context)) log("restore_context_failed");
        } else if (temporary != EGL_NO_DISPLAY) {
            eglMakeCurrent(temporary, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        eglBindAPI(api);
    }
};

GLuint shader(GLenum type, const char* text) {
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &text, nullptr); glCompileShader(result);
    GLint ok{}; glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(result); return 0; }
    return result;
}

GLuint program() {
    constexpr char vertex[] = "#version 300 es\n"
        "out highp vec2 uv; void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "uv=p;gl_Position=vec4(p*2.-1.,0.,1.);}";
    constexpr char fragment[] = "#version 300 es\nprecision highp float;"
        "uniform highp sampler2D video;uniform highp vec4 rect;"
        "in highp vec2 uv;out vec4 c;"
        "void main(){c=texture(video,rect.xy+uv*rect.zw);}";
    GLuint v = shader(GL_VERTEX_SHADER, vertex), f = shader(GL_FRAGMENT_SHADER, fragment);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint result = glCreateProgram(); glAttachShader(result,v); glAttachShader(result,f);
    glLinkProgram(result); glDeleteShader(v); glDeleteShader(f);
    GLint ok{}; glGetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(result); return 0; }
    return result;
}

bool initializeGl(Session& s) {
    if (!atlasMode) return false;
    if (s.context != EGL_NO_CONTEXT) return true;
    const char* exts = eglQueryString(s.display, EGL_EXTENSIONS);
    if (!extension(exts, "EGL_KHR_gl_colorspace")) return false;
    std::vector<EGLint> attributes = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_SAMPLES, 0,
        EGL_NONE,
    };
    EGLint count{};
    if (!eglChooseConfig(s.display, attributes.data(), &s.config, 1, &count) || count != 1) return false;
    for (EGLint field : {EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE}) {
        EGLint size{}; if (!eglGetConfigAttrib(s.display, s.config, field, &size) || size != 8) return false;
    }
    Current old;
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return false;
    const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    s.context = eglCreateContext(s.display, s.config, s.appContext, contextAttrs);
    if (s.context == EGL_NO_CONTEXT) return false;
    const EGLint pbAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    s.pbuffer = eglCreatePbufferSurface(s.display, s.config, pbAttrs);
    if (s.pbuffer == EGL_NO_SURFACE || !old.enter(s, s.pbuffer)) return false;
    s.program = program();
    glGenVertexArrays(1,&s.vao); glGenSamplers(1,&s.sampler);
    glSamplerParameteri(s.sampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glSamplerParameteri(s.sampler,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glSamplerParameteri(s.sampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glSamplerParameteri(s.sampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return s.program && glGetError() == GL_NO_ERROR;
}

struct Jni {
    JNIEnv* env{}; bool attached{};
    Jni() {
        if (!vm) return;
        jint result = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (result == JNI_EDETACHED) {
            attached = vm->AttachCurrentThread(&env,nullptr) == JNI_OK;
            if (!attached) env = nullptr;
        } else if (result != JNI_OK) env = nullptr;
    }
    ~Jni() { if (attached) vm->DetachCurrentThread(); }
};

void cleanupSurface(Session& s, Surface& target) {
    if (target.egl != EGL_NO_SURFACE && s.display != EGL_NO_DISPLAY)
        eglDestroySurface(s.display, target.egl);
    target.egl = EGL_NO_SURFACE;
    if (target.window) ANativeWindow_release(target.window);
    target.window = nullptr;
    if (target.xr) g.destroy(target.xr);
    target.xr = XR_NULL_HANDLE;
    target.extent = {};
}

bool ensureTarget(Session& s, Swapchain& source, gxr::layer::Extent extent) {
    if (!atlasMode) return false;
    if (source.target.xr && gxr::layer::sameExtent(source.target.extent, extent)) return true;
    cleanupSurface(s, source.target);
    Jni jni; if (!jni.env) return false;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.width = extent.width;
    info.height = extent.height;
    jobject surface{};
    XrResult result = g.createSurface(s.xr, &info, &source.target.xr, &surface);
    if (XR_FAILED(result) || !surface) {
        log("create_surface_failed", result);
        cleanupSurface(s, source.target);
        return false;
    }
    source.target.window = ANativeWindow_fromSurface(jni.env, surface);
    jni.env->DeleteLocalRef(surface);
    if (!source.target.window) { cleanupSurface(s, source.target); return false; }
    if (ANativeWindow_setBuffersGeometry(source.target.window, extent.width, extent.height,
            AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM) != 0 ||
        ANativeWindow_setBuffersDataSpace(source.target.window, ADATASPACE_SRGB) != 0) {
        cleanupSurface(s, source.target);
        return false;
    }
    const EGLint attrs[] = {
        EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_SRGB_KHR, EGL_NONE,
    };
    source.target.egl = eglCreateWindowSurface(s.display, s.config, source.target.window, attrs);
    if (source.target.egl == EGL_NO_SURFACE) {
        log("egl_window_failed", eglGetError());
        cleanupSurface(s, source.target);
        return false;
    }
    source.target.extent = extent;
    log("target_surface", static_cast<int>(extent.width));
    return true;
}

bool transfer(Session& s, Swapchain& source, uint32_t imageIndex,
              const XrRect2Di& rect) {
    if (!atlasMode) return false;
    if (eglGetCurrentDisplay() != s.display || eglGetCurrentContext() != s.appContext ||
        imageIndex >= source.images.size() || !source.target.egl) return false;
    const auto uv = gxr::layer::sourceRectUv(
        rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height,
        source.info.width, source.info.height);
    if (!(uv.width > 0.0f) || !(uv.height > 0.0f)) return false;
    GLsync rendered = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if (!rendered) return false;
    glFlush();
    bool ok = false;
    {
        Current restore;
        if (initializeGl(s) && restore.enter(s, s.pbuffer)) {
            glWaitSync(rendered,0,GL_TIMEOUT_IGNORED);
            if (eglMakeCurrent(s.display, source.target.egl, source.target.egl, s.context)) {
                eglSwapInterval(s.display,0);
                glBindFramebuffer(GL_FRAMEBUFFER,0);
                glViewport(0,0,source.target.extent.width,source.target.extent.height);
                glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST);
                glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
                glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
                glClearColor(0,0,0,0);
                glClear(GL_COLOR_BUFFER_BIT);
                glBindVertexArray(s.vao); glActiveTexture(GL_TEXTURE0);
                glUseProgram(s.program);
                glUniform1i(glGetUniformLocation(s.program,"video"),0);
                glUniform4f(glGetUniformLocation(s.program,"rect"),
                    uv.x,uv.y,uv.width,uv.height);
                glBindTexture(GL_TEXTURE_2D, source.images[imageIndex].image);
                glBindSampler(0,s.sampler);
                glDrawArrays(GL_TRIANGLES,0,3);
                glFinish();
                ok = glGetError() == GL_NO_ERROR && eglSwapBuffers(s.display,source.target.egl);
            }
        }
        if (eglGetCurrentContext() == s.context && s.context != EGL_NO_CONTEXT) glFinish();
    }
    glDeleteSync(rendered);
    if (ok && ++s.copies <= 3) log("surface_frame",int(s.copies));
    return ok;
}

void cleanupSwap(Session& s, Swapchain& source) {
    source.ready = false;
    source.queue.clear();
    cleanupSurface(s, source.target);
    source.images.clear();
    source.hasRect = false;
    source.failures = 0;
}

void cleanupSession(Session& s) {
    for (auto& entry : chains) if (entry.second.session == s.xr) cleanupSwap(s, entry.second);
    if (s.context != EGL_NO_CONTEXT) {
        {
            Current old;
            if (s.pbuffer != EGL_NO_SURFACE && old.enter(s, s.pbuffer)) {
                glDeleteProgram(s.program);
                glDeleteSamplers(1,&s.sampler);
                glDeleteVertexArrays(1,&s.vao);
                glFinish();
            }
        }
        if (s.pbuffer != EGL_NO_SURFACE) eglDestroySurface(s.display, s.pbuffer);
        eglDestroyContext(s.display, s.context);
    }
    s.context = EGL_NO_CONTEXT; s.pbuffer = EGL_NO_SURFACE;
}

XrResult XRAPI_PTR createSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* output) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    XrResult result = g.createSession(instance, info, output);
    if (XR_FAILED(result) || !output || !info) return result;
    Session s; s.xr=*output;
    for (auto* next=static_cast<const XrBaseInStructure*>(info->next); next; next=next->next) {
        if (next->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR) {
            auto* binding=reinterpret_cast<const XrGraphicsBindingOpenGLESAndroidKHR*>(next);
            s.display=binding->display; s.appContext=binding->context; break;
        }
    }
    if (atlasMode && g.enumerateViews) {
        uint32_t count{};
        if (XR_SUCCEEDED(g.enumerateViews(instance, info->systemId,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr)) && count) {
            std::vector<XrViewConfigurationView> views(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
            if (XR_SUCCEEDED(g.enumerateViews(instance, info->systemId,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, views.data()))) {
                gxr::layer::Extent max{};
                for (const auto& view : views) {
                    max.width = std::max(max.width, view.maxImageRectWidth);
                    max.height = std::max(max.height, view.maxImageRectHeight);
                }
                if (max.width && max.height) s.maxExtent = max;
            }
        }
    }
    s.failed = !enabled || (atlasMode && (s.display == EGL_NO_DISPLAY || s.appContext == EGL_NO_CONTEXT));
    auto* stored = &sessions.emplace(*output,s).first->second;
    log(stored->failed?"session_passthrough":"session_ready");
    return result;
}

XrResult XRAPI_PTR createSwap(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* output) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!info || !output) return g.create(session, info, output);
    const bool projection = supportedInfo(*info);
    XrSwapchainCreateInfo createInfo = *info;
    bool enlarged = false;
    if (projection && !atlasMode) {
        const auto grown = gxr::layer::enlargedExtent(info->width, info->height);
        if (grown.width != info->width || grown.height != info->height) {
            createInfo.width = grown.width;
            createInfo.height = grown.height;
            enlarged = true;
        }
    }
    XrResult result = g.create(session, &createInfo, output);
    if (XR_FAILED(result) && enlarged) {
        log("enlarge_rejected", result);
        result = g.create(session, info, output);
        enlarged = false;
    }
    if (XR_FAILED(result) || !output) return result;
    Swapchain source;
    source.session = session;
    source.info = *info;
    source.info.next = nullptr;
    // Atlas mode re-points each view at one Surface image per source swapchain;
    // array textures would need one target per slice and are passed through.
    source.supported = projection && (!atlasMode || info->arraySize == 1);
    source.enlarged = enlarged;
    if (source.supported && atlasMode) {
        uint32_t imageCount{};
        if (XR_SUCCEEDED(g.enumerate(*output, 0, &imageCount, nullptr)) &&
            imageCount && imageCount <= 32) {
            source.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            if (XR_FAILED(g.enumerate(*output, imageCount, &imageCount,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(source.images.data())))) {
                source.supported = false;
                source.images.clear();
            }
        } else {
            source.supported = false;
        }
    }
    chains.emplace(*output, std::move(source));
    if (enlarged) log("swapchain_enlarged", static_cast<int>(createInfo.width));
    else if (!projection) log("swapchain_tracked_non_projection", static_cast<int>(info->format));
    return result;
}

XrResult XRAPI_PTR enumerate(XrSwapchain chain, uint32_t capacity, uint32_t* count,
                             XrSwapchainImageBaseHeader* images) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    XrResult result = g.enumerate(chain, capacity, count, images);
    if (XR_FAILED(result) || !images || !count || !*count) return result;
    auto it = chains.find(chain);
    if (it == chains.end()) return result;
    if (images[0].type != XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR) return result;
    auto* glImages = reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(images);
    if (atlasMode) {
        it->second.images.assign(glImages, glImages + *count);
        return result;
    }
    if (!it->second.enlarged || eglGetCurrentContext() == EGL_NO_CONTEXT) return result;
    using ClearTexImage = void (*)(GLuint, GLint, GLenum, GLenum, const void*);
    static ClearTexImage clearTexImage =
        reinterpret_cast<ClearTexImage>(eglGetProcAddress("glClearTexImage"));
    if (!clearTexImage) return result;
    GLint previous{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    for (uint32_t i = 0; i < *count; ++i) {
        glBindTexture(GL_TEXTURE_2D, glImages[i].image);
        clearTexImage(glImages[i].image, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    if (glGetError() != GL_NO_ERROR) log("enlarge_clear_failed");
    return result;
}

XrResult XRAPI_PTR acquire(XrSwapchain chain, const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    XrResult result = g.acquire(chain, info, index);
    auto it = chains.find(chain);
    if (result == XR_SUCCESS && index && it != chains.end()) it->second.queue.acquired(*index);
    return result;
}

XrResult XRAPI_PTR waitImage(XrSwapchain chain, const XrSwapchainImageWaitInfo* info) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    XrResult result = g.wait(chain, info);
    auto it = chains.find(chain);
    if (result == XR_SUCCESS && it != chains.end()) it->second.queue.waited();
    return result;
}

XrResult XRAPI_PTR release(XrSwapchain chain, const XrSwapchainImageReleaseInfo* info) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it = chains.find(chain);
    bool copied = false;
    if (it != chains.end() && atlasMode) {
        auto& source = it->second;
        auto session = sessions.find(source.session);
        const auto* readable = source.queue.readable();
        if (session != sessions.end() && source.supported && readable && source.hasRect &&
            session->second.visible && !session->second.failed) {
            const auto extent = gxr::layer::targetExtent(
                static_cast<uint32_t>(source.lastRect.extent.width),
                static_cast<uint32_t>(source.lastRect.extent.height),
                session->second.maxExtent.width, session->second.maxExtent.height);
            if (extent.width && extent.height && ensureTarget(session->second, source, extent)) {
                copied = transfer(session->second, source, readable->index, source.lastRect);
            }
            if (!copied) {
                source.ready = false;
                if (++source.failures <= 3) log("transfer_failed_passthrough");
            }
        }
    }
    XrResult result = g.release(chain, info);
    if (it != chains.end()) {
        it->second.ready = result == XR_SUCCESS && (!atlasMode || copied);
        if (result == XR_SUCCESS) it->second.queue.released();
    }
    return result;
}

XrResult XRAPI_PTR endFrame(XrSession session, const XrFrameEndInfo* info) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto si = sessions.find(session);
    if (!atlasMode) {
        if (si != sessions.end() && ++si->second.frames <= 3) log("enlarged_frame", int(si->second.frames));
        return g.endFrame(session, info);
    }
    if (si == sessions.end() || si->second.failed || !si->second.visible || !info ||
        !info->layers) return g.endFrame(session, info);
    std::array<XrCompositionLayerProjection,maxProjections> layers{};
    std::array<std::array<XrCompositionLayerProjectionView,2>,maxProjections> views{};
    std::array<const XrCompositionLayerBaseHeader*,maxProjections> pointers{};
    std::array<std::array<XrRect2Di,2>,maxProjections> rects{};
    auto lookup=[](XrSwapchain xr)->Swapchain* {
        auto it=chains.find(xr); return it==chains.end() ? nullptr : &it->second;
    };
    if (!gxr::layer::prepareFrame<maxProjections>(session,*info,lookup,layers,views,pointers,rects))
        return g.endFrame(session,info);
    bool ready = true;
    for (uint32_t i=0;i<info->layerCount;++i) {
        for (uint32_t eye=0;eye<2;++eye) {
            auto* source=lookup(views[i][eye].subImage.swapchain);
            if (!source) { ready=false; continue; }
            source->lastRect=rects[i][eye];
            source->hasRect=true;
            const auto expected=gxr::layer::targetExtent(
                static_cast<uint32_t>(rects[i][eye].extent.width),
                static_cast<uint32_t>(rects[i][eye].extent.height),
                si->second.maxExtent.width,si->second.maxExtent.height);
            if (!source->ready || !source->target.xr ||
                !gxr::layer::sameExtent(source->target.extent,expected)) ready=false;
        }
    }
    if (!ready) return g.endFrame(session,info);
    for (uint32_t i=0;i<info->layerCount;++i) {
        for (uint32_t eye=0;eye<2;++eye) {
            auto* source=lookup(views[i][eye].subImage.swapchain);
            views[i][eye].subImage.swapchain=source->target.xr;
            views[i][eye].subImage.imageArrayIndex=0;
            views[i][eye].subImage.imageRect={
                {0,0},
                {static_cast<int32_t>(source->target.extent.width),
                 static_cast<int32_t>(source->target.extent.height)},
            };
        }
        layers[i].views=views[i].data();
        pointers[i]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layers[i]);
    }
    XrFrameEndInfo replacement=*info;
    replacement.layerCount=info->layerCount;
    replacement.layers=pointers.data();
    XrResult result=g.endFrame(session,&replacement);
    if (XR_FAILED(result)) log("surface_frame_rejected",result);
    else if (++si->second.frames<=3) log("surface_frame",int(si->second.frames));
    return result;
}

XrResult XRAPI_PTR poll(XrInstance instance, XrEventDataBuffer* event) {
    XrResult result = g.poll(instance, event);
    if (result != XR_SUCCESS || !event || event->type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        return result;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto* state=reinterpret_cast<const XrEventDataSessionStateChanged*>(event);
    auto si=sessions.find(state->session);
    if (si!=sessions.end()) {
        si->second.visible=state->state==XR_SESSION_STATE_VISIBLE || state->state==XR_SESSION_STATE_FOCUSED;
        if (!si->second.visible) for (auto& item:chains)
            if (item.second.session==state->session) item.second.ready=false;
    }
    return result;
}

XrResult XRAPI_PTR endSession(XrSession session) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it=sessions.find(session); if(it!=sessions.end()) it->second.visible=false;
    for (auto& item:chains) if (item.second.session==session) item.second.ready=false;
    return g.endSession(session);
}

XrResult XRAPI_PTR destroySwap(XrSwapchain chain) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it=chains.find(chain);
    if(it!=chains.end()) {
        auto si=sessions.find(it->second.session);
        if(si!=sessions.end()) cleanupSwap(si->second,it->second);
        chains.erase(it);
    }
    return g.destroy(chain);
}

XrResult XRAPI_PTR destroySession(XrSession session) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto it=sessions.find(session); if(it!=sessions.end()) {cleanupSession(it->second);sessions.erase(it);}
    for(auto itc=chains.begin();itc!=chains.end();) {
        if(itc->second.session==session) itc=chains.erase(itc); else ++itc;
    }
    return g.destroySession(session);
}

XrResult XRAPI_PTR destroyInstance(XrInstance instance) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for(auto& s:sessions) cleanupSession(s.second);
    sessions.clear();chains.clear();
    XrResult result=g.destroyInstance(instance);g={};vm=nullptr;enabled=false;return result;
}

XrResult XRAPI_PTR get(XrInstance instance, const char* name, PFN_xrVoidFunction* out) {
    if(!name || !out) return XR_ERROR_VALIDATION_FAILURE;
#define ROUTE(n,f) if(!std::strcmp(name,n)) { *out=reinterpret_cast<PFN_xrVoidFunction>(f);return XR_SUCCESS; }
    ROUTE("xrGetInstanceProcAddr",get)
    ROUTE("xrCreateSession",createSession) ROUTE("xrDestroySession",destroySession)
    ROUTE("xrEndSession",endSession) ROUTE("xrDestroyInstance",destroyInstance)
    ROUTE("xrCreateSwapchain",createSwap) ROUTE("xrDestroySwapchain",destroySwap)
    ROUTE("xrEnumerateSwapchainImages",enumerate)
    ROUTE("xrAcquireSwapchainImage",acquire) ROUTE("xrWaitSwapchainImage",waitImage)
    ROUTE("xrReleaseSwapchainImage",release) ROUTE("xrEndFrame",endFrame) ROUTE("xrPollEvent",poll)
#undef ROUTE
    return g.get?g.get(instance,name,out):XR_ERROR_FUNCTION_UNSUPPORTED;
}

template<class T> bool load(const char* name,T& fn) {
    PFN_xrVoidFunction address{};
    XrResult result=g.get(g.instance,name,&address);fn=reinterpret_cast<T>(address);
    return XR_SUCCEEDED(result) && fn;
}

XrResult XRAPI_PTR createInstance(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,
                                  XrInstance* out) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!info || !layer || !layer->nextInfo || !out || g.instance) return XR_ERROR_INITIALIZATION_FAILED;
    for(auto* next=static_cast<const XrBaseInStructure*>(info->next);next;next=next->next)
        if(next->type==XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR)
            vm=static_cast<JavaVM*>(reinterpret_cast<const XrInstanceCreateInfoAndroidKHR*>(next)->applicationVM);
    std::vector<const char*> extensions;
    bool present=false;
    for(uint32_t i=0;i<info->enabledExtensionCount;++i) {
        extensions.push_back(info->enabledExtensionNames[i]);
        present |= !std::strcmp(extensions.back(),XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
    }
    if(atlasMode && !present) extensions.push_back(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
    auto changed=*info;
    if (extensions.size() != info->enabledExtensionCount) {
        changed.enabledExtensionCount=extensions.size();
        changed.enabledExtensionNames=extensions.data();
    }
    auto next=*layer;next.nextInfo=layer->nextInfo->next;
    XrResult result=layer->nextInfo->nextCreateApiLayerInstance(&changed,&next,out);
    enabled=XR_SUCCEEDED(result);
    if(XR_FAILED(result) && atlasMode && !present) {
        result=layer->nextInfo->nextCreateApiLayerInstance(info,&next,out);enabled=false;
    }
    if(XR_FAILED(result)) {vm=nullptr;return result;}
    g.instance=*out;g.get=layer->nextInfo->nextGetInstanceProcAddr;
    bool ok=true;
#define LOAD(name,field) ok=load(name,g.field) && ok
    LOAD("xrDestroyInstance",destroyInstance);LOAD("xrCreateSession",createSession);
    LOAD("xrDestroySession",destroySession);LOAD("xrEndSession",endSession);
    LOAD("xrCreateSwapchain",create);LOAD("xrDestroySwapchain",destroy);
    LOAD("xrEnumerateSwapchainImages",enumerate);
    LOAD("xrAcquireSwapchainImage",acquire);LOAD("xrWaitSwapchainImage",wait);
    LOAD("xrReleaseSwapchainImage",release);LOAD("xrEndFrame",endFrame);LOAD("xrPollEvent",poll);
#undef LOAD
    if(atlasMode && enabled) enabled=load("xrCreateSwapchainAndroidSurfaceKHR",g.createSurface);
    load("xrEnumerateViewConfigurationViews",g.enumerateViews);
    if(!ok) {if(g.destroyInstance)g.destroyInstance(*out);g={};vm=nullptr;enabled=false;
        *out=XR_NULL_HANDLE;return XR_ERROR_INITIALIZATION_FAILED;}
    log("layer_initialized",enabled);return XR_SUCCESS;
}
} // namespace

EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loader,
    const char* name,XrNegotiateApiLayerRequest* request) {
    if(!loader || !name || !request || std::strcmp(name,layerName) ||
        loader->maxInterfaceVersion<XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loader->maxApiVersion<XR_MAKE_VERSION(1,0,0)) return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion=XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion=XR_MAKE_VERSION(1,0,0);
    request->getInstanceProcAddr=get;request->createApiLayerInstance=createInstance;return XR_SUCCESS;
}
