#include <android/log.h>
#include <dlfcn.h>
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
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <unistd.h>
#include <vector>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))
#ifndef GXR_LAYER_NAME
#define GXR_LAYER_NAME "XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_efficient_v1"
#endif

namespace {

constexpr char kLayerName[] = GXR_LAYER_NAME;
constexpr char kLogTag[] = "GXRResolutionTrace";
#if defined(GXR_NATIVE_RENDERER_HELPER) && defined(GXR_NATIVE_DUAL_FORMAT) && defined(GXR_DIAGNOSTIC_PROBE)
constexpr char kModeName[] = "single_projection_native_probe_v1";
constexpr char kBuildId[] = "single-projection-native-probe-v1.2-20260831";
constexpr uint64_t kSuccessSummaryInterval = 900;
#elif defined(GXR_NATIVE_RENDERER_HELPER) && defined(GXR_NATIVE_DUAL_FORMAT)
constexpr char kModeName[] = "single_projection_native_renderer_dual_v1";
constexpr char kBuildId[] = "single-projection-native-renderer-dual-v1.2-20260831";
constexpr uint64_t kSuccessSummaryInterval = 900;
#elif defined(GXR_NATIVE_RENDERER_HELPER)
constexpr char kModeName[] = "single_projection_native_renderer_v1";
constexpr char kBuildId[] = "single-projection-native-renderer-v1.4-20260831";
constexpr uint64_t kSuccessSummaryInterval = 900;
#else
constexpr char kModeName[] = "single_projection_reconstruction_efficient_v1";
constexpr char kBuildId[] = "single-projection-reconstruction-efficient-v1.4-20260831";
constexpr uint64_t kSuccessSummaryInterval = 30;
#endif
constexpr uint32_t kSourceExtent = 1536;
constexpr uint32_t kGalaxyXrPanelWidth = 3552;
constexpr uint32_t kGalaxyXrPanelHeight = 3840;
constexpr uint32_t kDensityPreservingTier = 0;
constexpr uint32_t kPanelNativeTier = 1;
constexpr uint32_t kReportedMaximumTier = 2;
const char* outputTierName(uint32_t tier){
    if(tier==kDensityPreservingTier)return "density_preserving";
    if(tier==kPanelNativeTier)return "panel_native";
    return "reported_maximum";
}
constexpr int64_t kSourceFormat = GL_SRGB8_ALPHA8;
#ifdef GXR_NATIVE_DUAL_FORMAT
constexpr int64_t kRgb10A2Format = GL_RGB10_A2;
bool supportedSourceFormat(int64_t format) {
    return format == kSourceFormat || format == kRgb10A2Format;
}
const char* sourcePrecision(int64_t format) {
    return format == kRgb10A2Format ? "rgb10a2" : "srgb8";
}
#endif
constexpr XrCompositionLayerFlags kFoveaFlags =
    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
    XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;

struct Dispatch {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrCreateInstance createInstance{};
    PFN_xrGetInstanceProcAddr getInstanceProcAddr{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrEnumerateViewConfigurationViews enumerateViews{};
    PFN_xrCreateSession createSession{};
    PFN_xrDestroySession destroySession{};
#ifdef GXR_NATIVE_DUAL_FORMAT
    PFN_xrEnumerateSwapchainFormats enumerateSwapchainFormats{};
#endif
    PFN_xrCreateSwapchain createSwapchain{};
    PFN_xrDestroySwapchain destroySwapchain{};
    PFN_xrEnumerateSwapchainImages enumerateImages{};
    PFN_xrAcquireSwapchainImage acquireImage{};
    PFN_xrWaitSwapchainImage waitImage{};
    PFN_xrReleaseSwapchainImage releaseImage{};
    PFN_xrEndFrame endFrame{};
    PFN_xrPollEvent pollEvent{};
    PFN_xrWaitFrame waitFrame{};
    PFN_xrRequestExitSession requestExitSession{};
};

enum class Role { Unknown, BaseL, BaseR, UnderL, UnderR, FoveaL, FoveaR };

struct SwapchainState {
    XrSession session{XR_NULL_HANDLE};
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::deque<uint32_t> acquired;
    bool waited{};
    bool deferred{};
    Role role{Role::Unknown};
#ifdef GXR_NATIVE_RENDERER_HELPER
    uint64_t generation{};
#endif
};

#ifdef GXR_NATIVE_RENDERER_HELPER
struct NativeSourceBinding {
    XrSwapchain handle{XR_NULL_HANDLE};
    SwapchainState* state{};
    uint64_t generation{};
};
#endif

struct OutputEye {
    XrSwapchain handle{XR_NULL_HANDLE};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    uint32_t index{};
    bool acquired{};
    bool waited{};
};

struct GlState {
    GLuint program{}, vao{};
    std::array<GLuint, 2> resolved{};
    std::unordered_map<GLuint, GLuint> framebuffers;
    std::vector<GLuint> pendingFramebufferDeletes;
    GLint underTexLocation{-1}, foveaTexLocation{-1};
    GLint fullTanLocation{-1}, foveaTanLocation{-1};
    std::array<XrFovf, 2> cachedFullFovs{}, cachedFoveaFovs{};
    std::array<std::array<GLfloat, 4>, 2> cachedFullTans{}, cachedFoveaTans{};
    bool fovCacheReady{}, ready{}, ditherStateObserved{}, ditherWasEnabled{};
};

struct ReconstructionMapping {
    bool valid{};
    XrSpace space{XR_NULL_HANDLE};
    std::array<XrPosef, 2> underPoses{};
    std::array<XrFovf, 2> underFovs{};
    std::array<XrFovf, 2> foveaFovs{};
};

struct SessionState {
    XrSession session{XR_NULL_HANDLE};
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext context{EGL_NO_CONTEXT};
    uint32_t recommendedWidth{}, recommendedHeight{}, maxWidth{}, maxHeight{};
    uint32_t outputWidth{}, outputHeight{};
    uint32_t outputAttemptTier{kDensityPreservingTier};
    bool outputAboveReportedMaximum{};
    bool outputTierAccepted{};
    bool viewLimitsDirty{};
    uint64_t viewLimitsGeneration{};
    std::array<XrSwapchain, 6> sources{};
    std::array<OutputEye, 2> outputs{};
    GlState gl;
    ReconstructionMapping mapping;
#ifdef GXR_NATIVE_RENDERER_HELPER
    std::array<NativeSourceBinding, 6> nativeSources{};
    std::array<XrFovf, 6> nativeFingerprintFovs{};
    bool nativeFingerprintReady{};
    uint64_t nativeFastFingerprintCount{}, nativeSlowFingerprintCount{};
    uint64_t nativeSourceCacheInvalidationCount{};
    uint64_t nativeDeferredReleaseCount{}, nativeForwardedReleaseCount{}, nativeOutputReleaseCount{};
#endif
#ifdef GXR_NATIVE_DUAL_FORMAT
    int64_t sourceFormat{};
#endif
#ifdef GXR_DIAGNOSTIC_PROBE
    std::array<XrViewConfigurationView, 2> probeViews{};
    std::array<uint32_t, 2> probeRequestedWidths{}, probeRequestedHeights{};
    std::array<uint32_t, 2> probeClampedWidths{}, probeClampedHeights{};
    std::array<bool, 2> probeOutputStorageEmitted{};
    bool probeViewLimitsReady{};
    bool probeSubmissionAttemptEmitted{}, probeSubmissionSuccessEmitted{};
#endif
    uint64_t successfulTransforms{}, transformsSinceSummary{};
    uint64_t freshSinceSummary{}, reusedSinceSummary{};
    uint64_t summaryFirstFrame{}, summaryFirstElapsedMs{};
    bool learned{}, active{}, disabled{}, everActivated{};
};

Dispatch g;
#ifdef GXR_NATIVE_RENDERER_HELPER
std::atomic<bool> nativeDispatchReady{false};
std::mutex nativeDispatchMutex;
std::atomic<uintptr_t> nativeStreamCallsite{0};
std::atomic<uintptr_t> nativeExitCallsite{0};
std::atomic<bool> nativeUnexpectedCallsiteLogged{false};
std::atomic<uint64_t> nextSwapchainGeneration{1};
std::atomic<uint64_t> nativeCallsiteCacheMissCount{0};
#endif
// Steam Link creates/destroys these objects outside the steady-state frame loop.
// std::map keeps element addresses stable while different swapchain elements are
// externally synchronized by OpenXR. No application/runtime call is made under
// a layer-global lock, avoiding lock inversion on the renderer thread.
std::map<XrSwapchain, SwapchainState> swapchains;
std::map<XrSession, SessionState> sessions;
std::array<XrViewConfigurationView, 2> stereoViews{};
bool haveStereoViews{};
bool qualitySettingsEnabled{};
bool recommendedResolutionEnabled{};
bool recommendedResolutionAppRequested{};
XrSystemId stereoSystemId{XR_NULL_SYSTEM_ID};
XrViewConfigurationType stereoViewType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
std::array<std::atomic<uint32_t>,8> publishedStereoViewLimits{};
std::atomic<uint64_t> publishedStereoViewLimitsGeneration{0};
std::mutex publishedStereoViewLimitsMutex;
#ifdef GXR_DIAGNOSTIC_PROBE
bool probeXrFbFoveationAdvertised{};
#endif
std::atomic<uint64_t> frameCounter{0};

uint64_t elapsedMs() {
    timespec t{};
    clock_gettime(CLOCK_BOOTTIME, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000ULL + static_cast<uint64_t>(t.tv_nsec) / 1000000ULL;
}

template <typename H> uint64_t hv(H h) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h));
}

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '\\') out << "\\\\";
        else if (c == '"') out << "\\\"";
        else if (c == '\n') out << "\\n";
        else if (c < 0x20) out << "?";
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
    if (g.getInstanceProcAddr && XR_SUCCEEDED(g.getInstanceProcAddr(g.instance, name, &address)))
        fn = reinterpret_cast<T>(address);
}

bool sample(uint64_t frame) {
#ifdef GXR_NATIVE_RENDERER_HELPER
    return frame <= 3;
#else
    return frame <= 3 || frame % 90 == 0;
#endif
}
bool close(float a, float b) { return std::fabs(a - b) <= 0.0001f; }
bool samePose(const XrPosef& a, const XrPosef& b) {
    return close(a.orientation.x,b.orientation.x) && close(a.orientation.y,b.orientation.y) &&
        close(a.orientation.z,b.orientation.z) && close(a.orientation.w,b.orientation.w) &&
        close(a.position.x,b.position.x) && close(a.position.y,b.position.y) && close(a.position.z,b.position.z);
}
bool sameFov(const XrFovf& a, const XrFovf& b) {
    return close(a.angleLeft,b.angleLeft) && close(a.angleRight,b.angleRight) &&
        close(a.angleUp,b.angleUp) && close(a.angleDown,b.angleDown);
}
bool sameMapping(const SessionState& s,const XrCompositionLayerProjection& under,const XrCompositionLayerProjection& fovea) {
    if(!s.mapping.valid||s.mapping.space!=under.space||under.space!=fovea.space)return false;
    for(int eye=0;eye<2;++eye)if(!sameFov(s.mapping.underFovs[eye],under.views[eye].fov)||
        !sameFov(s.mapping.foveaFovs[eye],fovea.views[eye].fov))return false;
    return true;
}
bool cachedPoseChanged(const SessionState& s,const XrCompositionLayerProjection& under) {
    for(int eye=0;eye<2;++eye)if(!samePose(s.mapping.underPoses[eye],under.views[eye].pose))return true;
    return false;
}
void rememberMapping(SessionState& s,const XrCompositionLayerProjection& under,const XrCompositionLayerProjection& fovea) {
    s.mapping.space=under.space;
    for(int eye=0;eye<2;++eye){s.mapping.underPoses[eye]=under.views[eye].pose;s.mapping.underFovs[eye]=under.views[eye].fov;s.mapping.foveaFovs[eye]=fovea.views[eye].fov;}
    s.mapping.valid=true;
}
float spanX(const XrFovf& f) { return std::tan(f.angleRight)-std::tan(f.angleLeft); }
float spanY(const XrFovf& f) { return std::tan(f.angleUp)-std::tan(f.angleDown); }
bool checkedOutputExtent(double value,uint32_t& output){
    if(!std::isfinite(value)||value<=0.0||value>static_cast<double>(std::numeric_limits<int32_t>::max()))
        return false;
    output=static_cast<uint32_t>(std::ceil(value));return true;
}
bool finitePositiveFov(const XrFovf& f) {
    const float x=spanX(f),y=spanY(f);
    return std::isfinite(x)&&std::isfinite(y)&&x>0.01f&&y>0.01f;
}
bool safeProjectionNext(const void* next) {
    if(!next)return true;
    const auto* header=static_cast<const XrBaseInStructure*>(next);
    if(header->type!=XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB||header->next)return false;
    return reinterpret_cast<const XrCompositionLayerSettingsFB*>(next)->layerFlags==0;
}

GLuint compile(GLenum type, const char* source, std::string& error) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok{};
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return shader;
    GLint length{};
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> text(static_cast<size_t>(std::max(length,1)));
    glGetShaderInfoLog(shader, length, nullptr, text.data());
    error = text.data();
    glDeleteShader(shader);
    return 0;
}

#ifdef GXR_DIAGNOSTIC_PROBE
std::string number(double value) {
    std::ostringstream out;
    out << std::setprecision(9) << value;
    return out.str();
}

bool glExtensionAdvertised(const char* wanted) {
    GLint count{};
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (extension && std::strcmp(extension, wanted) == 0) return true;
    }
    return false;
}

