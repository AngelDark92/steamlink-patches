#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <time.h>
#include <unordered_set>
#include <unistd.h>
#include <vector>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))
#ifndef GXR_LAYER_NAME
#define GXR_LAYER_NAME "XR_APILAYER_local_GalaxyXR_three_projection_sampler_proxy_v1"
#endif

namespace {

constexpr char kLayerName[] = GXR_LAYER_NAME;
constexpr char kLogTag[] = "GXRResolutionTrace";
constexpr char kModeName[] = "three_projection_sampler_proxy_v1";
constexpr char kBuildId[] = "three-projection-sampler-proxy-v1.2-20260829";
constexpr uint32_t kExtent = 1536;
constexpr int64_t kFormat = GL_SRGB8_ALPHA8;
constexpr XrSwapchainUsageFlags kUsage =
    XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
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
    PFN_xrEnumerateSwapchainImages enumerateImages{};
    PFN_xrAcquireSwapchainImage acquireImage{};
    PFN_xrWaitSwapchainImage waitImage{};
    PFN_xrReleaseSwapchainImage releaseImage{};
    PFN_xrEndFrame endFrame{};
};

struct SwapchainState {
    XrSession session{XR_NULL_HANDLE};
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::deque<uint32_t> acquired;
    bool waited{};
    bool deferred{};
    bool source{};
    bool safeCreateInfo{};
};

struct ProxyState {
    XrSwapchain handle{XR_NULL_HANDLE};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    uint32_t index{};
    bool acquired{};
    bool waited{};
    bool poisoned{};
};

struct SessionState {
    XrSession session{XR_NULL_HANDLE};
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext context{EGL_NO_CONTEXT};
    std::array<XrSwapchain, 6> sources{};
    std::array<ProxyState, 6> proxies{};
    GLuint readFbo{}, drawFbo{};
    std::array<bool, 6> textureStateEmitted{};
    size_t stagedProxyCount{};
    std::string lastAuxiliarySignature;
    bool learned{}, active{}, everActivated{}, disabled{}, cacheValid{};
};

struct Fingerprint {
    bool valid{};
    std::string reason;
    XrSession session{XR_NULL_HANDLE};
    std::array<const XrCompositionLayerProjection*, 3> projections{};
    std::array<XrSwapchain, 6> handles{};
};

Dispatch g;
std::map<XrSwapchain, SwapchainState> swapchains;
std::map<XrSession, SessionState> sessions;
std::mutex stateMutex;
std::atomic<uint64_t> frameCounter{0};

template <typename F>
XrResult callUnlocked(std::unique_lock<std::mutex>& lock, F&& call) {
    lock.unlock();
    const XrResult result = call();
    lock.lock();
    return result;
}

uint64_t elapsedMs() {
    timespec value{};
    clock_gettime(CLOCK_BOOTTIME, &value);
    return static_cast<uint64_t>(value.tv_sec) * 1000ULL +
        static_cast<uint64_t>(value.tv_nsec) / 1000000ULL;
}

template <typename H> uint64_t hv(H handle) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
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

template <typename T> void load(const char* name, T& fn) {
    PFN_xrVoidFunction address{};
    if (g.getInstanceProcAddr &&
        XR_SUCCEEDED(g.getInstanceProcAddr(g.instance, name, &address)))
        fn = reinterpret_cast<T>(address);
}

bool sample(uint64_t frame) { return frame <= 3 || frame % 90 == 0; }
bool close(float a, float b) { return std::fabs(a - b) <= 0.0001f; }
bool samePose(const XrPosef& a, const XrPosef& b) {
    return close(a.orientation.x, b.orientation.x) && close(a.orientation.y, b.orientation.y) &&
        close(a.orientation.z, b.orientation.z) && close(a.orientation.w, b.orientation.w) &&
        close(a.position.x, b.position.x) && close(a.position.y, b.position.y) &&
        close(a.position.z, b.position.z);
}
bool sameFov(const XrFovf& a, const XrFovf& b) {
    return close(a.angleLeft, b.angleLeft) && close(a.angleRight, b.angleRight) &&
        close(a.angleUp, b.angleUp) && close(a.angleDown, b.angleDown);
}
float spanX(const XrFovf& f) { return std::tan(f.angleRight) - std::tan(f.angleLeft); }
float spanY(const XrFovf& f) { return std::tan(f.angleUp) - std::tan(f.angleDown); }
bool finitePositiveFov(const XrFovf& f) {
    const float x = spanX(f), y = spanY(f);
    return std::isfinite(x) && std::isfinite(y) && x > 0.01f && y > 0.01f;
}
bool strictlyInset(const XrFovf& full, const XrFovf& inset) {
    constexpr float epsilon = 0.0001f;
    return finitePositiveFov(full) && finitePositiveFov(inset) &&
        inset.angleLeft >= full.angleLeft - epsilon &&
        inset.angleRight <= full.angleRight + epsilon &&
        inset.angleDown >= full.angleDown - epsilon &&
        inset.angleUp <= full.angleUp + epsilon &&
        spanX(inset) < spanX(full) * .95f &&
        spanY(inset) < spanY(full) * .95f;
}
bool safeProjectionNext(const void* next) {
    if (!next) return true;
    const auto* header = static_cast<const XrBaseInStructure*>(next);
    if (header->type != XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB || header->next) return false;
    return reinterpret_cast<const XrCompositionLayerSettingsFB*>(next)->layerFlags == 0;
}