void emitGlStorageProbe(const char* attachment, int eye, GLuint texture, GLenum framebufferTarget,
                        GLenum framebufferStatus, int64_t requestedFormat,
                        uint32_t requestedWidth, uint32_t requestedHeight) {
    GLint internalFormat{}, width{}, height{}, redBits{}, greenBits{}, blueBits{}, alphaBits{};
    GLint immutable{}, immutableLevels{}, attachmentType{}, attachmentName{}, componentType{}, colorEncoding{};
    GLint attachmentRedBits{}, attachmentGreenBits{}, attachmentBlueBits{}, attachmentAlphaBits{};
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &redBits);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &greenBits);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_BLUE_SIZE, &blueBits);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &alphaBits);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_FORMAT, &immutable);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_LEVELS, &immutableLevels);
    glGetFramebufferAttachmentParameteriv(framebufferTarget, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attachmentType);
    glGetFramebufferAttachmentParameteriv(framebufferTarget, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attachmentName);
    glGetFramebufferAttachmentParameteriv(framebufferTarget, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &componentType);
    glGetFramebufferAttachmentParameteriv(framebufferTarget, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &colorEncoding);
    glGetFramebufferAttachmentParameteriv(framebufferTarget, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &attachmentRedBits);
    glGetFramebufferAttachmentParameteriv(framebufferTarget, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &attachmentGreenBits);
    glGetFramebufferAttachmentParameteriv(framebufferTarget, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE, &attachmentBlueBits);
    glGetFramebufferAttachmentParameteriv(framebufferTarget, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &attachmentAlphaBits);
    emit("reconstruction_attachment_precision",
        "\"attachment\":" + quote(attachment) +
        ",\"eye\":" + std::to_string(eye) +
        ",\"texture\":" + std::to_string(texture) +
        ",\"requestedFormat\":" + std::to_string(requestedFormat) +
        ",\"requestedWidth\":" + std::to_string(requestedWidth) +
        ",\"requestedHeight\":" + std::to_string(requestedHeight) +
        ",\"actualInternalFormat\":" + std::to_string(internalFormat) +
        ",\"internalFormat\":" + std::to_string(internalFormat) +
        ",\"actualWidth\":" + std::to_string(width) +
        ",\"actualHeight\":" + std::to_string(height) +
        ",\"textureRedBits\":" + std::to_string(redBits) +
        ",\"textureGreenBits\":" + std::to_string(greenBits) +
        ",\"textureBlueBits\":" + std::to_string(blueBits) +
        ",\"textureAlphaBits\":" + std::to_string(alphaBits) +
        ",\"immutableStorage\":" + std::string(immutable ? "true" : "false") +
        ",\"immutableLevels\":" + std::to_string(immutableLevels) +
        ",\"framebufferStatus\":" + std::to_string(framebufferStatus) +
        ",\"fboStatus\":" + std::to_string(framebufferStatus) +
        ",\"attachmentObjectType\":" + std::to_string(attachmentType) +
        ",\"attachmentObjectName\":" + std::to_string(attachmentName) +
        ",\"attachmentComponentType\":" + std::to_string(componentType) +
        ",\"attachmentColorEncoding\":" + std::to_string(colorEncoding) +
        ",\"attachmentRedBits\":" + std::to_string(attachmentRedBits) +
        ",\"attachmentGreenBits\":" + std::to_string(attachmentGreenBits) +
        ",\"attachmentBlueBits\":" + std::to_string(attachmentBlueBits) +
        ",\"attachmentAlphaBits\":" + std::to_string(attachmentAlphaBits) +
        ",\"redBits\":" + std::to_string(attachmentRedBits) +
        ",\"greenBits\":" + std::to_string(attachmentGreenBits) +
        ",\"blueBits\":" + std::to_string(attachmentBlueBits) +
        ",\"alphaBits\":" + std::to_string(attachmentAlphaBits));
}
#endif

void destroyGl(GlState& gl);

bool ensureGl(SessionState& s) {
    if (s.gl.ready) return true;
#ifdef GXR_DIAGNOSTIC_PROBE
    const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const bool glQcomTextureFoveatedSupported=
        glExtensionAdvertised("GL_QCOM_texture_foveated");
    emit("resolution_probe_capabilities",
        "\"vendor\":" + quote(vendor ? vendor : "") +
        ",\"renderer\":" + quote(renderer ? renderer : "") +
        ",\"version\":" + quote(version ? version : "") +
        ",\"xrFbFoveationAdvertised\":" +
        std::string(probeXrFbFoveationAdvertised ? "true" : "false") +
        ",\"glQcomTextureFoveatedSupported\":" +
        std::string(glQcomTextureFoveatedSupported ? "true" : "false"));
#endif
#ifdef GXR_NATIVE_DUAL_FORMAT
#define GXR_SAMPLER_DECLARATIONS "uniform highp sampler2D underTex;uniform highp sampler2D foveaTex;\n"
#else
#define GXR_SAMPLER_DECLARATIONS "uniform sampler2D underTex;uniform sampler2D foveaTex;\n"
#endif
    static const char* vertex = R"(#version 310 es
precision highp float;
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);})";
    static const char* fragment = R"(#version 310 es
precision highp float;
in vec2 uv;layout(location=0)out vec4 color;
)" GXR_SAMPLER_DECLARATIONS R"(uniform vec4 fullTan;uniform vec4 foveaTan;
void main(){
 vec2 ray=vec2(mix(fullTan.x,fullTan.y,uv.x),mix(fullTan.z,fullTan.w,uv.y));
 vec2 fuv=vec2((ray.x-foveaTan.x)/(foveaTan.y-foveaTan.x),(ray.y-foveaTan.z)/(foveaTan.w-foveaTan.z));
 vec4 base=texture(underTex,clamp(uv,vec2(0.0),vec2(1.0)));color=vec4(base.rgb,1.0);
 if(all(greaterThanEqual(fuv,vec2(0.0)))&&all(lessThanEqual(fuv,vec2(1.0)))){
  vec4 inset=texture(foveaTex,clamp(fuv,vec2(0.0),vec2(1.0)));color.rgb=mix(color.rgb,inset.rgb,inset.a);
 }
})";
#undef GXR_SAMPLER_DECLARATIONS
    std::string error;
    GLuint vs=compile(GL_VERTEX_SHADER,vertex,error), fs=compile(GL_FRAGMENT_SHADER,fragment,error);
    if (!vs || !fs) {
        emit("reconstruction_gl_failure","\"stage\":\"compile\",\"message\":"+quote(error));
        if(vs)glDeleteShader(vs); if(fs)glDeleteShader(fs); return false;
    }
    s.gl.program=glCreateProgram(); glAttachShader(s.gl.program,vs); glAttachShader(s.gl.program,fs);
    glLinkProgram(s.gl.program); glDeleteShader(vs); glDeleteShader(fs);
    GLint linked{}; glGetProgramiv(s.gl.program,GL_LINK_STATUS,&linked);
    if(!linked){emit("reconstruction_gl_failure","\"stage\":\"link\"");glDeleteProgram(s.gl.program);s.gl.program=0;return false;}
    s.gl.underTexLocation=glGetUniformLocation(s.gl.program,"underTex");
    s.gl.foveaTexLocation=glGetUniformLocation(s.gl.program,"foveaTex");
    s.gl.fullTanLocation=glGetUniformLocation(s.gl.program,"fullTan");
    s.gl.foveaTanLocation=glGetUniformLocation(s.gl.program,"foveaTan");
    if(s.gl.underTexLocation<0||s.gl.foveaTexLocation<0||
       s.gl.fullTanLocation<0||s.gl.foveaTanLocation<0){
        emit("reconstruction_gl_failure","\"stage\":\"uniform_locations\"");
        glDeleteProgram(s.gl.program);s.gl.program=0;return false;
    }
    glGenVertexArrays(1,&s.gl.vao);
    glGenTextures(2,s.gl.resolved.data());
    for(size_t textureIndex=0;textureIndex<s.gl.resolved.size();++textureIndex){
        const GLuint texture=s.gl.resolved[textureIndex];
        glBindTexture(GL_TEXTURE_2D,texture);
#ifdef GXR_NATIVE_DUAL_FORMAT
        glTexStorage2D(GL_TEXTURE_2D,1,static_cast<GLenum>(s.sourceFormat),kSourceExtent,kSourceExtent);
#else
        glTexStorage2D(GL_TEXTURE_2D,1,GL_SRGB8_ALPHA8,kSourceExtent,kSourceExtent);
#endif
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
#ifdef GXR_NATIVE_DUAL_FORMAT
        GLuint framebuffer{};
        glGenFramebuffers(1,&framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
#ifdef GXR_DIAGNOSTIC_PROBE
        emitGlStorageProbe("scratch", static_cast<int>(textureIndex),
            texture, GL_FRAMEBUFFER, status, s.sourceFormat, kSourceExtent, kSourceExtent);
#endif
        glDeleteFramebuffers(1,&framebuffer);
        if(status!=GL_FRAMEBUFFER_COMPLETE){
            emit("reconstruction_gl_failure","\"stage\":\"scratch_fbo\",\"status\":"+
                std::to_string(status)+",\"format\":"+std::to_string(s.sourceFormat));
            destroyGl(s.gl);return false;
        }
#endif
    }
    s.gl.ready=true;
    return s.gl.ready;
}

void destroyGl(GlState& gl) {
    if(gl.program)glDeleteProgram(gl.program); if(gl.vao)glDeleteVertexArrays(1,&gl.vao);
    for(const auto& pair:gl.framebuffers){GLuint fbo=pair.second;if(fbo)glDeleteFramebuffers(1,&fbo);}
    if(!gl.pendingFramebufferDeletes.empty())glDeleteFramebuffers(
        static_cast<GLsizei>(gl.pendingFramebufferDeletes.size()),gl.pendingFramebufferDeletes.data());
    glDeleteTextures(2,gl.resolved.data()); gl={};
}

struct SavedGl {
    GLint program{},vao{},active{},tex0{},tex1{},sampler0{},sampler1{},readFbo{},drawFbo{},viewport[4]{},scissor[4]{};
    GLboolean blend{},depth{},stencil{},cull{},dither{},scissorEnabled{},mask[4]{};
};
SavedGl saveGl(){
    SavedGl s;glGetIntegerv(GL_CURRENT_PROGRAM,&s.program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&s.vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&s.active);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&s.tex0);
    glGetIntegerv(GL_SAMPLER_BINDING,&s.sampler0);glActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&s.tex1);
    glGetIntegerv(GL_SAMPLER_BINDING,&s.sampler1);glActiveTexture(s.active);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&s.readFbo);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&s.drawFbo);
    glGetIntegerv(GL_VIEWPORT,s.viewport);glGetIntegerv(GL_SCISSOR_BOX,s.scissor);
    s.blend=glIsEnabled(GL_BLEND);s.depth=glIsEnabled(GL_DEPTH_TEST);s.stencil=glIsEnabled(GL_STENCIL_TEST);
    s.cull=glIsEnabled(GL_CULL_FACE);s.dither=glIsEnabled(GL_DITHER);s.scissorEnabled=glIsEnabled(GL_SCISSOR_TEST);glGetBooleanv(GL_COLOR_WRITEMASK,s.mask);return s;
}
void restoreGl(const SavedGl& s){
    auto en=[](GLenum c,GLboolean v){v?glEnable(c):glDisable(c);};glUseProgram(s.program);glBindVertexArray(s.vao);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,s.tex0);glBindSampler(0,static_cast<GLuint>(s.sampler0));
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,s.tex1);glBindSampler(1,static_cast<GLuint>(s.sampler1));glActiveTexture(s.active);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s.readFbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s.drawFbo);
    glViewport(s.viewport[0],s.viewport[1],s.viewport[2],s.viewport[3]);glScissor(s.scissor[0],s.scissor[1],s.scissor[2],s.scissor[3]);
    en(GL_BLEND,s.blend);en(GL_DEPTH_TEST,s.depth);en(GL_STENCIL_TEST,s.stencil);en(GL_CULL_FACE,s.cull);en(GL_DITHER,s.dither);en(GL_SCISSOR_TEST,s.scissorEnabled);
    glColorMask(s.mask[0],s.mask[1],s.mask[2],s.mask[3]);
}

bool framebufferForTexture(GlState& gl,GLuint texture,GLuint& framebuffer){
    if(!gl.pendingFramebufferDeletes.empty()){
        glDeleteFramebuffers(static_cast<GLsizei>(gl.pendingFramebufferDeletes.size()),
                             gl.pendingFramebufferDeletes.data());
        gl.pendingFramebufferDeletes.clear();
    }
    auto cached=gl.framebuffers.find(texture);
    if(cached!=gl.framebuffers.end()){framebuffer=cached->second;return true;}
    glGenFramebuffers(1,&framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status!=GL_FRAMEBUFFER_COMPLETE){
        emit("reconstruction_fbo_failure","\"status\":"+std::to_string(status)+
             ",\"texture\":"+std::to_string(texture)+",\"sourceTarget\":\"GL_TEXTURE_2D\"");
        glDeleteFramebuffers(1,&framebuffer);framebuffer=0;return false;
    }
    gl.framebuffers.emplace(texture,framebuffer);return true;
}
void forgetFramebuffer(GlState& gl,GLuint texture,bool canDelete=true){
    auto it=gl.framebuffers.find(texture);if(it==gl.framebuffers.end())return;
    GLuint framebuffer=it->second;
    if(framebuffer){if(canDelete)glDeleteFramebuffers(1,&framebuffer);else gl.pendingFramebufferDeletes.push_back(framebuffer);}
    gl.framebuffers.erase(it);
}
bool resolve(GlState& gl,GLuint source,GLuint destination){
    GLuint readFbo{},drawFbo{};
    if(!framebufferForTexture(gl,source,readFbo)||!framebufferForTexture(gl,destination,drawFbo))return false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);
    glBlitFramebuffer(0,0,kSourceExtent,kSourceExtent,0,0,kSourceExtent,kSourceExtent,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    return true;
}
bool sameFovBits(const XrFovf& a,const XrFovf& b){
    return std::memcmp(&a,&b,sizeof(XrFovf))==0;
}
void updateFovCache(GlState& gl,const XrCompositionLayerProjection& full,
                    const XrCompositionLayerProjection& fovea){
    bool unchanged=gl.fovCacheReady;
    for(int eye=0;eye<2&&unchanged;++eye)
        unchanged=sameFovBits(gl.cachedFullFovs[eye],full.views[eye].fov)&&
                  sameFovBits(gl.cachedFoveaFovs[eye],fovea.views[eye].fov);
    if(unchanged)return;
    for(int eye=0;eye<2;++eye){
        gl.cachedFullFovs[eye]=full.views[eye].fov;
        gl.cachedFoveaFovs[eye]=fovea.views[eye].fov;
        const auto& fullFov=full.views[eye].fov;const auto& foveaFov=fovea.views[eye].fov;
        gl.cachedFullTans[eye]={std::tan(fullFov.angleLeft),std::tan(fullFov.angleRight),
                               std::tan(fullFov.angleDown),std::tan(fullFov.angleUp)};
        gl.cachedFoveaTans[eye]={std::tan(foveaFov.angleLeft),std::tan(foveaFov.angleRight),
                                std::tan(foveaFov.angleDown),std::tan(foveaFov.angleUp)};
    }
    gl.fovCacheReady=true;
}

bool sourceTextureState(const SwapchainState& state,GLuint& texture){
    if(state.acquired.empty())return false;
    uint32_t index=state.acquired.front();if(index>=state.images.size())return false;
    texture=state.images[index].image;return true;
}

bool sourceTexture(SessionState& s,size_t sourceIndex,XrSwapchain handle,GLuint& texture){
#ifdef GXR_NATIVE_RENDERER_HELPER
    const NativeSourceBinding& binding=s.nativeSources[sourceIndex];
    return binding.handle==handle&&binding.state&&binding.state->generation==binding.generation&&
        sourceTextureState(*binding.state,texture);
#else
    (void)s;(void)sourceIndex;
    auto it=swapchains.find(handle);return it!=swapchains.end()&&sourceTextureState(it->second,texture);
#endif
}

uint32_t deferredMask(const SessionState& s){
    uint32_t mask=0;
#ifdef GXR_NATIVE_RENDERER_HELPER
    for(size_t i=0;i<s.nativeSources.size();++i){const auto& binding=s.nativeSources[i];
        if(binding.state&&binding.state->generation==binding.generation&&binding.state->deferred)mask|=1u<<i;}
#else
    for(size_t i=0;i<s.sources.size();++i){auto it=swapchains.find(s.sources[i]);if(it!=swapchains.end()&&it->second.deferred)mask|=1u<<i;}
#endif
    return mask;
}

bool compose(SessionState& s,const XrCompositionLayerProjection& under,const XrCompositionLayerProjection& fovea){
    if(eglGetCurrentContext()!=s.context||eglGetCurrentDisplay()!=s.display){emit("reconstruction_passthrough","\"session\":"+std::to_string(hv(s.session))+",\"everActivated\":"+std::string(s.everActivated?"true":"false")+",\"reason\":\"egl_context_mismatch\"");return false;}
    SavedGl saved=saveGl();
    if(!ensureGl(s)){restoreGl(saved);return false;}bool ok=true;
    s.gl.ditherWasEnabled=saved.dither;s.gl.ditherStateObserved=true;
    if(!s.everActivated)emit("reconstruction_dither_state","\"fixedFunctionDitherWasEnabled\":"+
        std::string(s.gl.ditherWasEnabled?"true":"false")+",\"fixedFunctionDitherDisabledForDraw\":true");
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glUseProgram(s.gl.program);glBindVertexArray(s.gl.vao);glBindSampler(0,0);glBindSampler(1,0);
    glUniform1i(s.gl.underTexLocation,0);glUniform1i(s.gl.foveaTexLocation,1);
    updateFovCache(s.gl,under,fovea);
    for(uint32_t eye=0;eye<2&&ok;++eye){
        GLuint u{},f{};ok=sourceTexture(s,2+eye,under.views[eye].subImage.swapchain,u)&&
            sourceTexture(s,4+eye,fovea.views[eye].subImage.swapchain,f);
        if(!ok)break;ok=resolve(s.gl,u,s.gl.resolved[0])&&resolve(s.gl,f,s.gl.resolved[1]);if(!ok)break;
        auto& out=s.outputs[eye];if(!out.acquired||out.index>=out.images.size()){ok=false;break;}
        GLuint outputFbo{};if(!framebufferForTexture(s.gl,out.images[out.index].image,outputFbo)){ok=false;break;}
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,outputFbo);
#ifdef GXR_DIAGNOSTIC_PROBE
        if(!s.probeOutputStorageEmitted[eye]){
            emitGlStorageProbe("output",static_cast<int>(eye),out.images[out.index].image,
                GL_DRAW_FRAMEBUFFER,glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER),s.sourceFormat,
                s.outputWidth,s.outputHeight);
            s.probeOutputStorageEmitted[eye]=true;
        }
#endif
        glViewport(0,0,s.outputWidth,s.outputHeight);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,s.gl.resolved[0]);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,s.gl.resolved[1]);
        const auto& fullTan=s.gl.cachedFullTans[eye];const auto& foveaTan=s.gl.cachedFoveaTans[eye];
        glUniform4fv(s.gl.fullTanLocation,1,fullTan.data());
        glUniform4fv(s.gl.foveaTanLocation,1,foveaTan.data());
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    if(ok)glFlush();restoreGl(saved);return ok;
}

bool flushSources(SessionState& s){
    bool ok=true;for(size_t i=0;i<s.sources.size();++i){XrSwapchain handle=s.sources[i];SwapchainState* state{};
#ifdef GXR_NATIVE_RENDERER_HELPER
        const auto& binding=s.nativeSources[i];
        if(binding.handle==handle&&binding.state&&binding.state->generation==binding.generation)state=binding.state;
#else
        auto it=swapchains.find(handle);if(it!=swapchains.end())state=&it->second;
#endif
        if(!state||!state->deferred)continue;
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XrResult r=g.releaseImage(handle,&ri);ok&=XR_SUCCEEDED(r);
        if(XR_SUCCEEDED(r)){state->deferred=false;state->waited=false;if(!state->acquired.empty())state->acquired.pop_front();
#ifdef GXR_NATIVE_RENDERER_HELPER
            ++s.nativeForwardedReleaseCount;
#endif
        }
        if(XR_FAILED(r)||sample(frameCounter.load()))emit("source_release_forwarded","\"swapchain\":"+std::to_string(hv(handle))+",\"result\":"+std::to_string(r));}
    return ok;
}
bool releaseOutputs(SessionState& s){bool ok=true;for(auto& out:s.outputs)if(out.acquired){XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(!out.waited){ok=false;emit("output_release","\"result\":"+std::to_string(XR_ERROR_CALL_ORDER_INVALID)+",\"reason\":\"not_waited\"");continue;}XrResult r=g.releaseImage(out.handle,&ri);
    if(XR_FAILED(r)||sample(frameCounter.load()))emit("output_release","\"result\":"+std::to_string(r));ok&=XR_SUCCEEDED(r);if(XR_SUCCEEDED(r)){out.acquired=false;out.waited=false;
#ifdef GXR_NATIVE_RENDERER_HELPER
        ++s.nativeOutputReleaseCount;
#endif
    }}return ok;}
bool acquireOutputs(SessionState& s){for(auto& out:s.outputs){XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(g.acquireImage(out.handle,&ai,&out.index))){releaseOutputs(s);return false;}out.acquired=true;out.waited=false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;if(XR_FAILED(g.waitImage(out.handle,&wi))){s.disabled=true;s.active=false;emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s.session))+",\"reason\":\"output_wait_failed\"");return false;}out.waited=true;}return true;}
void destroyOutputs(SessionState& s){s.mapping.valid=false;releaseOutputs(s);for(auto& out:s.outputs){
    if(eglGetCurrentContext()==s.context)for(const auto& image:out.images)forgetFramebuffer(s.gl,image.image);
    if(out.handle&&g.destroySwapchain&&!out.acquired)g.destroySwapchain(out.handle);out={};}
    if(eglGetCurrentContext()==s.context)destroyGl(s.gl);else s.gl={};
#ifdef GXR_DIAGNOSTIC_PROBE
    s.probeOutputStorageEmitted={};
    s.probeSubmissionAttemptEmitted=false;s.probeSubmissionSuccessEmitted=false;
#endif
}

void emitSuccessSummary(SessionState& s,uint64_t lastFrame,const char* reason){
    if(s.transformsSinceSummary==0)return;
    const uint64_t lastElapsedMs=elapsedMs();
    emit("single_projection_reconstruction_success_summary",
        "\"session\":"+std::to_string(hv(s.session))+",\"firstFrame\":"+std::to_string(s.summaryFirstFrame)+
        ",\"lastFrame\":"+std::to_string(lastFrame)+
        ",\"firstElapsedMs\":"+std::to_string(s.summaryFirstElapsedMs)+
        ",\"lastElapsedMs\":"+std::to_string(lastElapsedMs)+
        ",\"aggregateTransformCount\":"+std::to_string(s.transformsSinceSummary)+
        ",\"totalSuccessfulTransformCount\":"+std::to_string(s.successfulTransforms)+
        ",\"freshTransformCount\":"+std::to_string(s.freshSinceSummary)+
        ",\"reusedTransformCount\":"+std::to_string(s.reusedSinceSummary)+
#ifdef GXR_NATIVE_RENDERER_HELPER
        ",\"fastFingerprintCount\":"+std::to_string(s.nativeFastFingerprintCount)+
        ",\"slowFingerprintCount\":"+std::to_string(s.nativeSlowFingerprintCount)+
        ",\"sourceCacheInvalidationCount\":"+std::to_string(s.nativeSourceCacheInvalidationCount)+
        ",\"nativeCallsiteCacheMissCount\":"+std::to_string(nativeCallsiteCacheMissCount.load(std::memory_order_relaxed))+
        ",\"deferredReleaseCount\":"+std::to_string(s.nativeDeferredReleaseCount)+
        ",\"forwardedReleaseCount\":"+std::to_string(s.nativeForwardedReleaseCount)+
        ",\"outputReleaseCount\":"+std::to_string(s.nativeOutputReleaseCount)+
#endif
        ",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"forwardedLayerCount\":1,"
        "\"outputProjectionCount\":1,\"sourceViewCount\":6,\"outputViewCount\":2,"
        "\"unsafeLayerCount\":0,\"reconstructed\":true,\"changed\":true,\"releaseSuccess\":true,"
        "\"outputWidth\":"+std::to_string(s.outputWidth)+",\"outputHeight\":"+std::to_string(s.outputHeight)+
        ",\"outputTier\":"+quote(outputTierName(s.outputAttemptTier))+
        ",\"reportedMaxWidth\":"+std::to_string(s.maxWidth)+",\"reportedMaxHeight\":"+std::to_string(s.maxHeight)+
        ",\"aboveReportedMaximum\":"+std::string(s.outputAboveReportedMaximum?"true":"false")+
        ",\"foveaFilter\":\"linear_center_1tap\",\"fixedFunctionDitherDisabled\":true,\"fixedFunctionDitherWasEnabled\":"+
        (s.gl.ditherStateObserved?(s.gl.ditherWasEnabled?std::string("true"):std::string("false")):std::string("null"))+
        ",\"outputQualitySettingsAttached\":"+
        (qualitySettingsEnabled?std::string("true"):std::string("false"))+
        ",\"outputQualitySettingsFlags\":10,\"summaryReason\":"+quote(reason));
    s.transformsSinceSummary=0;s.freshSinceSummary=0;s.reusedSinceSummary=0;
#ifdef GXR_NATIVE_RENDERER_HELPER
    s.nativeFastFingerprintCount=0;s.nativeSlowFingerprintCount=0;
    s.nativeSourceCacheInvalidationCount=0;s.nativeDeferredReleaseCount=0;
    s.nativeForwardedReleaseCount=0;s.nativeOutputReleaseCount=0;
#endif
    s.summaryFirstFrame=0;s.summaryFirstElapsedMs=0;
}