Fingerprint inspect(const XrFrameEndInfo* info) {
    Fingerprint f;
    if (!info) {
        f.reason = "null";
        return f;
    }
    if (info->type != XR_TYPE_FRAME_END_INFO) {
        f.reason = "type";
        return f;
    }
    if (info->next) {
        f.reason = "next";
        return f;
    }
    if (info->layerCount == 0) {
        f.reason = "no_layers";
        return f;
    }
    if (info->layerCount != 3 || !info->layers) {
        f.reason = "layer_count";
        return f;
    }
    for (size_t layer = 0; layer < 3; ++layer) {
        if (!info->layers[layer] ||
            info->layers[layer]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            f.reason = "non_projection";
            return f;
        }
        f.projections[layer] = reinterpret_cast<const XrCompositionLayerProjection*>(
            info->layers[layer]);
        if (f.projections[layer]->viewCount != 2 || !f.projections[layer]->views ||
            !safeProjectionNext(f.projections[layer]->next)) {
            f.reason = "projection_metadata";
            return f;
        }
    }
    if (f.projections[0]->layerFlags != 0 || f.projections[1]->layerFlags != 0 ||
        f.projections[2]->layerFlags != kFoveaFlags) {
        f.reason = "flags";
        return f;
    }
    if (!f.projections[0]->space ||
        f.projections[0]->space != f.projections[1]->space ||
        f.projections[1]->space != f.projections[2]->space) {
        f.reason = "space";
        return f;
    }
    XrSession owner = XR_NULL_HANDLE;
    std::unordered_set<XrSwapchain> unique;
    for (size_t layer = 0; layer < 3; ++layer) {
        for (size_t eye = 0; eye < 2; ++eye) {
            const auto& view = f.projections[layer]->views[eye];
            const auto& rect = view.subImage.imageRect;
            if (view.type != XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW || view.next ||
                rect.offset.x || rect.offset.y || rect.extent.width != kExtent ||
                rect.extent.height != kExtent || view.subImage.imageArrayIndex != 0) {
                f.reason = "subimage";
                return f;
            }
            if (!samePose(view.pose, f.projections[1]->views[eye].pose)) {
                f.reason = "pose";
                return f;
            }
            auto it = swapchains.find(view.subImage.swapchain);
            if (it == swapchains.end()) {
                f.reason = "unknown_swapchain";
                return f;
            }
            const auto& ci = it->second.info;
            if (!it->second.safeCreateInfo || ci.createFlags || ci.usageFlags != kUsage ||
                ci.format != kFormat ||
                ci.sampleCount != 2 || ci.width != kExtent || ci.height != kExtent ||
                ci.faceCount != 1 || ci.arraySize != 1 || ci.mipCount != 1) {
                f.reason = "swapchain_info";
                return f;
            }
            if (!owner) owner = it->second.session;
            if (owner != it->second.session) {
                f.reason = "session";
                return f;
            }
            f.handles[layer * 2 + eye] = view.subImage.swapchain;
            unique.insert(view.subImage.swapchain);
        }
    }
    for (size_t eye = 0; eye < 2; ++eye) {
        const auto& full = f.projections[1]->views[eye].fov;
        const auto& fovea = f.projections[2]->views[eye].fov;
        if (!sameFov(f.projections[0]->views[eye].fov, full) ||
            !strictlyInset(full, fovea)) {
            f.reason = "topology";
            return f;
        }
    }
    if (unique.size() != 6) {
        f.reason = "duplicate_swapchain";
        return f;
    }
    f.valid = true;
    f.session = owner;
    return f;
}

uint32_t deferredMask(const SessionState& state) {
    uint32_t mask = 0;
    for (size_t i = 0; i < state.sources.size(); ++i) {
        auto it = swapchains.find(state.sources[i]);
        if (it != swapchains.end() && it->second.deferred) mask |= 1u << i;
    }
    return mask;
}

bool flushSources(SessionState& state, std::unique_lock<std::mutex>& lock) {
    bool ok = true;
    for (XrSwapchain handle : state.sources) {
        auto it = swapchains.find(handle);
        if (it == swapchains.end() || !it->second.deferred) continue;
        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        XrResult result = callUnlocked(lock, [&] { return g.releaseImage(handle, &releaseInfo); });
        ok &= XR_SUCCEEDED(result);
        if (XR_SUCCEEDED(result)) {
            it->second.deferred = false;
            it->second.waited = false;
            if (!it->second.acquired.empty()) it->second.acquired.pop_front();
        }
        if (XR_FAILED(result) || sample(frameCounter.load()))
            emit("proxy_source_release", "\"swapchain\":" + std::to_string(hv(handle)) +
                ",\"result\":" + std::to_string(result));
    }
    return ok;
}

bool releaseProxies(SessionState& state, std::unique_lock<std::mutex>& lock) {
    bool ok = true;
    for (auto& proxy : state.proxies) {
        if (!proxy.acquired) continue;
        if (!proxy.waited) {
            proxy.poisoned = true;
            ok = false;
            continue;
        }
        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        XrResult result = callUnlocked(lock,
            [&] { return g.releaseImage(proxy.handle, &releaseInfo); });
        ok &= XR_SUCCEEDED(result);
        if (XR_SUCCEEDED(result)) {
            proxy.acquired = false;
            proxy.waited = false;
            proxy.poisoned = false;
        }
        if (XR_FAILED(result) || sample(frameCounter.load()))
            emit("proxy_release", "\"swapchain\":" + std::to_string(hv(proxy.handle)) +
                ",\"result\":" + std::to_string(result));
    }
    return ok;
}

bool primeProxy(SessionState& state, size_t index, std::unique_lock<std::mutex>& lock);

void destroyProxies(SessionState& state, std::unique_lock<std::mutex>& lock) {
    releaseProxies(state, lock);
    for (auto& proxy : state.proxies) {
        if (proxy.handle && !proxy.acquired && g.destroySwapchain) {
            callUnlocked(lock, [&] { return g.destroySwapchain(proxy.handle); });
            proxy = {};
        }
    }
    if (eglGetCurrentContext() == state.context) {
        if (state.readFbo) glDeleteFramebuffers(1, &state.readFbo);
        if (state.drawFbo) glDeleteFramebuffers(1, &state.drawFbo);
    }
    state.readFbo = state.drawFbo = 0;
    state.textureStateEmitted.fill(false);
    state.stagedProxyCount = 0;
    state.active = false;
    state.cacheValid = false;
}

bool createProxy(SessionState& state, size_t index, std::unique_lock<std::mutex>& lock) {
    if (index >= state.proxies.size() || state.proxies[index].handle) return false;
    auto& proxy = state.proxies[index];
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = kUsage;
    ci.format = kFormat;
    ci.sampleCount = 1;
    ci.width = kExtent;
    ci.height = kExtent;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    XrResult result = callUnlocked(lock,
        [&] { return g.createSwapchain(state.session, &ci, &proxy.handle); });
    if (XR_FAILED(result)) {
        emit("proxy_stage_failure", "\"proxyIndex\":" + std::to_string(index) +
            ",\"stage\":\"create\",\"result\":" + std::to_string(result));
        return false;
    }
    uint32_t count{};
    result = callUnlocked(lock,
        [&] { return g.enumerateImages(proxy.handle, 0, &count, nullptr); });
    if (XR_FAILED(result) || !count) {
        emit("proxy_stage_failure", "\"proxyIndex\":" + std::to_string(index) +
            ",\"stage\":\"enumerate_count\",\"result\":" + std::to_string(result));
        return false;
    }
    proxy.images.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    result = callUnlocked(lock, [&] {
        return g.enumerateImages(proxy.handle, count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(proxy.images.data()));
    });
    if (XR_FAILED(result)) {
        emit("proxy_stage_failure", "\"proxyIndex\":" + std::to_string(index) +
            ",\"stage\":\"enumerate_images\",\"result\":" + std::to_string(result));
        return false;
    }
    emit("proxy_staged", "\"proxyIndex\":" + std::to_string(index) +
        ",\"swapchain\":" + std::to_string(hv(proxy.handle)) +
        ",\"imageCount\":" + std::to_string(count) +
        ",\"width\":1536,\"height\":1536,\"sampleCount\":1");
    return true;
}

bool acquireProxies(SessionState& state, std::unique_lock<std::mutex>& lock) {
    for (auto& proxy : state.proxies) {
        if (proxy.acquired || proxy.poisoned) return false;
        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrResult result = callUnlocked(lock,
            [&] { return g.acquireImage(proxy.handle, &acquireInfo, &proxy.index); });
        if (XR_FAILED(result)) {
            emit("proxy_acquire_failure", "\"swapchain\":" +
                std::to_string(hv(proxy.handle)) + ",\"result\":" + std::to_string(result) +
                ",\"cleanup\":\"none_required\"");
            return false;
        }
        proxy.acquired = true;
        XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        result = callUnlocked(lock, [&] { return g.waitImage(proxy.handle, &waitInfo); });
        if (XR_FAILED(result)) {
            proxy.poisoned = true;
            emit("proxy_wait_failure", "\"swapchain\":" +
                std::to_string(hv(proxy.handle)) + ",\"result\":" + std::to_string(result) +
                ",\"cleanup\":\"runtime_session_teardown\"");
            return false;
        }
        proxy.waited = true;
    }
    return true;
}

bool acquireProxy(SessionState& state, size_t index, std::unique_lock<std::mutex>& lock) {
    if (index >= state.proxies.size()) return false;
    auto& proxy = state.proxies[index];
    if (!proxy.handle || proxy.acquired || proxy.poisoned) return false;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = callUnlocked(lock,
        [&] { return g.acquireImage(proxy.handle, &acquireInfo, &proxy.index); });
    if (XR_FAILED(result)) {
        emit("proxy_acquire_failure", "\"proxyIndex\":" + std::to_string(index) +
            ",\"swapchain\":" + std::to_string(hv(proxy.handle)) +
            ",\"result\":" + std::to_string(result));
        return false;
    }
    proxy.acquired = true;
    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = callUnlocked(lock, [&] { return g.waitImage(proxy.handle, &waitInfo); });
    if (XR_FAILED(result)) {
        proxy.poisoned = true;
        emit("proxy_wait_failure", "\"proxyIndex\":" + std::to_string(index) +
            ",\"swapchain\":" + std::to_string(hv(proxy.handle)) +
            ",\"result\":" + std::to_string(result));
        return false;
    }
    proxy.waited = true;
    return true;
}

struct SavedGl {
    GLint readFbo{}, drawFbo{}, texture2d{}, scissor[4]{};
    GLboolean colorMask[4]{};
    GLboolean scissorEnabled{};
};
SavedGl saveGl() {
    SavedGl saved;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved.readFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved.drawFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved.texture2d);
    glGetIntegerv(GL_SCISSOR_BOX, saved.scissor);
    glGetBooleanv(GL_COLOR_WRITEMASK, saved.colorMask);
    saved.scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    return saved;
}
void restoreGl(const SavedGl& saved) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, saved.readFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved.drawFbo);
    glBindTexture(GL_TEXTURE_2D, saved.texture2d);
    glScissor(saved.scissor[0], saved.scissor[1], saved.scissor[2], saved.scissor[3]);
    saved.scissorEnabled ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    glColorMask(saved.colorMask[0], saved.colorMask[1], saved.colorMask[2], saved.colorMask[3]);
}

GLenum drainGlErrors() {
    GLenum last = GL_NO_ERROR;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) last = error;
    return last;
}

bool configureProxyTexture(SessionState& state, size_t index, GLuint texture) {
    static constexpr const char* roles[] = {
        "base_l", "base_r", "under_l", "under_r", "fovea_l", "fovea_r"
    };
    drainGlErrors();
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint minFilter{}, magFilter{}, wrapS{}, wrapT{};
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &wrapT);
    const GLenum error = drainGlErrors();
    const bool ok = error == GL_NO_ERROR;
    if (!state.textureStateEmitted[index] || !ok) {
        emit("three_projection_sampler_proxy_texture_state",
            "\"session\":" + std::to_string(hv(state.session)) +
            ",\"role\":" + quote(roles[index]) +
            ",\"scope\":\"proxy_application_texture_object\",\"textureTarget\":\"GL_TEXTURE_2D\"" +
            ",\"minFilter\":" + std::to_string(minFilter) +
            ",\"magFilter\":" + std::to_string(magFilter) +
            ",\"wrapS\":" + std::to_string(wrapS) +
            ",\"wrapT\":" + std::to_string(wrapT) +
            ",\"querySuccess\":" + (ok ? "true" : "false") +
            ",\"glError\":" + std::to_string(error) +
            ",\"compositorSamplerStateKnown\":false");
        if (ok) state.textureStateEmitted[index] = true;
    }
    return ok;
}

bool sourceTexture(XrSwapchain handle, GLuint& texture) {
    auto it = swapchains.find(handle);
    if (it == swapchains.end() || it->second.acquired.size() != 1) return false;
    const uint32_t index = it->second.acquired.front();
    if (index >= it->second.images.size()) return false;
    texture = it->second.images[index].image;
    return texture != 0;
}