#ifdef GXR_DIAGNOSTIC_PROBE
uint32_t probeCeilToUint32(double value) {
    if (!std::isfinite(value) || value <= 0.0) return 0;
    const double limit = static_cast<double>(std::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(std::ceil(std::min(value, limit)));
}

void emitProbeDensity(SessionState& s,const XrCompositionLayerProjection& full,
                      const XrCompositionLayerProjection& fovea,
                      uint32_t jointRequestedWidth,uint32_t jointRequestedHeight) {
    for(int eye=0;eye<2;++eye){
        const double ratioX=static_cast<double>(spanX(full.views[eye].fov))/spanX(fovea.views[eye].fov);
        const double ratioY=static_cast<double>(spanY(full.views[eye].fov))/spanY(fovea.views[eye].fov);
        const auto& limits=s.probeViews[eye];
        const uint32_t detailWidth=probeCeilToUint32(kSourceExtent*ratioX);
        const uint32_t detailHeight=probeCeilToUint32(kSourceExtent*ratioY);
        const uint32_t requestedWidth=std::max(limits.recommendedImageRectWidth,detailWidth);
        const uint32_t requestedHeight=std::max(limits.recommendedImageRectHeight,detailHeight);
        const uint32_t clampedWidth=limits.maxImageRectWidth?std::min(requestedWidth,limits.maxImageRectWidth):requestedWidth;
        const uint32_t clampedHeight=limits.maxImageRectHeight?std::min(requestedHeight,limits.maxImageRectHeight):requestedHeight;
        s.probeRequestedWidths[eye]=requestedWidth;s.probeRequestedHeights[eye]=requestedHeight;
        s.probeClampedWidths[eye]=clampedWidth;s.probeClampedHeights[eye]=clampedHeight;
        const double mappedFoveaWidth=ratioX>0.0?static_cast<double>(s.outputWidth)/ratioX:0.0;
        const double mappedFoveaHeight=ratioY>0.0?static_cast<double>(s.outputHeight)/ratioY:0.0;
        emit("probe_eye_density",
            "\"eye\":"+std::to_string(eye)+
            ",\"fullSpanX\":"+number(spanX(full.views[eye].fov))+
            ",\"fullSpanY\":"+number(spanY(full.views[eye].fov))+
            ",\"foveaSpanX\":"+number(spanX(fovea.views[eye].fov))+
            ",\"foveaSpanY\":"+number(spanY(fovea.views[eye].fov))+
            ",\"densityRatioX\":"+number(ratioX)+
            ",\"densityRatioY\":"+number(ratioY)+
            ",\"detailPreservingWidth\":"+std::to_string(detailWidth)+
            ",\"detailPreservingHeight\":"+std::to_string(detailHeight)+
            ",\"recommendedWidth\":"+std::to_string(limits.recommendedImageRectWidth)+
            ",\"recommendedHeight\":"+std::to_string(limits.recommendedImageRectHeight)+
            ",\"maxWidth\":"+std::to_string(limits.maxImageRectWidth)+
            ",\"maxHeight\":"+std::to_string(limits.maxImageRectHeight)+
            ",\"requestedWidth\":"+std::to_string(requestedWidth)+
            ",\"requestedHeight\":"+std::to_string(requestedHeight)+
            ",\"clampedWidth\":"+std::to_string(clampedWidth)+
            ",\"clampedHeight\":"+std::to_string(clampedHeight)+
            ",\"jointRequestedWidth\":"+std::to_string(jointRequestedWidth)+
            ",\"jointRequestedHeight\":"+std::to_string(jointRequestedHeight)+
            ",\"submittedWidth\":"+std::to_string(s.outputWidth)+
            ",\"submittedHeight\":"+std::to_string(s.outputHeight)+
            ",\"mappedFoveaWidth\":"+number(mappedFoveaWidth)+
            ",\"mappedFoveaHeight\":"+number(mappedFoveaHeight)+
            ",\"foveaDensityRetentionX\":"+number(mappedFoveaWidth/kSourceExtent)+
            ",\"foveaDensityRetentionY\":"+number(mappedFoveaHeight/kSourceExtent));
    }
}

void probeAboveCapAllocations(SessionState& s,uint32_t jointRequestedWidth,uint32_t jointRequestedHeight) {
    struct Candidate { const char* name; uint32_t width; uint32_t height; };
    const uint32_t plusWidth=s.maxWidth<std::numeric_limits<uint32_t>::max()?s.maxWidth+1:s.maxWidth;
    const uint32_t plusHeight=s.maxHeight<std::numeric_limits<uint32_t>::max()?s.maxHeight+1:s.maxHeight;
    const std::array<Candidate,5> candidates{{
        {"joint_detail_request",jointRequestedWidth,jointRequestedHeight},
        {"left_eye_request",s.probeRequestedWidths[0],s.probeRequestedHeights[0]},
        {"right_eye_request",s.probeRequestedWidths[1],s.probeRequestedHeights[1]},
        {"max_plus_1_width",plusWidth,s.maxHeight},
        {"max_plus_1_height",s.maxWidth,plusHeight},
    }};
    std::vector<std::pair<uint32_t,uint32_t>> attempted;
    const uint64_t safeWidth=std::max<uint64_t>(static_cast<uint64_t>(s.maxWidth)*2,plusWidth);
    const uint64_t safeHeight=std::max<uint64_t>(static_cast<uint64_t>(s.maxHeight)*2,plusHeight);
    for(const auto& candidate:candidates){
        const bool aboveCap=candidate.width>s.maxWidth||candidate.height>s.maxHeight;
        const bool duplicate=std::find(attempted.begin(),attempted.end(),
            std::make_pair(candidate.width,candidate.height))!=attempted.end();
        const bool bounded=candidate.width>0&&candidate.height>0&&candidate.width<=safeWidth&&candidate.height<=safeHeight;
        if(!aboveCap||duplicate||!bounded){
            emit("resolution_probe_candidate",
                "\"candidate\":"+quote(candidate.name)+",\"width\":"+std::to_string(candidate.width)+
                ",\"height\":"+std::to_string(candidate.height)+
                ",\"createResult\":"+std::to_string(XR_ERROR_VALIDATION_FAILURE)+
                ",\"submitAttempted\":false,\"submitResult\":"+
                std::to_string(XR_ERROR_FUNCTION_UNSUPPORTED)+
                ",\"accepted\":false,\"allocationAttempted\":false,\"reason\":"+
                quote(!aboveCap?"not_above_cap":(duplicate?"duplicate":"safety_bound"))+
                ",\"fallbackWidth\":"+std::to_string(s.outputWidth)+
                ",\"fallbackHeight\":"+std::to_string(s.outputHeight));
            continue;
        }
        attempted.emplace_back(candidate.width,candidate.height);
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format=s.sourceFormat;ci.sampleCount=1;ci.width=candidate.width;ci.height=candidate.height;
        ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
        XrSwapchain handle{XR_NULL_HANDLE};
        const XrResult createResult=g.createSwapchain(s.session,&ci,&handle);
        XrResult destroyResult=XR_SUCCESS;
        if(XR_SUCCEEDED(createResult)&&handle!=XR_NULL_HANDLE)destroyResult=g.destroySwapchain(handle);
        emit("resolution_probe_candidate",
            "\"candidate\":"+quote(candidate.name)+",\"width\":"+std::to_string(candidate.width)+
            ",\"height\":"+std::to_string(candidate.height)+",\"attempted\":true,\"format\":"+
            std::to_string(s.sourceFormat)+",\"createResult\":"+std::to_string(createResult)+
            ",\"submitAttempted\":false,\"submitResult\":"+
            std::to_string(XR_ERROR_FUNCTION_UNSUPPORTED)+
            ",\"accepted\":false,\"allocationAttempted\":true,\"allocationAccepted\":"+
            std::string(XR_SUCCEEDED(createResult)?"true":"false")+
            ",\"destroyResult\":"+std::to_string(destroyResult)+
            ",\"fallbackToClamped\":true,\"fallbackWidth\":"+std::to_string(s.outputWidth)+
            ",\"fallbackHeight\":"+std::to_string(s.outputHeight));
    }
}
#endif

bool createOutputs(SessionState& s,const XrCompositionLayerProjection& full,const XrCompositionLayerProjection& fovea){
    s.mapping.valid=false;
    if(!s.recommendedWidth||!s.recommendedHeight||!s.maxWidth||!s.maxHeight){
        emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s.session))+",\"reason\":\"missing_view_limits\"");return false;
    }
    float rx=1,ry=1;for(int eye=0;eye<2;++eye){rx=std::max(rx,spanX(full.views[eye].fov)/spanX(fovea.views[eye].fov));ry=std::max(ry,spanY(full.views[eye].fov)/spanY(fovea.views[eye].fov));}
    uint32_t requestedWidth{},requestedHeight{};
    if(!checkedOutputExtent(std::max<double>(s.recommendedWidth,kSourceExtent*static_cast<double>(rx)),requestedWidth)||
       !checkedOutputExtent(std::max<double>(s.recommendedHeight,kSourceExtent*static_cast<double>(ry)),requestedHeight)){
        emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s.session))+
            ",\"reason\":\"unsafe_density_extent\"");return false;
    }
    const uint32_t reportedWidth=std::min(requestedWidth,s.maxWidth);
    const uint32_t reportedHeight=std::min(requestedHeight,s.maxHeight);
#ifdef GXR_NATIVE_DUAL_FORMAT
    uint32_t formatCount{};
    if(!g.enumerateSwapchainFormats||XR_FAILED(g.enumerateSwapchainFormats(s.session,0,&formatCount,nullptr))||!formatCount){
        emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s.session))+
            ",\"reason\":\"enumerate_output_formats_failed\",\"sourceFormat\":"+std::to_string(s.sourceFormat));
        return false;
    }
    std::vector<int64_t> formats(formatCount);
    if(XR_FAILED(g.enumerateSwapchainFormats(s.session,formatCount,&formatCount,formats.data()))||
       std::find(formats.begin(),formats.end(),s.sourceFormat)==formats.end()){
        emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s.session))+
            ",\"reason\":\"source_format_not_supported_for_output\",\"sourceFormat\":"+std::to_string(s.sourceFormat));
        return false;
    }
#ifdef GXR_DIAGNOSTIC_PROBE
    std::ostringstream formatList;
    formatList << '[';
    for(uint32_t i=0;i<formatCount;++i){if(i)formatList << ',';formatList << formats[i];}
    formatList << ']';
    emit("probe_swapchain_format_census","\"formats\":"+formatList.str()+
        ",\"selectedFormat\":"+std::to_string(s.sourceFormat)+
        ",\"selectedFormatAdvertised\":true");
#endif
#endif
    const uint32_t panelWidth=std::max(reportedWidth,std::min(requestedWidth,kGalaxyXrPanelWidth));
    const uint32_t panelHeight=std::max(reportedHeight,std::min(requestedHeight,kGalaxyXrPanelHeight));
    struct OutputCandidate{uint32_t tier,width,height;};
    const std::array<OutputCandidate,3> candidates{{
        {kDensityPreservingTier,requestedWidth,requestedHeight},
        {kPanelNativeTier,panelWidth,panelHeight},
        {kReportedMaximumTier,reportedWidth,reportedHeight},
    }};
    auto tryCreatePair=[&](uint32_t width,uint32_t height,std::string& failedStage)->XrResult{
        for(auto& out:s.outputs){XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
#ifdef GXR_NATIVE_DUAL_FORMAT
            ci.format=s.sourceFormat;
#else
            ci.format=kSourceFormat;
#endif
            ci.sampleCount=1;ci.width=width;ci.height=height;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
            XrResult result=g.createSwapchain(s.session,&ci,&out.handle);
            if(XR_FAILED(result)){failedStage="create";destroyOutputs(s);return result;}
            uint32_t count{};result=g.enumerateImages(out.handle,0,&count,nullptr);
            if(XR_FAILED(result)||!count){failedStage="enumerate_count";destroyOutputs(s);return XR_FAILED(result)?result:XR_ERROR_RUNTIME_FAILURE;}
            out.images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            result=g.enumerateImages(out.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(out.images.data()));
            if(XR_FAILED(result)){failedStage="enumerate_images";destroyOutputs(s);return result;}
        }
        return XR_SUCCESS;
    };
    bool created=false;std::pair<uint32_t,uint32_t> lastAttempt{};bool haveLastAttempt=false;
    for(const auto& candidate:candidates){
        if(candidate.tier<s.outputAttemptTier)continue;
        const std::pair<uint32_t,uint32_t> dimensions{candidate.width,candidate.height};
        if(haveLastAttempt&&dimensions==lastAttempt){s.outputAttemptTier=candidate.tier+1;continue;}
        haveLastAttempt=true;lastAttempt=dimensions;s.outputWidth=candidate.width;s.outputHeight=candidate.height;
        std::string failedStage;const XrResult result=tryCreatePair(candidate.width,candidate.height,failedStage);
        emit("reconstruction_output_attempt","\"session\":"+std::to_string(hv(s.session))+
            ",\"tier\":"+quote(outputTierName(candidate.tier))+
            ",\"width\":"+std::to_string(candidate.width)+",\"height\":"+std::to_string(candidate.height)+
            ",\"reportedMaxWidth\":"+std::to_string(s.maxWidth)+",\"reportedMaxHeight\":"+std::to_string(s.maxHeight)+
            ",\"aboveReportedMaximum\":"+std::string(candidate.width>s.maxWidth||candidate.height>s.maxHeight?"true":"false")+
            ",\"result\":"+std::to_string(result)+",\"failedStage\":"+quote(failedStage));
        if(XR_SUCCEEDED(result)){s.outputAttemptTier=candidate.tier;s.outputTierAccepted=false;created=true;break;}
        s.outputAttemptTier=candidate.tier+1;
    }
    if(!created){
        emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s.session))+
            ",\"reason\":\"all_output_tiers_rejected\"");return false;
    }
    s.outputAboveReportedMaximum=s.outputWidth>s.maxWidth||s.outputHeight>s.maxHeight;
#ifdef GXR_DIAGNOSTIC_PROBE
    emitProbeDensity(s,full,fovea,requestedWidth,requestedHeight);
    probeAboveCapAllocations(s,requestedWidth,requestedHeight);
#endif
    emit("reconstruction_outputs_created","\"width\":"+std::to_string(s.outputWidth)+",\"height\":"+std::to_string(s.outputHeight)+
        ",\"requestedWidth\":"+std::to_string(requestedWidth)+",\"requestedHeight\":"+std::to_string(requestedHeight)+
        ",\"outputTier\":"+quote(outputTierName(s.outputAttemptTier))+
        ",\"reportedMaxWidth\":"+std::to_string(s.maxWidth)+",\"reportedMaxHeight\":"+std::to_string(s.maxHeight)+
        ",\"aboveReportedMaximum\":"+std::string(s.outputAboveReportedMaximum?"true":"false")+
        ",\"widthClamped\":"+std::string(s.outputWidth<requestedWidth?"true":"false")+",\"heightClamped\":"+
        std::string(s.outputHeight<requestedHeight?"true":"false")+
#ifdef GXR_NATIVE_DUAL_FORMAT
        ",\"sourceFormat\":"+std::to_string(s.sourceFormat)+",\"scratchFormat\":"+std::to_string(s.sourceFormat)+
        ",\"outputFormat\":"+std::to_string(s.sourceFormat)+",\"sourcePrecision\":"+quote(sourcePrecision(s.sourceFormat))+
#endif
        ",\"sampleCount\":1,\"foveaFilter\":\"linear_center_1tap\",\"fixedFunctionDitherDisabled\":true"+
        ",\"qualitySettingsEnabled\":"+(qualitySettingsEnabled?std::string("true"):std::string("false")));return true;
}

struct Fingerprint{bool valid{};std::string reason;XrSession session{XR_NULL_HANDLE};
    std::array<const XrCompositionLayerProjection*,3> p{};std::array<XrSwapchain,6> handles{};
#ifdef GXR_NATIVE_DUAL_FORMAT
    int64_t sourceFormat{};
#endif
#ifdef GXR_NATIVE_RENDERER_HELPER
    std::array<SwapchainState*,6> states{};
    std::array<uint64_t,6> generations{};
#endif
};

bool inspectShape(const XrFrameEndInfo* info,Fingerprint& f){
    if(!info||info->layerCount!=3||!info->layers){f.reason="layer_count";return false;}
    for(int i=0;i<3;++i){if(!info->layers[i]||info->layers[i]->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION){f.reason="non_projection";return false;}
        f.p[i]=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[i]);if(f.p[i]->viewCount!=2||!f.p[i]->views){f.reason="view_count";return false;}
        if(!safeProjectionNext(f.p[i]->next)){f.reason="projection_next";return false;}}
    if(f.p[0]->layerFlags!=0||f.p[1]->layerFlags!=0||f.p[2]->layerFlags!=kFoveaFlags){f.reason="flags";return false;}
    if(f.p[0]->space!=f.p[1]->space||f.p[1]->space!=f.p[2]->space){f.reason="space";return false;}
    return true;
}

#ifdef GXR_NATIVE_RENDERER_HELPER
bool inspectNativeFast(const XrFrameEndInfo* info,SessionState& s,Fingerprint& f){
    if(!s.learned||!s.nativeFingerprintReady||!inspectShape(info,f))return false;
    for(int layer=0;layer<3;++layer)for(int eye=0;eye<2;++eye){
        const size_t index=static_cast<size_t>(layer*2+eye);
        const auto& v=f.p[layer]->views[eye];const auto& r=v.subImage.imageRect;
        const NativeSourceBinding& binding=s.nativeSources[index];
        f.handles[index]=v.subImage.swapchain;
        if(v.next||r.offset.x||r.offset.y||r.extent.width!=kSourceExtent||r.extent.height!=kSourceExtent||
           v.subImage.imageArrayIndex||f.handles[index]!=s.sources[index]||binding.handle!=f.handles[index]||
           !binding.state||binding.state->generation!=binding.generation||binding.state->session!=s.session||
           !samePose(v.pose,f.p[1]->views[eye].pose)||
           !sameFovBits(v.fov,s.nativeFingerprintFovs[index]))return false;
        f.states[index]=binding.state;f.generations[index]=binding.generation;
    }
#ifdef GXR_NATIVE_DUAL_FORMAT
    f.sourceFormat=s.sourceFormat;
#endif
    f.valid=true;f.session=s.session;++s.nativeFastFingerprintCount;return true;
}

Fingerprint inspectSlow(const XrFrameEndInfo* info){
    Fingerprint f;if(!inspectShape(info,f))return f;
    XrSession owner=XR_NULL_HANDLE;
    for(int layer=0;layer<3;++layer)for(int eye=0;eye<2;++eye){auto& v=f.p[layer]->views[eye];auto& r=v.subImage.imageRect;
        if(v.next||r.offset.x||r.offset.y||r.extent.width!=kSourceExtent||r.extent.height!=kSourceExtent||v.subImage.imageArrayIndex){f.reason="subimage";return f;}
        if(!samePose(v.pose,f.p[1]->views[eye].pose)){f.reason="pose";return f;}auto it=swapchains.find(v.subImage.swapchain);
        if(it==swapchains.end()){f.reason="unknown_swapchain";return f;}auto& ci=it->second.info;
        if(ci.width!=kSourceExtent||ci.height!=kSourceExtent||ci.arraySize!=1||ci.sampleCount!=2||
#ifdef GXR_NATIVE_DUAL_FORMAT
            !supportedSourceFormat(ci.format)||
#else
            ci.format!=kSourceFormat||
#endif
            ci.usageFlags!=(XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT)||ci.faceCount!=1||ci.mipCount!=1){f.reason="swapchain_info";return f;}
#ifdef GXR_NATIVE_DUAL_FORMAT
        if(!f.sourceFormat)f.sourceFormat=ci.format;
        else if(f.sourceFormat!=ci.format){f.reason="mixed_source_format";return f;}
#endif
        if(!owner)owner=it->second.session;if(owner!=it->second.session){f.reason="session";return f;}
        const size_t index=static_cast<size_t>(layer*2+eye);f.handles[index]=v.subImage.swapchain;
        f.states[index]=&it->second;f.generations[index]=it->second.generation;
        for(size_t earlier=0;earlier<index;++earlier)if(f.handles[earlier]==f.handles[index]){f.reason="duplicate_swapchain";return f;}}
    if(!sameFov(f.p[0]->views[0].fov,f.p[1]->views[0].fov)||!sameFov(f.p[0]->views[1].fov,f.p[1]->views[1].fov)||
        !finitePositiveFov(f.p[1]->views[0].fov)||!finitePositiveFov(f.p[1]->views[1].fov)||!finitePositiveFov(f.p[2]->views[0].fov)||!finitePositiveFov(f.p[2]->views[1].fov)||
        spanX(f.p[2]->views[0].fov)>=spanX(f.p[1]->views[0].fov)*.95f||spanY(f.p[2]->views[0].fov)>=spanY(f.p[1]->views[0].fov)*.95f||
        spanX(f.p[2]->views[1].fov)>=spanX(f.p[1]->views[1].fov)*.95f||spanY(f.p[2]->views[1].fov)>=spanY(f.p[1]->views[1].fov)*.95f){f.reason="topology";return f;}
    f.valid=true;f.session=owner;return f;
}

bool sameNativeIdentity(const SessionState& s,const Fingerprint& f){
    for(size_t i=0;i<6;++i)if(s.sources[i]!=f.handles[i]||s.nativeSources[i].generation!=f.generations[i])return false;
    return true;
}

void cacheNativeFingerprint(SessionState& s,const Fingerprint& f){
    for(size_t i=0;i<6;++i){
        s.nativeSources[i]={f.handles[i],f.states[i],f.generations[i]};
        s.nativeFingerprintFovs[i]=f.p[i/2]->views[i%2].fov;
    }
    s.nativeFingerprintReady=true;
}

Fingerprint inspect(const XrFrameEndInfo* info,SessionState* s){
    Fingerprint fast;
    if(s&&inspectNativeFast(info,*s,fast))return fast;
    if(s)++s->nativeSlowFingerprintCount;
    Fingerprint slow=inspectSlow(info);
    if(s&&slow.valid&&s->learned&&sameNativeIdentity(*s,slow))cacheNativeFingerprint(*s,slow);
    return slow;
}
#else
Fingerprint inspect(const XrFrameEndInfo* info){
    Fingerprint f;if(!inspectShape(info,f))return f;
    std::unordered_set<XrSwapchain> unique;XrSession owner=XR_NULL_HANDLE;
    for(int layer=0;layer<3;++layer)for(int eye=0;eye<2;++eye){auto& v=f.p[layer]->views[eye];auto& r=v.subImage.imageRect;
        if(v.next||r.offset.x||r.offset.y||r.extent.width!=kSourceExtent||r.extent.height!=kSourceExtent||v.subImage.imageArrayIndex){f.reason="subimage";return f;}
        if(!samePose(v.pose,f.p[1]->views[eye].pose)){f.reason="pose";return f;}auto it=swapchains.find(v.subImage.swapchain);
        if(it==swapchains.end()){f.reason="unknown_swapchain";return f;}auto& ci=it->second.info;
        if(ci.width!=kSourceExtent||ci.height!=kSourceExtent||ci.arraySize!=1||ci.sampleCount!=2||ci.format!=kSourceFormat||
            ci.usageFlags!=(XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT)||ci.faceCount!=1||ci.mipCount!=1){f.reason="swapchain_info";return f;}
        if(!owner)owner=it->second.session;if(owner!=it->second.session){f.reason="session";return f;}unique.insert(v.subImage.swapchain);f.handles[layer*2+eye]=v.subImage.swapchain;}
    if(unique.size()!=6||!sameFov(f.p[0]->views[0].fov,f.p[1]->views[0].fov)||!sameFov(f.p[0]->views[1].fov,f.p[1]->views[1].fov)||
        !finitePositiveFov(f.p[1]->views[0].fov)||!finitePositiveFov(f.p[1]->views[1].fov)||!finitePositiveFov(f.p[2]->views[0].fov)||!finitePositiveFov(f.p[2]->views[1].fov)||
        spanX(f.p[2]->views[0].fov)>=spanX(f.p[1]->views[0].fov)*.95f||spanY(f.p[2]->views[0].fov)>=spanY(f.p[1]->views[0].fov)*.95f||
        spanX(f.p[2]->views[1].fov)>=spanX(f.p[1]->views[1].fov)*.95f||spanY(f.p[2]->views[1].fov)>=spanY(f.p[1]->views[1].fov)*.95f){f.reason="topology";return f;}
    f.valid=true;f.session=owner;return f;
}
#endif

void learn(SessionState& s,const Fingerprint& f){
    s.mapping.valid=false;
    s.sources=f.handles;Role roles[]={Role::BaseL,Role::BaseR,Role::UnderL,Role::UnderR,Role::FoveaL,Role::FoveaR};
#ifdef GXR_NATIVE_RENDERER_HELPER
    cacheNativeFingerprint(s,f);
    for(size_t i=0;i<6;++i)if(s.nativeSources[i].state)s.nativeSources[i].state->role=roles[i];
#else
    for(size_t i=0;i<6;++i)swapchains[s.sources[i]].role=roles[i];
#endif
#ifdef GXR_NATIVE_DUAL_FORMAT
    s.sourceFormat=f.sourceFormat;
#endif
    s.learned=true;
    emit("reconstruction_fingerprint_learned","\"sourceProjectionCount\":3,\"sourceViewCount\":6"
#ifdef GXR_NATIVE_DUAL_FORMAT
        ",\"sourceFormat\":"+std::to_string(s.sourceFormat)+",\"sourcePrecision\":"+quote(sourcePrecision(s.sourceFormat))
#endif
    );
#ifdef GXR_DIAGNOSTIC_PROBE
    static constexpr const char* roleNames[]={"base_left","base_right","under_left","under_right","fovea_left","fovea_right"};
    std::ostringstream contract;
    contract << "\"sources\":[";
    for(size_t i=0;i<f.states.size();++i){
        if(i)contract << ',';
        const auto* state=f.states[i];
        contract << "{\"role\":" << quote(roleNames[i])
                 << ",\"swapchain\":" << hv(f.handles[i])
                 << ",\"width\":" << (state?state->info.width:0)
                 << ",\"height\":" << (state?state->info.height:0)
                 << ",\"sampleCount\":" << (state?state->info.sampleCount:0)
                 << ",\"arraySize\":" << (state?state->info.arraySize:0)
                 << ",\"format\":" << (state?state->info.format:0) << '}';
    }
    contract << "]";
    emit("probe_source_swapchain_contract",contract.str());
#endif
}

void applyStereoViewLimits(SessionState& s,const std::array<XrViewConfigurationView,2>& views){
    s.recommendedWidth=std::max(views[0].recommendedImageRectWidth,views[1].recommendedImageRectWidth);
    s.recommendedHeight=std::max(views[0].recommendedImageRectHeight,views[1].recommendedImageRectHeight);
    s.maxWidth=std::min(views[0].maxImageRectWidth,views[1].maxImageRectWidth);
    s.maxHeight=std::min(views[0].maxImageRectHeight,views[1].maxImageRectHeight);
#ifdef GXR_DIAGNOSTIC_PROBE
    s.probeViews=views;s.probeViewLimitsReady=true;
#endif
}

void publishStereoViewLimits(const std::array<XrViewConfigurationView,2>& views){
    std::lock_guard<std::mutex> lock(publishedStereoViewLimitsMutex);
    const uint64_t generation=publishedStereoViewLimitsGeneration.load(std::memory_order_relaxed);
    publishedStereoViewLimitsGeneration.store(generation+1,std::memory_order_release);
    for(uint32_t eye=0;eye<2;++eye){const size_t base=eye*4;
        publishedStereoViewLimits[base].store(views[eye].recommendedImageRectWidth,std::memory_order_relaxed);
        publishedStereoViewLimits[base+1].store(views[eye].recommendedImageRectHeight,std::memory_order_relaxed);
        publishedStereoViewLimits[base+2].store(views[eye].maxImageRectWidth,std::memory_order_relaxed);
        publishedStereoViewLimits[base+3].store(views[eye].maxImageRectHeight,std::memory_order_relaxed);
    }
    publishedStereoViewLimitsGeneration.store(generation+2,std::memory_order_release);
}

bool applyPublishedStereoViewLimits(SessionState& s){
    for(;;){
        const uint64_t before=publishedStereoViewLimitsGeneration.load(std::memory_order_acquire);
        if(!before||before==s.viewLimitsGeneration)return false;
        if(before&1u)continue;
        std::array<XrViewConfigurationView,2> views{{
            {XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        for(uint32_t eye=0;eye<2;++eye){const size_t base=eye*4;
            views[eye].recommendedImageRectWidth=publishedStereoViewLimits[base].load(std::memory_order_relaxed);
            views[eye].recommendedImageRectHeight=publishedStereoViewLimits[base+1].load(std::memory_order_relaxed);
            views[eye].maxImageRectWidth=publishedStereoViewLimits[base+2].load(std::memory_order_relaxed);
            views[eye].maxImageRectHeight=publishedStereoViewLimits[base+3].load(std::memory_order_relaxed);
        }
        const uint64_t after=publishedStereoViewLimitsGeneration.load(std::memory_order_acquire);
        if(before!=after||(after&1u))continue;
        applyStereoViewLimits(s,views);s.viewLimitsGeneration=after;return true;
    }
}

XrResult XRAPI_PTR layerEnumerateViews(XrInstance instance,XrSystemId system,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrViewConfigurationView* views){
    XrResult r=g.enumerateViews(instance,system,type,cap,count,views);if(XR_SUCCEEDED(r)&&type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO&&count&&views&&cap>=*count&&*count>=2){
        stereoViews[0]=views[0];stereoViews[1]=views[1];haveStereoViews=true;stereoSystemId=system;stereoViewType=type;
        publishStereoViewLimits(stereoViews);emit("view_configuration","\"recommendedWidth\":"+std::to_string(views[0].recommendedImageRectWidth)+
        ",\"recommendedHeight\":"+std::to_string(views[0].recommendedImageRectHeight)+",\"maxWidth\":"+std::to_string(views[0].maxImageRectWidth)+",\"maxHeight\":"+std::to_string(views[0].maxImageRectHeight));
#ifdef GXR_DIAGNOSTIC_PROBE
        for(uint32_t eye=0;eye<2;++eye)emit("view_configuration_eye",
            "\"eye\":"+std::to_string(eye)+
            ",\"viewCount\":"+std::to_string(*count)+
            ",\"recommendedWidth\":"+std::to_string(views[eye].recommendedImageRectWidth)+
            ",\"recommendedHeight\":"+std::to_string(views[eye].recommendedImageRectHeight)+
            ",\"maxWidth\":"+std::to_string(views[eye].maxImageRectWidth)+
            ",\"maxHeight\":"+std::to_string(views[eye].maxImageRectHeight)+
            ",\"recommendedSampleCount\":"+
            std::to_string(views[eye].recommendedSwapchainSampleCount)+
            ",\"maxSampleCount\":"+std::to_string(views[eye].maxSwapchainSampleCount));
#endif
    }return r;}

XrResult XRAPI_PTR layerCreateSession(XrInstance instance,const XrSessionCreateInfo* info,XrSession* session){
    XrResult r=g.createSession(instance,info,session);if(XR_FAILED(r)||!session)return r;SessionState s;s.session=*session;
    auto* next=info?static_cast<const XrBaseInStructure*>(info->next):nullptr;for(int i=0;next&&i<32;++i,next=next->next)if(next->type==XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR){
        auto* b=reinterpret_cast<const XrGraphicsBindingOpenGLESAndroidKHR*>(next);s.display=b->display;s.context=b->context;break;}
    if(haveStereoViews)applyPublishedStereoViewLimits(s);sessions[*session]=s;
    emit("session_created","\"session\":"+std::to_string(hv(*session))+",\"hasGlesBinding\":"+(s.context!=EGL_NO_CONTEXT?"true":"false"));return r;}

XrResult XRAPI_PTR layerCreateSwapchain(XrSession session,const XrSwapchainCreateInfo* info,XrSwapchain* handle){
    XrResult r=g.createSwapchain(session,info,handle);if(XR_SUCCEEDED(r)&&info&&handle){SwapchainState s;s.session=session;s.info=*info;s.info.next=nullptr;swapchains[*handle]=s;
#ifdef GXR_NATIVE_RENDERER_HELPER
        swapchains[*handle].generation=nextSwapchainGeneration.fetch_add(1,std::memory_order_relaxed);
#endif
        emit("create_swapchain","\"swapchain\":"+std::to_string(hv(*handle))+",\"width\":"+std::to_string(info->width)+",\"height\":"+std::to_string(info->height)+",\"sampleCount\":"+std::to_string(info->sampleCount)+",\"format\":"+std::to_string(info->format));}return r;}
XrResult XRAPI_PTR layerEnumerateImages(XrSwapchain handle,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* images){
    XrResult r=g.enumerateImages(handle,cap,count,images);auto it=swapchains.find(handle);if(XR_SUCCEEDED(r)&&it!=swapchains.end()&&count&&images&&cap>=*count){it->second.images.resize(*count);
        for(uint32_t i=0;i<*count;++i)it->second.images[i]=*reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(reinterpret_cast<char*>(images)+i*sizeof(XrSwapchainImageOpenGLESKHR));}return r;}
XrResult XRAPI_PTR layerAcquire(XrSwapchain handle,const XrSwapchainImageAcquireInfo* info,uint32_t* index){auto it=swapchains.find(handle);if(it!=swapchains.end()&&it->second.deferred){auto s=sessions.find(it->second.session);if(s!=sessions.end()){flushSources(s->second);s->second.active=false;s->second.disabled=true;emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s->second.session))+",\"reason\":\"acquire_before_end_frame\"");}}
    XrResult r=g.acquireImage(handle,info,index);if(XR_SUCCEEDED(r)&&it!=swapchains.end()&&index){it->second.acquired.push_back(*index);it->second.waited=false;}return r;}