bool resolveSources(SessionState& state) {
    if (eglGetCurrentContext() != state.context || eglGetCurrentDisplay() != state.display) {
        emit("proxy_gl_failure", "\"reason\":\"egl_context_mismatch\"");
        return false;
    }
    if (!state.readFbo) glGenFramebuffers(1, &state.readFbo);
    if (!state.drawFbo) glGenFramebuffers(1, &state.drawFbo);
    if (!state.readFbo || !state.drawFbo) return false;
    SavedGl saved = saveGl();
    drainGlErrors();
    glDisable(GL_SCISSOR_TEST);
    bool ok = true;
    for (size_t i = 0; i < 6 && ok; ++i) {
        GLuint source{};
        if (!sourceTexture(state.sources[i], source) || !state.proxies[i].acquired ||
            state.proxies[i].index >= state.proxies[i].images.size()) {
            ok = false;
            break;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, state.readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, source, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state.drawFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, state.proxies[i].images[state.proxies[i].index].image, 0);
        const GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, drawBuffers);
        if (!configureProxyTexture(state, i,
                state.proxies[i].images[state.proxies[i].index].image)) {
            emit("proxy_gl_failure", "\"reason\":\"proxy_texture_state\",\"sourceIndex\":" +
                std::to_string(i));
            ok = false;
            break;
        }
        const GLenum readStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        const GLenum drawStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (readStatus != GL_FRAMEBUFFER_COMPLETE || drawStatus != GL_FRAMEBUFFER_COMPLETE) {
            emit("proxy_gl_failure", "\"reason\":\"framebuffer_incomplete\",\"sourceIndex\":" +
                std::to_string(i) + ",\"readStatus\":" + std::to_string(readStatus) +
                ",\"drawStatus\":" + std::to_string(drawStatus));
            ok = false;
            break;
        }
        glBlitFramebuffer(0, 0, kExtent, kExtent, 0, 0, kExtent, kExtent,
            GL_COLOR_BUFFER_BIT, GL_NEAREST);
        const GLenum error = drainGlErrors();
        if (error != GL_NO_ERROR) {
            emit("proxy_gl_failure", "\"reason\":\"blit_error\",\"sourceIndex\":" +
                std::to_string(i) + ",\"glError\":" + std::to_string(error));
            ok = false;
        }
    }
    glFlush();
    restoreGl(saved);
    return ok;
}

bool primeProxy(SessionState& state, size_t index, std::unique_lock<std::mutex>& lock) {
    if (!acquireProxy(state, index, lock)) return false;
    if (eglGetCurrentContext() != state.context || eglGetCurrentDisplay() != state.display) {
        emit("proxy_gl_failure", "\"reason\":\"prime_egl_context_mismatch\"");
        releaseProxies(state, lock);
        return false;
    }
    if (!state.drawFbo) glGenFramebuffers(1, &state.drawFbo);
    if (!state.drawFbo) {
        releaseProxies(state, lock);
        return false;
    }
    const SavedGl saved = saveGl();
    drainGlErrors();
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const GLfloat transparent[] = {0.f, 0.f, 0.f, 0.f};
    auto& proxy = state.proxies[index];
    bool ok = proxy.acquired && proxy.index < proxy.images.size();
    if (ok) {
        const GLuint texture = proxy.images[proxy.index].image;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state.drawFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, texture, 0);
        const GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, drawBuffers);
        ok = configureProxyTexture(state, index, texture) &&
            glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok) glClearBufferfv(GL_COLOR, 0, transparent);
        const GLenum error = drainGlErrors();
        if (error != GL_NO_ERROR) {
            emit("proxy_gl_failure", "\"reason\":\"prime_clear_error\",\"proxyIndex\":" +
                std::to_string(index) + ",\"glError\":" + std::to_string(error));
            ok = false;
        }
    }
    glFlush();
    restoreGl(saved);
    const bool releaseOk = releaseProxies(state, lock);
    emit("proxy_primed",
        "\"session\":" + std::to_string(hv(state.session)) +
        ",\"proxyIndex\":" + std::to_string(index) + ",\"success\":" +
        (ok && releaseOk ? "true" : "false"));
    return ok && releaseOk;
}

void disable(SessionState& state, const std::string& reason) {
    if (state.disabled) return;
    state.active = false;
    state.disabled = true;
    state.cacheValid = false;
    emit("three_projection_sampler_proxy_disabled",
        "\"session\":" + std::to_string(hv(state.session)) +
        ",\"reason\":" + quote(reason));
}

void learn(SessionState& state, const Fingerprint& fingerprint) {
    state.sources = fingerprint.handles;
    for (XrSwapchain handle : state.sources) swapchains[handle].source = true;
    state.learned = true;
    state.lastAuxiliarySignature.clear();
    emit("proxy_fingerprint_learned",
        "\"session\":" + std::to_string(hv(state.session)) +
        ",\"sourceProjectionCount\":3,\"sourceViewCount\":6,\"sourceExtent\":1536,"
        "\"sourceSampleCount\":2,\"sourceFormat\":35907");
}

void forgetSources(SessionState& state) {
    for (XrSwapchain handle : state.sources) {
        auto it = swapchains.find(handle);
        if (it != swapchains.end()) it->second.source = false;
    }
    state.sources.fill(XR_NULL_HANDLE);
    state.learned = false;
    state.active = false;
    state.everActivated = false;
    state.cacheValid = false;
}

bool disarm(SessionState& state, const std::string& reason,
    std::unique_lock<std::mutex>& lock, bool disableOnFailure = true) {
    const bool wasEverActivated = state.everActivated;
    const bool sourceOk = flushSources(state, lock);
    const bool proxyOk = releaseProxies(state, lock);
    forgetSources(state);
    destroyProxies(state, lock);
    emit("proxy_disarmed", "\"session\":" + std::to_string(hv(state.session)) +
        ",\"reason\":" + quote(reason) +
        ",\"everActivated\":" + (wasEverActivated ? "true" : "false") +
        ",\"sourceReleaseSuccess\":" + (sourceOk ? "true" : "false") +
        ",\"proxyReleaseSuccess\":" + (proxyOk ? "true" : "false"));
    if ((!sourceOk || !proxyOk) && disableOnFailure) {
        disable(state, "disarm_release_failed");
        return false;
    }
    return sourceOk && proxyOk;
}