XrResult XRAPI_PTR layerWait(XrSwapchain handle,const XrSwapchainImageWaitInfo* info){XrResult r=g.waitImage(handle,info);auto it=swapchains.find(handle);if(XR_SUCCEEDED(r)&&it!=swapchains.end())it->second.waited=true;return r;}
XrResult XRAPI_PTR layerRelease(XrSwapchain handle,const XrSwapchainImageReleaseInfo* info){auto it=swapchains.find(handle);if(it!=swapchains.end()&&it->second.role!=Role::Unknown){auto s=sessions.find(it->second.session);
    if(s!=sessions.end()&&s->second.active&&info&&!info->next&&it->second.waited&&it->second.acquired.size()==1&&!it->second.deferred){it->second.deferred=true;
#ifdef GXR_NATIVE_RENDERER_HELPER
        ++s->second.nativeDeferredReleaseCount;
#endif
        if(sample(frameCounter.load()+1))emit("source_release_deferred","\"swapchain\":"+std::to_string(hv(handle))+",\"imageIndex\":"+std::to_string(it->second.acquired.front()));return XR_SUCCESS;}
    if(s!=sessions.end()&&s->second.active){s->second.active=false;s->second.disabled=true;emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s->second.session))+",\"reason\":\"unsupported_release_sequence\"");}}
    XrResult r=g.releaseImage(handle,info);if(XR_SUCCEEDED(r)&&it!=swapchains.end()){it->second.waited=false;if(!it->second.acquired.empty())it->second.acquired.pop_front();}return r;}

XrResult submitPassthrough(XrSession handle,SessionState* state,const XrFrameEndInfo* info,
                           uint64_t frame,const std::string& reason,bool emitTransform){
    const uint32_t mask=state?deferredMask(*state):0;
    const bool sourceReleaseSuccess=!state||flushSources(*state);
    const bool outputReleaseSuccess=!state||releaseOutputs(*state);
    const XrFrameEndInfo* submitted=info;XrFrameEndInfo empty{};
    if(!sourceReleaseSuccess||!outputReleaseSuccess){
        if(state){state->active=false;state->disabled=true;}
        empty=info?*info:XrFrameEndInfo{XR_TYPE_FRAME_END_INFO};empty.layerCount=0;empty.layers=nullptr;submitted=&empty;
        emit("reconstruction_disabled","\"session\":"+std::to_string(hv(handle))+
             ",\"reason\":\"passthrough_release_failed\",\"sourceReleaseSuccess\":"+
             std::string(sourceReleaseSuccess?"true":"false")+",\"outputReleaseSuccess\":"+
             std::string(outputReleaseSuccess?"true":"false"));
    }
    if(emitTransform)emit("single_projection_reconstruction_transform","\"session\":"+std::to_string(hv(handle))+
        ",\"frame\":"+std::to_string(frame)+
        ",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"forwardedLayerCount\":"+
        std::to_string(submitted&&submitted->layerCount==3?3:0)+
        ",\"outputProjectionCount\":0,\"sourceViewCount\":6,\"outputViewCount\":0,\"unsafeLayerCount\":1,"
        "\"reconstructed\":false,\"changed\":false,\"releaseSuccess\":"+
        std::string(sourceReleaseSuccess&&outputReleaseSuccess?"true":"false")+",\"reason\":"+quote(reason)+
        ",\"deferredMask\":"+std::to_string(mask));
    if((state&&state->everActivated)||sample(frame))emit("reconstruction_passthrough","\"session\":"+
        std::to_string(hv(handle))+",\"frame\":"+std::to_string(frame)+",\"everActivated\":"+
        std::string(state&&state->everActivated?"true":"false")+",\"layerCount\":"+
        std::to_string(info?info->layerCount:0)+",\"reason\":"+quote(reason));
    XrResult result=g.endFrame(handle,submitted);
    if(sample(frame)||emitTransform||(state&&state->everActivated))emit("end_frame_result","\"session\":"+
        std::to_string(hv(handle))+",\"frame\":"+std::to_string(frame)+",\"result\":"+std::to_string(result));
    return result;
}

XrResult XRAPI_PTR layerEndFrame(XrSession handle,const XrFrameEndInfo* info){
    uint64_t frame=++frameCounter;auto sit=sessions.find(handle);
#ifdef GXR_NATIVE_RENDERER_HELPER
    Fingerprint f=inspect(info,sit==sessions.end()?nullptr:&sit->second);
#else
    Fingerprint f=inspect(info);
#endif
#ifdef GXR_NATIVE_DUAL_FORMAT
    if(sit!=sessions.end()&&!f.valid&&(f.reason=="swapchain_info"||f.reason=="mixed_source_format")){
        sit->second.disabled=true;sit->second.active=false;
        emit("reconstruction_disabled","\"session\":"+std::to_string(hv(handle))+
            ",\"reason\":"+quote(f.reason));
    }
#endif
    if(sit==sessions.end()||!f.valid||f.session!=handle)
        return submitPassthrough(handle,sit==sessions.end()?nullptr:&sit->second,info,frame,f.reason,false);
    SessionState& s=sit->second;
    if(applyPublishedStereoViewLimits(s))s.viewLimitsDirty=true;
    if(s.viewLimitsDirty){
        flushSources(s);destroyOutputs(s);s.learned=false;s.active=false;s.disabled=false;
        s.outputAttemptTier=kDensityPreservingTier;s.viewLimitsDirty=false;
        emit("recommended_resolution_outputs_invalidated","\"session\":"+std::to_string(hv(handle))+
            ",\"frame\":"+std::to_string(frame)+",\"retryTier\":\"density_preserving\"");
    }
#ifdef GXR_NATIVE_RENDERER_HELPER
    const bool sourceIdentityMatches=s.learned&&sameNativeIdentity(s,f);
#else
    const bool sourceIdentityMatches=s.learned&&s.sources==f.handles;
#endif
    if(!sourceIdentityMatches){const bool sourceIdentityChanged=s.learned;
        if(sourceIdentityChanged)emit("reconstruction_passthrough","\"session\":"+std::to_string(hv(handle))+
            ",\"frame\":"+std::to_string(frame)+",\"everActivated\":"+std::string(s.everActivated?"true":"false")+
            ",\"layerCount\":"+std::to_string(info?info->layerCount:0)+",\"reason\":\"source_identity_changed\"");
        flushSources(s);destroyOutputs(s);learn(s,f);XrResult r=g.endFrame(handle,info);
        if(XR_SUCCEEDED(r)&&createOutputs(s,*f.p[1],*f.p[2]))s.active=true;else{s.disabled=true;s.active=false;}
        emit("end_frame_result","\"session\":"+std::to_string(hv(handle))+",\"frame\":"+std::to_string(frame)+",\"result\":"+std::to_string(r));return r;}
    const uint32_t mask=deferredMask(s);
    const bool reusedOutput=mask==0&&s.active&&!s.disabled&&sameMapping(s,*f.p[1],*f.p[2]);
    const bool poseChanged=reusedOutput&&cachedPoseChanged(s,*f.p[1]);
    if(mask!=0x3fu&&!reusedOutput){if(mask)s.mapping.valid=false;const std::string reason=mask?"partial_source_update":
        (!s.active||s.disabled||!s.mapping.valid?"cached_output_unavailable":"cached_mapping_mismatch");
        return submitPassthrough(handle,&s,info,frame,reason,true);}
    if(!reusedOutput){
        if(!s.active||s.disabled)return submitPassthrough(handle,&s,info,frame,"source_not_ready",true);
        s.mapping.valid=false;
        if(!acquireOutputs(s)){
#ifdef GXR_NATIVE_DUAL_FORMAT
            s.disabled=true;s.active=false;
#endif
            return submitPassthrough(handle,&s,info,frame,"output_acquire_failed",true);
        }
        if(!compose(s,*f.p[1],*f.p[2])){
#ifdef GXR_NATIVE_DUAL_FORMAT
            s.disabled=true;s.active=false;
#endif
            return submitPassthrough(handle,&s,info,frame,"compose_failed",true);
        }
        const bool sourceReleaseSuccess=flushSources(s);
        const bool outputReleaseSuccess=releaseOutputs(s);
        if(!sourceReleaseSuccess||!outputReleaseSuccess){s.active=false;s.disabled=true;
            emit("reconstruction_disabled","\"session\":"+std::to_string(hv(handle))+",\"reason\":\"downstream_release_failed\"");
            emit("single_projection_reconstruction_transform","\"session\":"+std::to_string(hv(handle))+",\"frame\":"+std::to_string(frame)+",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"forwardedLayerCount\":0,\"outputProjectionCount\":0,\"sourceViewCount\":6,\"outputViewCount\":0,\"unsafeLayerCount\":1,\"reconstructed\":false,\"changed\":false,\"releaseSuccess\":false,\"reason\":\"downstream_release_failed\",\"deferredMask\":"+std::to_string(mask));
            XrFrameEndInfo empty=*info;empty.layerCount=0;empty.layers=nullptr;XrResult r=g.endFrame(handle,&empty);
            emit("end_frame_result","\"session\":"+std::to_string(hv(handle))+",\"frame\":"+std::to_string(frame)+",\"result\":"+std::to_string(r));return r;}
    }
    std::array<XrCompositionLayerProjectionView,2> views{};
    for(int eye=0;eye<2;++eye){views[eye]=f.p[1]->views[eye];views[eye].next=nullptr;views[eye].subImage.swapchain=s.outputs[eye].handle;
        views[eye].subImage.imageRect={{0,0},{static_cast<int32_t>(s.outputWidth),static_cast<int32_t>(s.outputHeight)}};views[eye].subImage.imageArrayIndex=0;}
    XrCompositionLayerSettingsFB qualitySettings{XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB};
    qualitySettings.layerFlags=XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB|
        XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB;
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.next=qualitySettingsEnabled?&qualitySettings:nullptr;projection.space=f.p[1]->space;projection.viewCount=2;projection.views=views.data();
    const auto* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);XrFrameEndInfo output=*info;output.layerCount=1;output.layers=&layer;
    XrResult r=g.endFrame(handle,&output);
#ifdef GXR_DIAGNOSTIC_PROBE
    const bool successfulSubmission=XR_SUCCEEDED(r);
    if(!s.probeSubmissionAttemptEmitted||(successfulSubmission&&!s.probeSubmissionSuccessEmitted)){
        if(successfulSubmission)emit("single_projection_reconstruction_transform",
            "\"session\":"+std::to_string(hv(handle))+
            ",\"frame\":"+std::to_string(frame)+
            ",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"forwardedLayerCount\":1,"+
            "\"outputProjectionCount\":1,\"sourceViewCount\":6,\"outputViewCount\":2,"+
            "\"unsafeLayerCount\":0,\"reconstructed\":true,\"changed\":true,\"releaseSuccess\":true,"+
            "\"outputWidth\":"+std::to_string(s.outputWidth)+
            ",\"outputHeight\":"+std::to_string(s.outputHeight)+
            ",\"sourceFormat\":"+std::to_string(s.sourceFormat)+
            ",\"outputFormat\":"+std::to_string(s.sourceFormat)+
            ",\"foveaFilter\":\"linear_center_1tap\",\"result\":"+std::to_string(r));
        emit("probe_submitted_frame",
            "\"session\":"+std::to_string(hv(handle))+
            ",\"frame\":"+std::to_string(frame)+
            ",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"sourceViewCount\":6,"+
            "\"submittedLayerCount\":1,\"submittedProjectionCount\":1,\"submittedViewCount\":2,"+
            "\"leftSwapchain\":"+std::to_string(hv(views[0].subImage.swapchain))+
            ",\"leftRectOffsetX\":"+std::to_string(views[0].subImage.imageRect.offset.x)+
            ",\"leftRectOffsetY\":"+std::to_string(views[0].subImage.imageRect.offset.y)+
            ",\"leftRectWidth\":"+std::to_string(views[0].subImage.imageRect.extent.width)+
            ",\"leftRectHeight\":"+std::to_string(views[0].subImage.imageRect.extent.height)+
            ",\"leftArrayIndex\":"+std::to_string(views[0].subImage.imageArrayIndex)+
            ",\"rightSwapchain\":"+std::to_string(hv(views[1].subImage.swapchain))+
            ",\"rightRectOffsetX\":"+std::to_string(views[1].subImage.imageRect.offset.x)+
            ",\"rightRectOffsetY\":"+std::to_string(views[1].subImage.imageRect.offset.y)+
            ",\"rightRectWidth\":"+std::to_string(views[1].subImage.imageRect.extent.width)+
            ",\"rightRectHeight\":"+std::to_string(views[1].subImage.imageRect.extent.height)+
            ",\"rightArrayIndex\":"+std::to_string(views[1].subImage.imageArrayIndex)+
            ",\"sourceFormat\":"+std::to_string(s.sourceFormat)+
            ",\"scratchFormat\":"+std::to_string(s.sourceFormat)+
            ",\"outputFormat\":"+std::to_string(s.sourceFormat)+
            ",\"sourcePrecision\":"+quote(sourcePrecision(s.sourceFormat))+
            ",\"outputQualitySettingsAttached\":"+std::string(qualitySettingsEnabled?"true":"false")+
            ",\"displayTime\":"+std::to_string(output.displayTime)+
            ",\"environmentBlendMode\":"+std::to_string(output.environmentBlendMode)+
            ",\"result\":"+std::to_string(r));
        if(successfulSubmission)emit("end_frame_result","\"session\":"+std::to_string(hv(handle))+
            ",\"frame\":"+std::to_string(frame)+",\"result\":"+std::to_string(r));
        s.probeSubmissionAttemptEmitted=true;
        if(successfulSubmission)s.probeSubmissionSuccessEmitted=true;
    }
#endif
    if(XR_SUCCEEDED(r)){
        if(!s.outputTierAccepted)emit("reconstruction_output_tier_accepted",
            "\"session\":"+std::to_string(hv(handle))+
            ",\"tier\":"+quote(outputTierName(s.outputAttemptTier))+
            ",\"width\":"+std::to_string(s.outputWidth)+",\"height\":"+std::to_string(s.outputHeight)+
            ",\"reportedMaxWidth\":"+std::to_string(s.maxWidth)+",\"reportedMaxHeight\":"+std::to_string(s.maxHeight)+
            ",\"aboveReportedMaximum\":"+std::string(s.outputAboveReportedMaximum?"true":"false"));
        s.outputTierAccepted=true;s.everActivated=true;if(!reusedOutput)rememberMapping(s,*f.p[1],*f.p[2]);
        if(s.transformsSinceSummary==0){s.summaryFirstFrame=frame;s.summaryFirstElapsedMs=elapsedMs();}
        ++s.successfulTransforms;++s.transformsSinceSummary;
        if(reusedOutput)++s.reusedSinceSummary;else ++s.freshSinceSummary;
        if(s.transformsSinceSummary>=kSuccessSummaryInterval)emitSuccessSummary(s,frame,"interval");
    }else{
        emit("single_projection_reconstruction_transform",
            "\"session\":"+std::to_string(hv(handle))+",\"frame\":"+std::to_string(frame)+
            ",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"forwardedLayerCount\":1,"
            "\"outputProjectionCount\":1,\"sourceViewCount\":6,\"outputViewCount\":2,"
            "\"unsafeLayerCount\":1,\"reconstructed\":true,\"changed\":true,"
            "\"releaseSuccess\":true,\"reason\":\"end_frame_failed\",\"result\":"+std::to_string(r));
        emit("end_frame_result","\"session\":"+std::to_string(hv(handle))+
             ",\"frame\":"+std::to_string(frame)+",\"result\":"+std::to_string(r));
        const bool recoverableOversizeRejection=s.outputAboveReportedMaximum&&
            s.outputAttemptTier<kReportedMaximumTier&&
            (r==XR_ERROR_SWAPCHAIN_RECT_INVALID||r==XR_ERROR_VALIDATION_FAILURE||r==XR_ERROR_LAYER_INVALID);
        if(recoverableOversizeRejection){
            const uint32_t rejectedTier=s.outputAttemptTier;
            s.outputAttemptTier=std::min(kReportedMaximumTier,rejectedTier+1);
            emit("reconstruction_output_tier_rejected",
                "\"session\":"+std::to_string(hv(handle))+
                ",\"frame\":"+std::to_string(frame)+
                ",\"rejectedTier\":"+quote(outputTierName(rejectedTier))+
                ",\"nextTier\":"+quote(outputTierName(s.outputAttemptTier))+
                ",\"width\":"+std::to_string(s.outputWidth)+",\"height\":"+std::to_string(s.outputHeight)+
                ",\"result\":"+std::to_string(r)+
                ",\"frameDiscarded\":true,\"retrySameFrame\":false");
            destroyOutputs(s);s.outputTierAccepted=false;s.learned=false;s.active=false;s.disabled=false;
        }
    }
    return r;
}

void invalidateNativeSourceBinding(SessionState& s,XrSwapchain handle){
#ifdef GXR_NATIVE_RENDERER_HELPER
    bool matched=false;
    for(auto& binding:s.nativeSources)if(binding.handle==handle){binding={};matched=true;}
    if(matched){++s.nativeSourceCacheInvalidationCount;s.nativeFingerprintReady=false;s.learned=false;s.mapping.valid=false;s.active=false;}
#else
    (void)s;(void)handle;
#endif
}

XrResult XRAPI_PTR layerDestroySwapchain(XrSwapchain handle){auto it=swapchains.find(handle);if(it!=swapchains.end()){
    auto s=sessions.find(it->second.session);if(it->second.deferred&&s!=sessions.end())flushSources(s->second);
    if(s!=sessions.end()){
        const bool canDelete=eglGetCurrentContext()==s->second.context&&eglGetCurrentDisplay()==s->second.display;
        for(const auto& image:it->second.images)forgetFramebuffer(s->second.gl,image.image,canDelete);
        invalidateNativeSourceBinding(s->second,handle);
    }
}swapchains.erase(handle);return g.destroySwapchain(handle);}
XrResult XRAPI_PTR layerDestroySession(XrSession handle){auto it=sessions.find(handle);if(it!=sessions.end()){emitSuccessSummary(it->second,frameCounter.load(),"session_destroy");flushSources(it->second);destroyOutputs(it->second);sessions.erase(it);}for(auto si=swapchains.begin();si!=swapchains.end();)if(si->second.session==handle)si=swapchains.erase(si);else++si;return g.destroySession(handle);}
XrResult XRAPI_PTR layerDestroyInstance(XrInstance instance){for(auto& pair:sessions){emitSuccessSummary(pair.second,frameCounter.load(),"instance_destroy");flushSources(pair.second);destroyOutputs(pair.second);}sessions.clear();swapchains.clear();emit("destroy_instance");XrResult r=g.destroyInstance(instance);
#ifdef GXR_NATIVE_RENDERER_HELPER
    nativeDispatchReady.store(false,std::memory_order_release);
#endif
    g={};qualitySettingsEnabled=false;recommendedResolutionEnabled=false;recommendedResolutionAppRequested=false;
    haveStereoViews=false;stereoSystemId=XR_NULL_SYSTEM_ID;
    for(auto& value:publishedStereoViewLimits)value.store(0,std::memory_order_relaxed);
    publishedStereoViewLimitsGeneration.store(0,std::memory_order_release);
#ifdef GXR_DIAGNOSTIC_PROBE
    probeXrFbFoveationAdvertised=false;
#endif
#ifdef GXR_NATIVE_RENDERER_HELPER
    nativeStreamCallsite.store(0,std::memory_order_release);
    nativeExitCallsite.store(0,std::memory_order_release);nativeUnexpectedCallsiteLogged.store(false,std::memory_order_release);
#endif
    return r;}
XrResult XRAPI_PTR layerPollEvent(XrInstance instance,XrEventDataBuffer* data){
    for(;;){
        XrResult r=g.pollEvent(instance,data);
        if(XR_FAILED(r)||!data)return r;
        if(data->type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
            auto* e=reinterpret_cast<XrEventDataSessionStateChanged*>(data);
            emit("session_state_changed","\"state\":"+std::to_string(static_cast<int>(e->state)));
        }
        if(!recommendedResolutionEnabled||
           data->type!=XR_TYPE_EVENT_DATA_RECOMMENDED_RESOLUTION_CHANGED_ANDROID)return r;
        std::array<XrViewConfigurationView,2> updated{{
            {XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        uint32_t count{};
        const XrResult enumerateResult=haveStereoViews&&stereoSystemId!=XR_NULL_SYSTEM_ID?
            g.enumerateViews(g.instance,stereoSystemId,stereoViewType,2,&count,updated.data()):
            XR_ERROR_SYSTEM_INVALID;
        bool changed=false;
        if(XR_SUCCEEDED(enumerateResult)&&count>=2){
            for(uint32_t eye=0;eye<2;++eye)changed=changed||
                updated[eye].recommendedImageRectWidth!=stereoViews[eye].recommendedImageRectWidth||
                updated[eye].recommendedImageRectHeight!=stereoViews[eye].recommendedImageRectHeight||
                updated[eye].maxImageRectWidth!=stereoViews[eye].maxImageRectWidth||
                updated[eye].maxImageRectHeight!=stereoViews[eye].maxImageRectHeight;
            if(changed){stereoViews=updated;publishStereoViewLimits(stereoViews);}
        }
        emit("recommended_resolution_changed","\"result\":"+std::to_string(enumerateResult)+
            ",\"viewCount\":"+std::to_string(count)+",\"changed\":"+
            std::string(changed?"true":"false")+
            (XR_SUCCEEDED(enumerateResult)&&count>=2?
                ",\"recommendedWidth\":"+std::to_string(std::max(updated[0].recommendedImageRectWidth,updated[1].recommendedImageRectWidth))+
                ",\"recommendedHeight\":"+std::to_string(std::max(updated[0].recommendedImageRectHeight,updated[1].recommendedImageRectHeight))+
                ",\"maxWidth\":"+std::to_string(std::min(updated[0].maxImageRectWidth,updated[1].maxImageRectWidth))+
                ",\"maxHeight\":"+std::to_string(std::min(updated[0].maxImageRectHeight,updated[1].maxImageRectHeight)):
                std::string{}));
        if(recommendedResolutionAppRequested)return r;
        *data={XR_TYPE_EVENT_DATA_BUFFER};
    }
}
XrResult XRAPI_PTR layerWaitFrame(XrSession session,const XrFrameWaitInfo* info,XrFrameState* state){XrResult r=g.waitFrame(session,info,state);if(XR_FAILED(r)||sample(frameCounter.load()))emit("wait_frame","\"result\":"+std::to_string(r)+(state?",\"predictedDisplayPeriod\":"+std::to_string(state->predictedDisplayPeriod):""));return r;}

XrResult XRAPI_PTR layerGetInstanceProcAddr(XrInstance instance,const char* name,PFN_xrVoidFunction* fn){
    if(!name||!fn)return XR_ERROR_VALIDATION_FAILURE;
#define ROUTE(n,f) if(std::strcmp(name,n)==0)*fn=reinterpret_cast<PFN_xrVoidFunction>(f)
    ROUTE("xrGetInstanceProcAddr",layerGetInstanceProcAddr);else ROUTE("xrDestroyInstance",layerDestroyInstance);
    else ROUTE("xrEnumerateViewConfigurationViews",layerEnumerateViews);else ROUTE("xrCreateSession",layerCreateSession);else ROUTE("xrDestroySession",layerDestroySession);
    else ROUTE("xrCreateSwapchain",layerCreateSwapchain);else ROUTE("xrDestroySwapchain",layerDestroySwapchain);else ROUTE("xrEnumerateSwapchainImages",layerEnumerateImages);
    else ROUTE("xrAcquireSwapchainImage",layerAcquire);else ROUTE("xrWaitSwapchainImage",layerWait);else ROUTE("xrReleaseSwapchainImage",layerRelease);
    else ROUTE("xrEndFrame",layerEndFrame);else ROUTE("xrPollEvent",layerPollEvent);else ROUTE("xrWaitFrame",layerWaitFrame);
    else return g.getInstanceProcAddr?g.getInstanceProcAddr(instance,name,fn):XR_ERROR_FUNCTION_UNSUPPORTED;
#undef ROUTE
    return XR_SUCCESS;
}

bool runtimeAdvertisesExtension(PFN_xrGetInstanceProcAddr getInstanceProcAddr,const char* extensionName){
    PFN_xrVoidFunction address{};
    if(!getInstanceProcAddr||XR_FAILED(getInstanceProcAddr(XR_NULL_HANDLE,
        "xrEnumerateInstanceExtensionProperties",&address))||!address)return false;
    auto enumerate=reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(address);
    uint32_t count{};
    if(XR_FAILED(enumerate(nullptr,0,&count,nullptr))||!count)return false;
    std::vector<XrExtensionProperties> properties(count,{XR_TYPE_EXTENSION_PROPERTIES});
    if(XR_FAILED(enumerate(nullptr,count,&count,properties.data())))return false;
    for(const auto& property:properties)if(std::strcmp(property.extensionName,extensionName)==0)return true;
    return false;
}

bool runtimeAdvertisesQualitySettings(PFN_xrGetInstanceProcAddr getInstanceProcAddr){
    return runtimeAdvertisesExtension(getInstanceProcAddr,XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME);
}

XrResult XRAPI_PTR layerCreateApiLayerInstance(const XrInstanceCreateInfo* ci,const XrApiLayerCreateInfo* ai,XrInstance* instance){
    if(!ci||!ai||!ai->nextInfo)return XR_ERROR_INITIALIZATION_FAILED;
    bool appEnabled=false,recommendedAppEnabled=false;
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)if(std::strcmp(ci->enabledExtensionNames[i],
        XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME)==0){appEnabled=true;break;}
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)if(std::strcmp(ci->enabledExtensionNames[i],
        XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME)==0){recommendedAppEnabled=true;break;}
    const bool advertised=runtimeAdvertisesQualitySettings(ai->nextInfo->nextGetInstanceProcAddr);
    const bool recommendedAdvertised=runtimeAdvertisesExtension(ai->nextInfo->nextGetInstanceProcAddr,
        XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME);
    std::vector<const char*> extensions;
    extensions.reserve(ci->enabledExtensionCount+2);
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)
        extensions.push_back(ci->enabledExtensionNames[i]);
    const bool appended=advertised&&!appEnabled;
    if(appended)extensions.push_back(XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME);
    const bool recommendedAppended=recommendedAdvertised&&!recommendedAppEnabled;
    if(recommendedAppended)extensions.push_back(XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME);
    XrInstanceCreateInfo patched=*ci;
    if(appended||recommendedAppended){patched.enabledExtensionCount=static_cast<uint32_t>(extensions.size());patched.enabledExtensionNames=extensions.data();}
    XrApiLayerCreateInfo next=*ai;next.nextInfo=ai->nextInfo->next;
    XrResult r=ai->nextInfo->nextCreateApiLayerInstance(&patched,&next,instance);if(XR_FAILED(r))return r;
    qualitySettingsEnabled=appEnabled||appended;recommendedResolutionEnabled=recommendedAppEnabled||recommendedAppended;
    recommendedResolutionAppRequested=recommendedAppEnabled;
    g.instance=*instance;g.getInstanceProcAddr=ai->nextInfo->nextGetInstanceProcAddr;
    load("xrDestroyInstance",g.destroyInstance);load("xrEnumerateViewConfigurationViews",g.enumerateViews);load("xrCreateSession",g.createSession);load("xrDestroySession",g.destroySession);
    load("xrCreateSwapchain",g.createSwapchain);load("xrDestroySwapchain",g.destroySwapchain);load("xrEnumerateSwapchainImages",g.enumerateImages);load("xrAcquireSwapchainImage",g.acquireImage);
    load("xrWaitSwapchainImage",g.waitImage);load("xrReleaseSwapchainImage",g.releaseImage);load("xrEndFrame",g.endFrame);load("xrPollEvent",g.pollEvent);load("xrWaitFrame",g.waitFrame);
    emit("layer_initialized","\"layerName\":"+quote(kLayerName)+",\"reconstructionBuildId\":"+quote(kBuildId)+
        ",\"qualityExtensionAdvertised\":"+(advertised?std::string("true"):std::string("false"))+
        ",\"qualityExtensionAppEnabled\":"+(appEnabled?std::string("true"):std::string("false"))+
        ",\"qualityExtensionAppended\":"+(appended?std::string("true"):std::string("false"))+
        ",\"qualitySettingsEnabled\":"+(qualitySettingsEnabled?std::string("true"):std::string("false"))+
        ",\"recommendedResolutionAdvertised\":"+(recommendedAdvertised?std::string("true"):std::string("false"))+
        ",\"recommendedResolutionAppEnabled\":"+(recommendedAppEnabled?std::string("true"):std::string("false"))+
        ",\"recommendedResolutionAppended\":"+(recommendedAppended?std::string("true"):std::string("false"))+
        ",\"recommendedResolutionEnabled\":"+(recommendedResolutionEnabled?std::string("true"):std::string("false")));return XR_SUCCESS;
}