bool isLearnedSource(const SessionState& state, XrSwapchain handle) {
    if (!state.learned || handle == XR_NULL_HANDLE) return false;
    for (XrSwapchain source : state.sources)
        if (source != XR_NULL_HANDLE && source == handle) return true;
    return false;
}

struct AuxiliaryObservation {
    uint32_t layerCount{};
    std::array<int64_t, 3> layerTypes{};
    uint32_t recordedTypeCount{};
    bool referencesLearnedSources{};
    std::string signature;
};

AuxiliaryObservation observeAuxiliary(const SessionState& state, const XrFrameEndInfo* info) {
    AuxiliaryObservation observation;
    if (!info || info->type != XR_TYPE_FRAME_END_INFO) {
        observation.signature = "invalid";
        return observation;
    }
    observation.layerCount = info->layerCount;
    if (!info->layers) {
        observation.signature = std::to_string(info->layerCount) + ":null";
        return observation;
    }
    for (uint32_t i = 0; i < info->layerCount; ++i) {
        const auto* layer = info->layers[i];
        if (!layer) continue;
        if (i < observation.layerTypes.size()) {
            observation.layerTypes[i] = static_cast<int64_t>(layer->type);
            observation.recordedTypeCount = i + 1;
        }
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            if (projection->views) {
                for (uint32_t view = 0; view < projection->viewCount; ++view)
                    observation.referencesLearnedSources |=
                        isLearnedSource(state, projection->views[view].subImage.swapchain);
            }
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const auto* quad = reinterpret_cast<const XrCompositionLayerQuad*>(layer);
            observation.referencesLearnedSources |=
                isLearnedSource(state, quad->subImage.swapchain);
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR) {
            const auto* cylinder = reinterpret_cast<const XrCompositionLayerCylinderKHR*>(layer);
            observation.referencesLearnedSources |=
                isLearnedSource(state, cylinder->subImage.swapchain);
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_CUBE_KHR) {
            const auto* cube = reinterpret_cast<const XrCompositionLayerCubeKHR*>(layer);
            observation.referencesLearnedSources |=
                isLearnedSource(state, cube->swapchain);
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR) {
            const auto* equirect = reinterpret_cast<const XrCompositionLayerEquirectKHR*>(layer);
            observation.referencesLearnedSources |=
                isLearnedSource(state, equirect->subImage.swapchain);
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) {
            const auto* equirect = reinterpret_cast<const XrCompositionLayerEquirect2KHR*>(layer);
            observation.referencesLearnedSources |=
                isLearnedSource(state, equirect->subImage.swapchain);
        }
    }
    std::ostringstream signature;
    signature << observation.layerCount;
    for (uint32_t i = 0; i < observation.recordedTypeCount; ++i)
        signature << ':' << observation.layerTypes[i];
    signature << ':' << observation.referencesLearnedSources;
    observation.signature = signature.str();
    return observation;
}

XrResult submitAuxiliary(XrSession session, SessionState& state,
    const XrFrameEndInfo* info, uint64_t frame, const std::string& reason,
    std::unique_lock<std::mutex>& lock) {
    const AuxiliaryObservation observation = observeAuxiliary(state, info);
    const uint32_t mask = deferredMask(state);
    bool auxiliaryOk = true;
    bool cacheRefreshed = false;
    bool sourceOk = true;
    bool proxyOk = true;
    if (state.active && mask == 0x3fu) {
        auxiliaryOk = acquireProxies(state, lock) && resolveSources(state);
        sourceOk = flushSources(state, lock);
        proxyOk = releaseProxies(state, lock);
        cacheRefreshed = auxiliaryOk && sourceOk && proxyOk;
        state.cacheValid = cacheRefreshed;
    } else {
        sourceOk = flushSources(state, lock);
        proxyOk = releaseProxies(state, lock);
        if (mask) state.cacheValid = false;
    }
    const bool changed = observation.signature != state.lastAuxiliarySignature;
    state.lastAuxiliarySignature = observation.signature;
    if (changed || sample(frame) || mask) {
        std::ostringstream types;
        types << '[';
        for (uint32_t i = 0; i < observation.recordedTypeCount; ++i) {
            if (i) types << ',';
            types << observation.layerTypes[i];
        }
        types << ']';
        emit("three_projection_sampler_proxy_auxiliary",
            "\"frame\":" + std::to_string(frame) +
            ",\"session\":" + std::to_string(hv(session)) +
            ",\"layerCount\":" + std::to_string(observation.layerCount) +
            ",\"layerTypes\":" + types.str() +
            ",\"referencesLearnedSources\":" +
                (observation.referencesLearnedSources ? "true" : "false") +
            ",\"everActivated\":" + (state.everActivated ? "true" : "false") +
            ",\"deferredMask\":" + std::to_string(mask) +
            ",\"cacheRefreshed\":" + (cacheRefreshed ? "true" : "false") +
            ",\"cacheInvalidated\":" + (mask && !cacheRefreshed ? "true" : "false") +
            ",\"reason\":" + quote(reason));
    }
    const XrFrameEndInfo* submitted = info;
    XrFrameEndInfo empty{XR_TYPE_FRAME_END_INFO};
    if (!auxiliaryOk || !sourceOk || !proxyOk) {
        disable(state, auxiliaryOk ? "auxiliary_release_failed" : "auxiliary_proxy_refresh_failed");
        if (info) empty = *info;
        empty.layerCount = 0;
        empty.layers = nullptr;
        submitted = &empty;
    }
    XrResult result = callUnlocked(lock, [&] { return g.endFrame(session, submitted); });
    if (changed || sample(frame) || mask || XR_FAILED(result))
        emit("end_frame_result", "\"frame\":" + std::to_string(frame) +
            ",\"session\":" + std::to_string(hv(session)) +
            ",\"auxiliary\":true,\"result\":" + std::to_string(result));
    return result;
}

XrResult passthrough(XrSession session, SessionState* state, const XrFrameEndInfo* info,
    uint64_t frame, const std::string& reason, bool transformedAttempt,
    std::unique_lock<std::mutex>& lock) {
    const uint32_t mask = state ? deferredMask(*state) : 0;
    const bool sourceOk = !state || flushSources(*state, lock);
    const bool proxyOk = !state || releaseProxies(*state, lock);
    const XrFrameEndInfo* submitted = info;
    XrFrameEndInfo empty{XR_TYPE_FRAME_END_INFO};
    if (!sourceOk || !proxyOk) {
        if (state) disable(*state, "passthrough_release_failed");
        if (info) empty = *info;
        empty.layerCount = 0;
        empty.layers = nullptr;
        submitted = &empty;
    }
    if (transformedAttempt || sample(frame))
        emit("three_projection_sampler_proxy_passthrough",
            "\"frame\":" + std::to_string(frame) +
            ",\"session\":" + std::to_string(hv(session)) +
            ",\"activated\":" + (state && state->everActivated ? "true" : "false") +
            ",\"sourceReleaseSuccess\":" + (sourceOk ? "true" : "false") +
            ",\"proxyReleaseSuccess\":" + (proxyOk ? "true" : "false") +
            ",\"deferredMask\":" + std::to_string(mask) +
            ",\"reason\":" + quote(reason));
    XrResult result = callUnlocked(lock, [&] { return g.endFrame(session, submitted); });
    if (transformedAttempt || sample(frame))
        emit("end_frame_result", "\"frame\":" + std::to_string(frame) +
            ",\"session\":" + std::to_string(hv(session)) +
            ",\"result\":" + std::to_string(result));
    return result;
}

XrResult XRAPI_PTR layerCreateSession(XrInstance instance, const XrSessionCreateInfo* info,
    XrSession* session) {
    std::unique_lock<std::mutex> lock(stateMutex);
    XrResult result = callUnlocked(lock, [&] { return g.createSession(instance, info, session); });
    if (XR_FAILED(result) || !session) return result;
    SessionState state;
    state.session = *session;
    auto* next = info ? static_cast<const XrBaseInStructure*>(info->next) : nullptr;
    for (int i = 0; next && i < 32; ++i, next = next->next) {
        if (next->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR) {
            const auto* binding = reinterpret_cast<const XrGraphicsBindingOpenGLESAndroidKHR*>(next);
            state.display = binding->display;
            state.context = binding->context;
            break;
        }
    }
    sessions[*session] = state;
    emit("session_created", "\"session\":" + std::to_string(hv(*session)) +
        ",\"hasGlesBinding\":" + (state.context != EGL_NO_CONTEXT ? "true" : "false"));
    return result;
}

XrResult XRAPI_PTR layerCreateSwapchain(XrSession session,
    const XrSwapchainCreateInfo* info, XrSwapchain* handle) {
    std::unique_lock<std::mutex> lock(stateMutex);
    XrResult result = callUnlocked(lock,
        [&] { return g.createSwapchain(session, info, handle); });
    if (XR_SUCCEEDED(result) && info && handle) {
        SwapchainState state;
        state.session = session;
        state.info = *info;
        state.safeCreateInfo = info->next == nullptr;
        state.info.next = nullptr;
        swapchains[*handle] = state;
        emit("create_swapchain", "\"swapchain\":" + std::to_string(hv(*handle)) +
            ",\"width\":" + std::to_string(info->width) + ",\"height\":" +
            std::to_string(info->height) + ",\"sampleCount\":" +
            std::to_string(info->sampleCount) + ",\"format\":" + std::to_string(info->format));
    }
    return result;
}

XrResult XRAPI_PTR layerEnumerateImages(XrSwapchain handle, uint32_t capacity,
    uint32_t* count, XrSwapchainImageBaseHeader* images) {
    std::unique_lock<std::mutex> lock(stateMutex);
    XrResult result = callUnlocked(lock,
        [&] { return g.enumerateImages(handle, capacity, count, images); });
    auto it = swapchains.find(handle);
    if (XR_SUCCEEDED(result) && it != swapchains.end() && count && images && capacity >= *count) {
        it->second.images.resize(*count);
        for (uint32_t i = 0; i < *count; ++i) {
            it->second.images[i] = *reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(
                reinterpret_cast<char*>(images) + i * sizeof(XrSwapchainImageOpenGLESKHR));
        }
    }
    return result;
}

XrResult XRAPI_PTR layerAcquire(XrSwapchain handle,
    const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
    std::unique_lock<std::mutex> lock(stateMutex);
    auto it = swapchains.find(handle);
    if (it != swapchains.end() && it->second.deferred) {
        auto sit = sessions.find(it->second.session);
        if (sit != sessions.end()) {
            const bool flushed = flushSources(sit->second, lock);
            sit->second.cacheValid = false;
            emit("proxy_source_reacquire", "\"session\":" +
                std::to_string(hv(sit->second.session)) +
                ",\"flushSuccess\":" + (flushed ? "true" : "false"));
            if (!flushed) disable(sit->second, "reacquire_release_failed");
        }
    }
    XrResult result = callUnlocked(lock, [&] { return g.acquireImage(handle, info, index); });
    if (XR_SUCCEEDED(result) && it != swapchains.end() && index) {
        it->second.acquired.push_back(*index);
        it->second.waited = false;
    }
    return result;
}

XrResult XRAPI_PTR layerWait(XrSwapchain handle, const XrSwapchainImageWaitInfo* info) {
    std::unique_lock<std::mutex> lock(stateMutex);
    XrResult result = callUnlocked(lock, [&] { return g.waitImage(handle, info); });
    auto it = swapchains.find(handle);
    if (XR_SUCCEEDED(result) && it != swapchains.end()) it->second.waited = true;
    return result;
}