#ifdef GXR_NATIVE_RENDERER_HELPER
void* nativeLoaderHandle(){
    static void* handle=dlopen("libopenxr_loader.so",RTLD_NOW|RTLD_LOCAL);
    return handle;
}

template <typename T> bool nativeResolve(void* loader,const char* name,T& target){
    target=reinterpret_cast<T>(dlsym(loader,name));
    return target!=nullptr;
}

bool initializeNativeDispatch(){
    if(nativeDispatchReady.load(std::memory_order_acquire))return true;
    std::lock_guard<std::mutex> lock(nativeDispatchMutex);
    if(nativeDispatchReady.load(std::memory_order_relaxed))return true;
    void* loader=nativeLoaderHandle();Dispatch resolved{};
    if(!loader){g={};emit("native_helper_failure","\"reason\":\"openxr_loader_unavailable\"");return false;}
    bool ok=nativeResolve(loader,"xrCreateInstance",resolved.createInstance);
    ok=nativeResolve(loader,"xrGetInstanceProcAddr",resolved.getInstanceProcAddr)&&ok;
    ok=nativeResolve(loader,"xrDestroyInstance",resolved.destroyInstance)&&ok;
    ok=nativeResolve(loader,"xrEnumerateViewConfigurationViews",resolved.enumerateViews)&&ok;
    ok=nativeResolve(loader,"xrCreateSession",resolved.createSession)&&ok;
    ok=nativeResolve(loader,"xrDestroySession",resolved.destroySession)&&ok;
#ifdef GXR_NATIVE_DUAL_FORMAT
    ok=nativeResolve(loader,"xrEnumerateSwapchainFormats",resolved.enumerateSwapchainFormats)&&ok;
#endif
    ok=nativeResolve(loader,"xrCreateSwapchain",resolved.createSwapchain)&&ok;
    ok=nativeResolve(loader,"xrDestroySwapchain",resolved.destroySwapchain)&&ok;
    ok=nativeResolve(loader,"xrEnumerateSwapchainImages",resolved.enumerateImages)&&ok;
    ok=nativeResolve(loader,"xrAcquireSwapchainImage",resolved.acquireImage)&&ok;
    ok=nativeResolve(loader,"xrWaitSwapchainImage",resolved.waitImage)&&ok;
    ok=nativeResolve(loader,"xrReleaseSwapchainImage",resolved.releaseImage)&&ok;
    ok=nativeResolve(loader,"xrEndFrame",resolved.endFrame)&&ok;
    ok=nativeResolve(loader,"xrPollEvent",resolved.pollEvent)&&ok;
    ok=nativeResolve(loader,"xrWaitFrame",resolved.waitFrame)&&ok;
    ok=nativeResolve(loader,"xrRequestExitSession",resolved.requestExitSession)&&ok;
    if(!ok){g={};emit("native_helper_failure","\"reason\":\"openxr_symbol_resolution\"");return false;}
    g=resolved;nativeDispatchReady.store(true,std::memory_order_release);return true;
}

XrResult nativeCreateInstance(const XrInstanceCreateInfo* ci,XrInstance* instance){
    if(!ci||!instance||!initializeNativeDispatch())return XR_ERROR_INITIALIZATION_FAILED;
    bool appEnabled=false,recommendedAppEnabled=false;
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)if(std::strcmp(ci->enabledExtensionNames[i],
        XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME)==0){appEnabled=true;break;}
    const bool advertised=runtimeAdvertisesQualitySettings(g.getInstanceProcAddr);
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)if(std::strcmp(ci->enabledExtensionNames[i],
        XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME)==0){recommendedAppEnabled=true;break;}
    const bool recommendedAdvertised=runtimeAdvertisesExtension(g.getInstanceProcAddr,
        XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME);
#ifdef GXR_DIAGNOSTIC_PROBE
    constexpr const char* foveationExtension="XR_FB_foveation";
    const bool foveationAdvertised=runtimeAdvertisesExtension(g.getInstanceProcAddr,foveationExtension);
    probeXrFbFoveationAdvertised=foveationAdvertised;
    bool foveationAppEnabled=false;
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)
        if(std::strcmp(ci->enabledExtensionNames[i],foveationExtension)==0){foveationAppEnabled=true;break;}
#endif
    std::vector<const char*> extensions;
    extensions.reserve(ci->enabledExtensionCount+2);
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)extensions.push_back(ci->enabledExtensionNames[i]);
    const bool appended=advertised&&!appEnabled;
    if(appended)extensions.push_back(XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME);
    const bool recommendedAppended=recommendedAdvertised&&!recommendedAppEnabled;
    if(recommendedAppended)extensions.push_back(XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME);
    XrInstanceCreateInfo patched=*ci;
    if(appended||recommendedAppended){patched.enabledExtensionCount=static_cast<uint32_t>(extensions.size());patched.enabledExtensionNames=extensions.data();}
    XrResult r=g.createInstance(&patched,instance);
    if(XR_FAILED(r))return r;
    g.instance=*instance;qualitySettingsEnabled=appEnabled||appended;
    recommendedResolutionEnabled=recommendedAppEnabled||recommendedAppended;
    recommendedResolutionAppRequested=recommendedAppEnabled;
    emit("native_helper_initialized","\"helperName\":"+quote(kLayerName)+
        ",\"reconstructionBuildId\":"+quote(kBuildId)+
        ",\"qualityExtensionAdvertised\":"+(advertised?std::string("true"):std::string("false"))+
        ",\"qualityExtensionAppEnabled\":"+(appEnabled?std::string("true"):std::string("false"))+
        ",\"qualityExtensionAppended\":"+(appended?std::string("true"):std::string("false"))+
        ",\"qualitySettingsEnabled\":"+(qualitySettingsEnabled?std::string("true"):std::string("false"))+
        ",\"recommendedResolutionAdvertised\":"+(recommendedAdvertised?std::string("true"):std::string("false"))+
        ",\"recommendedResolutionAppEnabled\":"+(recommendedAppEnabled?std::string("true"):std::string("false"))+
        ",\"recommendedResolutionAppended\":"+(recommendedAppended?std::string("true"):std::string("false"))+
        ",\"recommendedResolutionEnabled\":"+(recommendedResolutionEnabled?std::string("true"):std::string("false")));
#ifdef GXR_DIAGNOSTIC_PROBE
    emit("probe_xr_capability_census",
        "\"xrFbFoveationAdvertised\":"+std::string(foveationAdvertised?"true":"false")+
        ",\"xrFbFoveationAppEnabled\":"+std::string(foveationAppEnabled?"true":"false")+
        ",\"xrFbFoveationExperimentEnabled\":false,"+
        "\"xrFbCompositionLayerSettingsAdvertised\":"+std::string(advertised?"true":"false")+
        ",\"xrFbCompositionLayerSettingsEnabled\":"+std::string(qualitySettingsEnabled?"true":"false"));
#endif
    return r;
}

bool initializeNativeCallsites(const void* returnAddress){
    if(nativeStreamCallsite.load(std::memory_order_acquire))return true;
    std::lock_guard<std::mutex> lock(nativeDispatchMutex);
    if(nativeStreamCallsite.load(std::memory_order_relaxed))return true;
    nativeCallsiteCacheMissCount.fetch_add(1,std::memory_order_relaxed);
    Dl_info caller{};
    if(dladdr(returnAddress,&caller)&&caller.dli_fbase){
        const uintptr_t base=reinterpret_cast<uintptr_t>(caller.dli_fbase);
        const uintptr_t offset=reinterpret_cast<uintptr_t>(returnAddress)-base;
        if(offset!=0x0011AA80u&&offset!=0x00142064u){
            emit("native_helper_failure","\"reason\":\"unexpected_callsite\",\"returnOffset\":"+std::to_string(offset));
            return false;
        }
        nativeExitCallsite.store(base+0x00142064u,std::memory_order_relaxed);
        nativeStreamCallsite.store(base+0x0011AA80u,std::memory_order_release);
        return true;
    }
    emit("native_helper_failure","\"reason\":\"unresolved_callsite\"");return false;
}

XrResult nativeDispatchEndFrame(XrSession session,const XrFrameEndInfo* info,const void* returnAddress){
    if(!initializeNativeDispatch()||!initializeNativeCallsites(returnAddress))return XR_ERROR_RUNTIME_FAILURE;
    const uintptr_t caller=reinterpret_cast<uintptr_t>(returnAddress);
    if(caller==nativeStreamCallsite.load(std::memory_order_relaxed))return layerEndFrame(session,info);
    if(caller==nativeExitCallsite.load(std::memory_order_relaxed))return g.requestExitSession(session);
    if(!nativeUnexpectedCallsiteLogged.exchange(true,std::memory_order_relaxed)){
        const uintptr_t base=nativeStreamCallsite.load(std::memory_order_relaxed)-0x0011AA80u;
        emit("native_helper_failure","\"reason\":\"unexpected_callsite\",\"returnOffset\":"+std::to_string(caller-base));
    }
    return XR_ERROR_RUNTIME_FAILURE;
}
#endif

} // namespace

#ifdef GXR_NATIVE_RENDERER_HELPER
GXR_EXPORT XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* ci,XrInstance* instance){return nativeCreateInstance(ci,instance);}
GXR_EXPORT XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance){return layerDestroyInstance(instance);}
GXR_EXPORT XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance instance,XrSystemId system,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrViewConfigurationView* views){return layerEnumerateViews(instance,system,type,cap,count,views);}
GXR_EXPORT XrResult XRAPI_CALL xrCreateSession(XrInstance instance,const XrSessionCreateInfo* info,XrSession* session){return layerCreateSession(instance,info,session);}
GXR_EXPORT XrResult XRAPI_CALL xrDestroySession(XrSession session){return layerDestroySession(session);}
GXR_EXPORT XrResult XRAPI_CALL xrCreateSwapchain(XrSession session,const XrSwapchainCreateInfo* info,XrSwapchain* swapchain){return layerCreateSwapchain(session,info,swapchain);}
GXR_EXPORT XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain){return layerDestroySwapchain(swapchain);}
GXR_EXPORT XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain swapchain,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* images){return layerEnumerateImages(swapchain,cap,count,images);}
GXR_EXPORT XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain,const XrSwapchainImageAcquireInfo* info,uint32_t* index){return layerAcquire(swapchain,info,index);}
GXR_EXPORT XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain swapchain,const XrSwapchainImageWaitInfo* info){return layerWait(swapchain,info);}
GXR_EXPORT XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain,const XrSwapchainImageReleaseInfo* info){return layerRelease(swapchain,info);}
GXR_EXPORT XrResult XRAPI_CALL xrPollEvent(XrInstance instance,XrEventDataBuffer* data){return layerPollEvent(instance,data);}
GXR_EXPORT __attribute__((noinline)) XrResult XRAPI_CALL gxrEndFrame(XrSession session,const XrFrameEndInfo* info){return nativeDispatchEndFrame(session,info,__builtin_return_address(0));}
#else
GXR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loader,const char* name,XrNegotiateApiLayerRequest* request){
    if(!loader||!name||!request||std::strcmp(name,kLayerName)!=0)return XR_ERROR_INITIALIZATION_FAILED;
    if(loader->maxInterfaceVersion<XR_CURRENT_LOADER_API_LAYER_VERSION||loader->maxApiVersion<XR_CURRENT_API_VERSION)return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion=XR_CURRENT_LOADER_API_LAYER_VERSION;request->layerApiVersion=XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr=layerGetInstanceProcAddr;request->createApiLayerInstance=layerCreateApiLayerInstance;return XR_SUCCESS;
}
#endif