XrResult XRAPI_PTR layerRelease(XrSwapchain handle,
    const XrSwapchainImageReleaseInfo* info) {
    std::unique_lock<std::mutex> lock(stateMutex);
    auto it = swapchains.find(handle);
    if (it != swapchains.end() && it->second.source) {
        auto sit = sessions.find(it->second.session);
        if (sit != sessions.end() && sit->second.active && !sit->second.disabled && info &&
            !info->next && it->second.waited && it->second.acquired.size() == 1 &&
            !it->second.deferred) {
            it->second.deferred = true;
            if (sample(frameCounter.load() + 1))
                emit("proxy_source_release_deferred", "\"swapchain\":" +
                    std::to_string(hv(handle)) + ",\"imageIndex\":" +
                    std::to_string(it->second.acquired.front()));
            return XR_SUCCESS;
        }
        if (sit != sessions.end() && sit->second.active) {
            sit->second.cacheValid = false;
            emit("proxy_source_release_passthrough", "\"session\":" +
                std::to_string(hv(sit->second.session)) +
                ",\"swapchain\":" + std::to_string(hv(handle)) +
                ",\"reason\":\"unsupported_sequence\"");
        }
    }
    XrResult result = callUnlocked(lock, [&] { return g.releaseImage(handle, info); });
    if (XR_SUCCEEDED(result) && it != swapchains.end()) {
        it->second.waited = false;
        if (!it->second.acquired.empty()) it->second.acquired.pop_front();
    }
    return result;
}

XrResult XRAPI_PTR layerEndFrame(XrSession session, const XrFrameEndInfo* info) {
    std::unique_lock<std::mutex> lock(stateMutex);
    const uint64_t frame = ++frameCounter;
    Fingerprint fingerprint = inspect(info);
    auto sit = sessions.find(session);
    if (sit == sessions.end())
        return passthrough(session, nullptr, info, frame, "unknown_session", false, lock);
    SessionState& state = sit->second;
    if (!fingerprint.valid || fingerprint.session != session)
        return submitAuxiliary(session, state, info, frame,
            fingerprint.reason.empty() ? "non_target" : fingerprint.reason, lock);
    if (state.disabled)
        return submitAuxiliary(session, state, info, frame, "disabled", lock);
    if (!state.learned) {
        XrResult result = callUnlocked(lock, [&] { return g.endFrame(session, info); });
        if (XR_SUCCEEDED(result)) learn(state, fingerprint);
        emit("end_frame_result", "\"frame\":" + std::to_string(frame) +
            ",\"session\":" + std::to_string(hv(session)) +
            ",\"learned\":" + (XR_SUCCEEDED(result) ? "true" : "false") +
            ",\"result\":" + std::to_string(result));
        return result;
    }
    if (state.sources != fingerprint.handles) {
        XrResult result = callUnlocked(lock, [&] { return g.endFrame(session, info); });
        if (XR_SUCCEEDED(result) && disarm(state, "source_identity_changed", lock))
            learn(state, fingerprint);
        emit("end_frame_result", "\"frame\":" + std::to_string(frame) +
            ",\"session\":" + std::to_string(hv(session)) +
            ",\"relearned\":" +
                (XR_SUCCEEDED(result) && state.learned ? "true" : "false") +
            ",\"result\":" + std::to_string(result));
        return result;
    }
    if (!state.active) {
        XrResult result = callUnlocked(lock, [&] { return g.endFrame(session, info); });
        if (XR_SUCCEEDED(result)) {
            const size_t index = state.stagedProxyCount;
            const bool staged = state.context != EGL_NO_CONTEXT &&
                createProxy(state, index, lock) && primeProxy(state, index, lock);
            if (!staged) {
                disable(state, "proxy_stage_failed");
            } else {
                ++state.stagedProxyCount;
                emit("proxy_stage_progress", "\"session\":" +
                    std::to_string(hv(session)) + ",\"stagedProxyCount\":" +
                    std::to_string(state.stagedProxyCount) + ",\"requiredProxyCount\":6");
                if (state.stagedProxyCount == state.proxies.size()) {
                    state.active = true;
                    emit("proxy_ready", "\"session\":" + std::to_string(hv(session)) +
                        ",\"proxySwapchainCount\":6,\"everActivated\":" +
                        (state.everActivated ? "true" : "false"));
                }
            }
        }
        emit("end_frame_result", "\"frame\":" + std::to_string(frame) +
            ",\"session\":" + std::to_string(hv(session)) +
            ",\"staging\":true,\"stagedProxyCount\":" +
                std::to_string(state.stagedProxyCount) +
            ",\"result\":" + std::to_string(result));
        return result;
    }
    const uint32_t mask = deferredMask(state);
    const bool cached = mask == 0 && state.cacheValid;
    if (mask != 0x3fu && !cached) {
        state.cacheValid = false;
        return passthrough(session, &state, info, frame,
            mask ? "partial_source_update" : "cached_proxy_unavailable", true, lock);
    }
    if (!cached) {
        if (!acquireProxies(state, lock) || !resolveSources(state)) {
            disable(state, "proxy_resolve_failed");
            return passthrough(session, &state, info, frame, "proxy_resolve_failed", true, lock);
        }
        const bool sourceOk = flushSources(state, lock);
        const bool proxyOk = releaseProxies(state, lock);
        if (!sourceOk || !proxyOk) {
            disable(state, "downstream_release_failed");
            return passthrough(session, &state, info, frame, "downstream_release_failed", true,
                lock);
        }
        state.cacheValid = true;
    }
    std::array<std::array<XrCompositionLayerProjectionView, 2>, 3> views{};
    std::array<XrCompositionLayerProjection, 3> projections{};
    std::array<const XrCompositionLayerBaseHeader*, 3> layers{};
    for (size_t layer = 0; layer < 3; ++layer) {
        projections[layer] = *fingerprint.projections[layer];
        for (size_t eye = 0; eye < 2; ++eye) {
            views[layer][eye] = fingerprint.projections[layer]->views[eye];
            views[layer][eye].subImage.swapchain = state.proxies[layer * 2 + eye].handle;
        }
        projections[layer].views = views[layer].data();
        layers[layer] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projections[layer]);
    }
    XrFrameEndInfo output = *info;
    output.layers = layers.data();
    emit("three_projection_sampler_proxy_transform",
        "\"frame\":" + std::to_string(frame) +
        ",\"session\":" + std::to_string(hv(session)) +
        ",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"sourceViewCount\":6,"
        "\"forwardedLayerCount\":3,\"outputProjectionCount\":3,\"outputViewCount\":6,"
        "\"proxySwapchainCount\":6,\"changedSubImageCount\":6,"
        "\"sourceWidth\":1536,\"sourceHeight\":1536,\"sourceSampleCount\":2,"
        "\"proxyWidth\":1536,\"proxyHeight\":1536,\"proxySampleCount\":1,"
        "\"format\":35907,\"resolved\":true,\"resampled\":false,"
        "\"preservedLayerCount\":true,\"preservedLayerOrder\":true,"
        "\"preservedFlags\":true,\"preservedSpaces\":true,\"preservedPoses\":true,"
        "\"preservedFovs\":true,\"preservedRects\":true,"
        "\"preservedProjectionNext\":true,\"unsafeLayerCount\":0,"
        "\"changed\":true,\"releaseSuccess\":true,"
        "\"sourceUpdate\":\"" + std::string(cached ? "cached" : "fresh") +
        "\",\"deferredMask\":" + std::to_string(mask) +
        ",\"reusedProxy\":" + (cached ? "true" : "false"));
    XrResult result = callUnlocked(lock, [&] { return g.endFrame(session, &output); });
    if (XR_FAILED(result)) disable(state, "proxy_end_frame_failed");
    else state.everActivated = true;
    emit("end_frame_result", "\"frame\":" + std::to_string(frame) +
        ",\"session\":" + std::to_string(hv(session)) +
        ",\"result\":" + std::to_string(result));
    return result;
}

XrResult XRAPI_PTR layerDestroySwapchain(XrSwapchain handle) {
    std::unique_lock<std::mutex> lock(stateMutex);
    auto it = swapchains.find(handle);
    if (it != swapchains.end()) {
        auto sit = sessions.find(it->second.session);
        if (sit != sessions.end() && it->second.source)
            disarm(sit->second, "source_destroyed", lock, false);
        else if (it->second.deferred && sit != sessions.end())
            flushSources(sit->second, lock);
        swapchains.erase(it);
    }
    return callUnlocked(lock, [&] { return g.destroySwapchain(handle); });
}

XrResult XRAPI_PTR layerDestroySession(XrSession session) {
    std::unique_lock<std::mutex> lock(stateMutex);
    auto sit = sessions.find(session);
    if (sit != sessions.end()) {
        flushSources(sit->second, lock);
        destroyProxies(sit->second, lock);
        sessions.erase(sit);
    }
    for (auto it = swapchains.begin(); it != swapchains.end();)
        if (it->second.session == session) it = swapchains.erase(it); else ++it;
    return callUnlocked(lock, [&] { return g.destroySession(session); });
}

XrResult XRAPI_PTR layerDestroyInstance(XrInstance instance) {
    std::unique_lock<std::mutex> lock(stateMutex);
    for (auto& item : sessions) {
        flushSources(item.second, lock);
        destroyProxies(item.second, lock);
    }
    sessions.clear();
    swapchains.clear();
    emit("destroy_instance");
    XrResult result = callUnlocked(lock, [&] { return g.destroyInstance(instance); });
    g = {};
    return result;
}

XrResult XRAPI_PTR layerGetInstanceProcAddr(XrInstance instance, const char* name,
    PFN_xrVoidFunction* fn) {
    std::unique_lock<std::mutex> lock(stateMutex);
    if (!name || !fn) return XR_ERROR_VALIDATION_FAILURE;
#define ROUTE(n, f) if (std::strcmp(name, n) == 0) *fn = reinterpret_cast<PFN_xrVoidFunction>(f)
    ROUTE("xrGetInstanceProcAddr", layerGetInstanceProcAddr);
    else ROUTE("xrDestroyInstance", layerDestroyInstance);
    else ROUTE("xrCreateSession", layerCreateSession);
    else ROUTE("xrDestroySession", layerDestroySession);
    else ROUTE("xrCreateSwapchain", layerCreateSwapchain);
    else ROUTE("xrDestroySwapchain", layerDestroySwapchain);
    else ROUTE("xrEnumerateSwapchainImages", layerEnumerateImages);
    else ROUTE("xrAcquireSwapchainImage", layerAcquire);
    else ROUTE("xrWaitSwapchainImage", layerWait);
    else ROUTE("xrReleaseSwapchainImage", layerRelease);
    else ROUTE("xrEndFrame", layerEndFrame);
    else {
        if (!g.getInstanceProcAddr) return XR_ERROR_FUNCTION_UNSUPPORTED;
        return callUnlocked(lock, [&] { return g.getInstanceProcAddr(instance, name, fn); });
    }
#undef ROUTE
    return XR_SUCCESS;
}

XrResult XRAPI_PTR layerCreateApiLayerInstance(const XrInstanceCreateInfo* createInfo,
    const XrApiLayerCreateInfo* layerInfo, XrInstance* instance) {
    std::unique_lock<std::mutex> lock(stateMutex);
    if (!layerInfo || !layerInfo->nextInfo) return XR_ERROR_INITIALIZATION_FAILED;
    XrApiLayerCreateInfo next = *layerInfo;
    next.nextInfo = layerInfo->nextInfo->next;
    XrResult result = callUnlocked(lock, [&] {
        return layerInfo->nextInfo->nextCreateApiLayerInstance(createInfo, &next, instance);
    });
    if (XR_FAILED(result)) return result;
    g.instance = *instance;
    g.getInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;
    lock.unlock();
    load("xrDestroyInstance", g.destroyInstance);
    load("xrCreateSession", g.createSession);
    load("xrDestroySession", g.destroySession);
    load("xrCreateSwapchain", g.createSwapchain);
    load("xrDestroySwapchain", g.destroySwapchain);
    load("xrEnumerateSwapchainImages", g.enumerateImages);
    load("xrAcquireSwapchainImage", g.acquireImage);
    load("xrWaitSwapchainImage", g.waitImage);
    load("xrReleaseSwapchainImage", g.releaseImage);
    load("xrEndFrame", g.endFrame);
    lock.lock();
    emit("layer_initialized", "\"layerName\":" + quote(kLayerName) +
        ",\"proxyBuildId\":" + quote(kBuildId));
    return XR_SUCCESS;
}

} // namespace

GXR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader, const char* name,
    XrNegotiateApiLayerRequest* request) {
    if (!loader || !name || !request || std::strcmp(name, kLayerName) != 0)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (loader->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loader->maxApiVersion < XR_CURRENT_API_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = layerGetInstanceProcAddr;
    request->createApiLayerInstance = layerCreateApiLayerInstance;
    return XR_SUCCESS;
}
