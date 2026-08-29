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
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>
#include <vector>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))
#ifndef GXR_LAYER_NAME
#define GXR_LAYER_NAME "XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_v1"
#endif

namespace {

constexpr char kLayerName[] = GXR_LAYER_NAME;
constexpr char kLogTag[] = "GXRResolutionTrace";
constexpr char kModeName[] = "single_projection_reconstruction_v1";
constexpr char kBuildId[] = "single-projection-reconstruction-v1.2-20260829";
constexpr uint32_t kSourceExtent = 1536;
constexpr int64_t kSourceFormat = GL_SRGB8_ALPHA8;
constexpr XrCompositionLayerFlags kFoveaFlags =
    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
    XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;

struct Dispatch {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrGetInstanceProcAddr getInstanceProcAddr{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrEnumerateViewConfigurationViews enumerateViews{};
    PFN_xrCreateSession createSession{};
    PFN_xrDestroySession destroySession{};
    PFN_xrCreateSwapchain createSwapchain{};
    PFN_xrDestroySwapchain destroySwapchain{};
    PFN_xrEnumerateSwapchainImages enumerateImages{};
    PFN_xrAcquireSwapchainImage acquireImage{};
    PFN_xrWaitSwapchainImage waitImage{};
    PFN_xrReleaseSwapchainImage releaseImage{};
    PFN_xrEndFrame endFrame{};
    PFN_xrPollEvent pollEvent{};
    PFN_xrWaitFrame waitFrame{};
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
};

struct OutputEye {
    XrSwapchain handle{XR_NULL_HANDLE};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    uint32_t index{};
    bool acquired{};
    bool waited{};
};

struct GlState {
    GLuint program{}, vao{}, readFbo{}, drawFbo{};
    std::array<std::array<GLuint, 2>, 2> resolved{};
    bool ready{};
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
    std::array<XrSwapchain, 6> sources{};
    std::array<OutputEye, 2> outputs{};
    GlState gl;
    ReconstructionMapping mapping;
    bool learned{}, active{}, disabled{}, everActivated{};
};

Dispatch g;
// Steam Link creates/destroys these objects outside the steady-state frame loop.
// std::map keeps element addresses stable while different swapchain elements are
// externally synchronized by OpenXR. No application/runtime call is made under
// a layer-global lock, avoiding lock inversion on the renderer thread.
std::map<XrSwapchain, SwapchainState> swapchains;
std::map<XrSession, SessionState> sessions;
std::array<XrViewConfigurationView, 2> stereoViews{};
bool haveStereoViews{};
bool qualitySettingsEnabled{};
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

bool sample(uint64_t frame) { return frame <= 3 || frame % 90 == 0; }
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

bool ensureGl(SessionState& s) {
    if (s.gl.ready) return true;
    static const char* vertex = R"(#version 310 es
precision highp float;
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);})";
    static const char* fragment = R"(#version 310 es
precision highp float;
in vec2 uv;layout(location=0)out vec4 color;
uniform sampler2D underTex;uniform sampler2D foveaTex;
uniform vec4 fullTan;uniform vec4 foveaTan;
uniform vec2 foveaTexel;
vec4 sampleFovea(vec2 p,vec2 footprint){
 vec2 o=footprint*0.25;
 return 0.25*(texture(foveaTex,clamp(p+vec2(-o.x,-o.y),0.0,1.0))+
  texture(foveaTex,clamp(p+vec2( o.x,-o.y),0.0,1.0))+
  texture(foveaTex,clamp(p+vec2(-o.x, o.y),0.0,1.0))+
  texture(foveaTex,clamp(p+vec2( o.x, o.y),0.0,1.0)));
}
void main(){
 vec2 ray=vec2(mix(fullTan.x,fullTan.y,uv.x),mix(fullTan.z,fullTan.w,uv.y));
 vec2 fuv=vec2((ray.x-foveaTan.x)/(foveaTan.y-foveaTan.x),(ray.y-foveaTan.z)/(foveaTan.w-foveaTan.z));
 vec2 footprint=max(fwidth(fuv),foveaTexel);
 vec4 base=texture(underTex,clamp(uv,vec2(0.0),vec2(1.0)));color=vec4(base.rgb,1.0);
 if(all(greaterThanEqual(fuv,vec2(0.0)))&&all(lessThanEqual(fuv,vec2(1.0)))){
  vec4 inset=sampleFovea(clamp(fuv,vec2(0.0),vec2(1.0)),footprint);color.rgb=mix(color.rgb,inset.rgb,inset.a);
 }
})";
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
    glGenVertexArrays(1,&s.gl.vao); glGenFramebuffers(1,&s.gl.readFbo); glGenFramebuffers(1,&s.gl.drawFbo);
    for(auto& eye:s.gl.resolved){
        glGenTextures(2,eye.data());
        for(GLuint texture:eye){
            glBindTexture(GL_TEXTURE_2D,texture);
            glTexStorage2D(GL_TEXTURE_2D,1,GL_SRGB8_ALPHA8,kSourceExtent,kSourceExtent);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
    }
    s.gl.ready=true;
    return s.gl.ready;
}

void destroyGl(GlState& gl) {
    if(gl.program)glDeleteProgram(gl.program); if(gl.vao)glDeleteVertexArrays(1,&gl.vao);
    if(gl.readFbo)glDeleteFramebuffers(1,&gl.readFbo); if(gl.drawFbo)glDeleteFramebuffers(1,&gl.drawFbo);
    for(auto& eye:gl.resolved)glDeleteTextures(2,eye.data()); gl={};
}

struct SavedGl {
    GLint program{},vao{},active{},tex0{},tex1{},sampler0{},sampler1{},readFbo{},drawFbo{},viewport[4]{},scissor[4]{};
    GLboolean blend{},depth{},stencil{},cull{},scissorEnabled{},mask[4]{};
};
SavedGl saveGl(){
    SavedGl s;glGetIntegerv(GL_CURRENT_PROGRAM,&s.program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&s.vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&s.active);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&s.tex0);
    glGetIntegerv(GL_SAMPLER_BINDING,&s.sampler0);glActiveTexture(GL_TEXTURE1);glGetIntegerv(GL_TEXTURE_BINDING_2D,&s.tex1);
    glGetIntegerv(GL_SAMPLER_BINDING,&s.sampler1);glActiveTexture(s.active);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&s.readFbo);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&s.drawFbo);
    glGetIntegerv(GL_VIEWPORT,s.viewport);glGetIntegerv(GL_SCISSOR_BOX,s.scissor);
    s.blend=glIsEnabled(GL_BLEND);s.depth=glIsEnabled(GL_DEPTH_TEST);s.stencil=glIsEnabled(GL_STENCIL_TEST);
    s.cull=glIsEnabled(GL_CULL_FACE);s.scissorEnabled=glIsEnabled(GL_SCISSOR_TEST);glGetBooleanv(GL_COLOR_WRITEMASK,s.mask);return s;
}
void restoreGl(const SavedGl& s){
    auto en=[](GLenum c,GLboolean v){v?glEnable(c):glDisable(c);};glUseProgram(s.program);glBindVertexArray(s.vao);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,s.tex0);glBindSampler(0,static_cast<GLuint>(s.sampler0));
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,s.tex1);glBindSampler(1,static_cast<GLuint>(s.sampler1));glActiveTexture(s.active);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s.readFbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s.drawFbo);
    glViewport(s.viewport[0],s.viewport[1],s.viewport[2],s.viewport[3]);glScissor(s.scissor[0],s.scissor[1],s.scissor[2],s.scissor[3]);
    en(GL_BLEND,s.blend);en(GL_DEPTH_TEST,s.depth);en(GL_STENCIL_TEST,s.stencil);en(GL_CULL_FACE,s.cull);en(GL_SCISSOR_TEST,s.scissorEnabled);
    glColorMask(s.mask[0],s.mask[1],s.mask[2],s.mask[3]);
}

bool resolve(GLuint source,GLuint destination,GLuint readFbo,GLuint drawFbo){
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,drawFbo);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,destination,0);
    const GLenum drawStatus=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source,0);
    const GLenum readStatus=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    if(drawStatus!=GL_FRAMEBUFFER_COMPLETE||readStatus!=GL_FRAMEBUFFER_COMPLETE){
        emit("reconstruction_fbo_failure","\"readStatus\":"+std::to_string(readStatus)+",\"drawStatus\":"+std::to_string(drawStatus)+",\"sourceTarget\":\"GL_TEXTURE_2D\"");
        return false;
    }
    glBlitFramebuffer(0,0,kSourceExtent,kSourceExtent,0,0,kSourceExtent,kSourceExtent,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    return true;
}

bool sourceTexture(XrSwapchain handle,GLuint& texture){
    auto it=swapchains.find(handle);if(it==swapchains.end()||it->second.acquired.empty())return false;
    uint32_t index=it->second.acquired.front();if(index>=it->second.images.size())return false;
    texture=it->second.images[index].image;return true;
}

uint32_t deferredMask(const SessionState& s){
    uint32_t mask=0;for(size_t i=0;i<s.sources.size();++i){auto it=swapchains.find(s.sources[i]);if(it!=swapchains.end()&&it->second.deferred)mask|=1u<<i;}return mask;
}

bool compose(SessionState& s,const XrCompositionLayerProjection& under,const XrCompositionLayerProjection& fovea){
    if(eglGetCurrentContext()!=s.context||eglGetCurrentDisplay()!=s.display){emit("reconstruction_passthrough","\"session\":"+std::to_string(hv(s.session))+",\"everActivated\":"+std::string(s.everActivated?"true":"false")+",\"reason\":\"egl_context_mismatch\"");return false;}
    SavedGl saved=saveGl();
    if(!ensureGl(s)){restoreGl(saved);return false;}bool ok=true;
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glUseProgram(s.gl.program);glBindVertexArray(s.gl.vao);glBindSampler(0,0);glBindSampler(1,0);
    glUniform1i(glGetUniformLocation(s.gl.program,"underTex"),0);glUniform1i(glGetUniformLocation(s.gl.program,"foveaTex"),1);
    glUniform2f(glGetUniformLocation(s.gl.program,"foveaTexel"),1.0f/kSourceExtent,1.0f/kSourceExtent);
    for(uint32_t eye=0;eye<2&&ok;++eye){
        GLuint u{},f{};ok=sourceTexture(under.views[eye].subImage.swapchain,u)&&sourceTexture(fovea.views[eye].subImage.swapchain,f);
        if(!ok)break;ok=resolve(u,s.gl.resolved[eye][0],s.gl.readFbo,s.gl.drawFbo)&&resolve(f,s.gl.resolved[eye][1],s.gl.readFbo,s.gl.drawFbo);if(!ok)break;
        auto& out=s.outputs[eye];if(!out.acquired||out.index>=out.images.size()){ok=false;break;}
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s.gl.drawFbo);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,out.images[out.index].image,0);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){ok=false;break;}
        glViewport(0,0,s.outputWidth,s.outputHeight);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,s.gl.resolved[eye][0]);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,s.gl.resolved[eye][1]);auto& full=under.views[eye].fov;auto& inset=fovea.views[eye].fov;
        glUniform4f(glGetUniformLocation(s.gl.program,"fullTan"),std::tan(full.angleLeft),std::tan(full.angleRight),std::tan(full.angleDown),std::tan(full.angleUp));
        glUniform4f(glGetUniformLocation(s.gl.program,"foveaTan"),std::tan(inset.angleLeft),std::tan(inset.angleRight),std::tan(inset.angleDown),std::tan(inset.angleUp));
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    if(ok)glFlush();restoreGl(saved);return ok;
}

bool flushSources(SessionState& s){
    bool ok=true;for(XrSwapchain handle:s.sources){auto it=swapchains.find(handle);if(it==swapchains.end()||!it->second.deferred)continue;
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XrResult r=g.releaseImage(handle,&ri);ok&=XR_SUCCEEDED(r);
        if(XR_SUCCEEDED(r)){it->second.deferred=false;it->second.waited=false;if(!it->second.acquired.empty())it->second.acquired.pop_front();}
        if(XR_FAILED(r)||sample(frameCounter.load()))emit("source_release_forwarded","\"swapchain\":"+std::to_string(hv(handle))+",\"result\":"+std::to_string(r));}
    return ok;
}
bool releaseOutputs(SessionState& s){bool ok=true;for(auto& out:s.outputs)if(out.acquired){XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(!out.waited){ok=false;emit("output_release","\"result\":"+std::to_string(XR_ERROR_CALL_ORDER_INVALID)+",\"reason\":\"not_waited\"");continue;}XrResult r=g.releaseImage(out.handle,&ri);
    if(XR_FAILED(r)||sample(frameCounter.load()))emit("output_release","\"result\":"+std::to_string(r));ok&=XR_SUCCEEDED(r);if(XR_SUCCEEDED(r)){out.acquired=false;out.waited=false;}}return ok;}
bool acquireOutputs(SessionState& s){for(auto& out:s.outputs){XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(g.acquireImage(out.handle,&ai,&out.index))){releaseOutputs(s);return false;}out.acquired=true;out.waited=false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;if(XR_FAILED(g.waitImage(out.handle,&wi))){s.disabled=true;s.active=false;emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s.session))+",\"reason\":\"output_wait_failed\"");return false;}out.waited=true;}return true;}
void destroyOutputs(SessionState& s){s.mapping.valid=false;releaseOutputs(s);for(auto& out:s.outputs){if(out.handle&&g.destroySwapchain&&!out.acquired)g.destroySwapchain(out.handle);out={};}
    if(eglGetCurrentContext()==s.context)destroyGl(s.gl);else s.gl={};}

bool createOutputs(SessionState& s,const XrCompositionLayerProjection& full,const XrCompositionLayerProjection& fovea){
    s.mapping.valid=false;
    if(!s.recommendedWidth||!s.recommendedHeight||!s.maxWidth||!s.maxHeight){
        emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s.session))+",\"reason\":\"missing_view_limits\"");return false;
    }
    float rx=1,ry=1;for(int eye=0;eye<2;++eye){rx=std::max(rx,spanX(full.views[eye].fov)/spanX(fovea.views[eye].fov));ry=std::max(ry,spanY(full.views[eye].fov)/spanY(fovea.views[eye].fov));}
    const uint32_t requestedWidth=std::max(s.recommendedWidth,static_cast<uint32_t>(std::ceil(kSourceExtent*rx)));
    const uint32_t requestedHeight=std::max(s.recommendedHeight,static_cast<uint32_t>(std::ceil(kSourceExtent*ry)));
    s.outputWidth=requestedWidth;s.outputHeight=requestedHeight;
    if(s.maxWidth)s.outputWidth=std::min(s.outputWidth,s.maxWidth);if(s.maxHeight)s.outputHeight=std::min(s.outputHeight,s.maxHeight);
    for(auto& out:s.outputs){XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format=kSourceFormat;ci.sampleCount=1;ci.width=s.outputWidth;ci.height=s.outputHeight;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
        if(XR_FAILED(g.createSwapchain(s.session,&ci,&out.handle))){destroyOutputs(s);return false;}uint32_t count{};
        if(XR_FAILED(g.enumerateImages(out.handle,0,&count,nullptr))||!count){destroyOutputs(s);return false;}
        out.images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        if(XR_FAILED(g.enumerateImages(out.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(out.images.data())))){destroyOutputs(s);return false;}}
    emit("reconstruction_outputs_created","\"width\":"+std::to_string(s.outputWidth)+",\"height\":"+std::to_string(s.outputHeight)+
        ",\"requestedWidth\":"+std::to_string(requestedWidth)+",\"requestedHeight\":"+std::to_string(requestedHeight)+
        ",\"widthClamped\":"+std::string(s.outputWidth<requestedWidth?"true":"false")+",\"heightClamped\":"+
        std::string(s.outputHeight<requestedHeight?"true":"false")+
        ",\"sampleCount\":1,\"foveaFilter\":\"linear_4tap_subpixel_box\""+
        ",\"qualitySettingsEnabled\":"+(qualitySettingsEnabled?std::string("true"):std::string("false")));return true;
}

struct Fingerprint{bool valid{};std::string reason;XrSession session{XR_NULL_HANDLE};
    std::array<const XrCompositionLayerProjection*,3> p{};std::array<XrSwapchain,6> handles{};};
Fingerprint inspect(const XrFrameEndInfo* info){
    Fingerprint f;if(!info||info->layerCount!=3||!info->layers){f.reason="layer_count";return f;}
    for(int i=0;i<3;++i){if(!info->layers[i]||info->layers[i]->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION){f.reason="non_projection";return f;}
        f.p[i]=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[i]);if(f.p[i]->viewCount!=2||!f.p[i]->views){f.reason="view_count";return f;}
        if(!safeProjectionNext(f.p[i]->next)){f.reason="projection_next";return f;}}
    if(f.p[0]->layerFlags!=0||f.p[1]->layerFlags!=0||f.p[2]->layerFlags!=kFoveaFlags){f.reason="flags";return f;}
    if(f.p[0]->space!=f.p[1]->space||f.p[1]->space!=f.p[2]->space){f.reason="space";return f;}
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

void learn(SessionState& s,const Fingerprint& f){
    s.mapping.valid=false;
    s.sources=f.handles;Role roles[]={Role::BaseL,Role::BaseR,Role::UnderL,Role::UnderR,Role::FoveaL,Role::FoveaR};
    for(size_t i=0;i<6;++i)swapchains[s.sources[i]].role=roles[i];s.learned=true;
    emit("reconstruction_fingerprint_learned","\"sourceProjectionCount\":3,\"sourceViewCount\":6");
}

XrResult XRAPI_PTR layerEnumerateViews(XrInstance instance,XrSystemId system,XrViewConfigurationType type,uint32_t cap,uint32_t* count,XrViewConfigurationView* views){
    XrResult r=g.enumerateViews(instance,system,type,cap,count,views);if(XR_SUCCEEDED(r)&&type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO&&count&&views&&cap>=*count&&*count>=2){
        stereoViews[0]=views[0];stereoViews[1]=views[1];haveStereoViews=true;emit("view_configuration","\"recommendedWidth\":"+std::to_string(views[0].recommendedImageRectWidth)+
        ",\"recommendedHeight\":"+std::to_string(views[0].recommendedImageRectHeight)+",\"maxWidth\":"+std::to_string(views[0].maxImageRectWidth)+",\"maxHeight\":"+std::to_string(views[0].maxImageRectHeight));}return r;}

XrResult XRAPI_PTR layerCreateSession(XrInstance instance,const XrSessionCreateInfo* info,XrSession* session){
    XrResult r=g.createSession(instance,info,session);if(XR_FAILED(r)||!session)return r;SessionState s;s.session=*session;
    auto* next=info?static_cast<const XrBaseInStructure*>(info->next):nullptr;for(int i=0;next&&i<32;++i,next=next->next)if(next->type==XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR){
        auto* b=reinterpret_cast<const XrGraphicsBindingOpenGLESAndroidKHR*>(next);s.display=b->display;s.context=b->context;break;}
    if(haveStereoViews){s.recommendedWidth=std::max(stereoViews[0].recommendedImageRectWidth,stereoViews[1].recommendedImageRectWidth);s.recommendedHeight=std::max(stereoViews[0].recommendedImageRectHeight,stereoViews[1].recommendedImageRectHeight);
        s.maxWidth=std::min(stereoViews[0].maxImageRectWidth,stereoViews[1].maxImageRectWidth);s.maxHeight=std::min(stereoViews[0].maxImageRectHeight,stereoViews[1].maxImageRectHeight);}sessions[*session]=s;
    emit("session_created","\"session\":"+std::to_string(hv(*session))+",\"hasGlesBinding\":"+(s.context!=EGL_NO_CONTEXT?"true":"false"));return r;}

XrResult XRAPI_PTR layerCreateSwapchain(XrSession session,const XrSwapchainCreateInfo* info,XrSwapchain* handle){
    XrResult r=g.createSwapchain(session,info,handle);if(XR_SUCCEEDED(r)&&info&&handle){SwapchainState s;s.session=session;s.info=*info;s.info.next=nullptr;swapchains[*handle]=s;
        emit("create_swapchain","\"swapchain\":"+std::to_string(hv(*handle))+",\"width\":"+std::to_string(info->width)+",\"height\":"+std::to_string(info->height)+",\"sampleCount\":"+std::to_string(info->sampleCount)+",\"format\":"+std::to_string(info->format));}return r;}
XrResult XRAPI_PTR layerEnumerateImages(XrSwapchain handle,uint32_t cap,uint32_t* count,XrSwapchainImageBaseHeader* images){
    XrResult r=g.enumerateImages(handle,cap,count,images);auto it=swapchains.find(handle);if(XR_SUCCEEDED(r)&&it!=swapchains.end()&&count&&images&&cap>=*count){it->second.images.resize(*count);
        for(uint32_t i=0;i<*count;++i)it->second.images[i]=*reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(reinterpret_cast<char*>(images)+i*sizeof(XrSwapchainImageOpenGLESKHR));}return r;}
XrResult XRAPI_PTR layerAcquire(XrSwapchain handle,const XrSwapchainImageAcquireInfo* info,uint32_t* index){auto it=swapchains.find(handle);if(it!=swapchains.end()&&it->second.deferred){auto s=sessions.find(it->second.session);if(s!=sessions.end()){flushSources(s->second);s->second.active=false;s->second.disabled=true;emit("reconstruction_disabled","\"session\":"+std::to_string(hv(s->second.session))+",\"reason\":\"acquire_before_end_frame\"");}}
    XrResult r=g.acquireImage(handle,info,index);if(XR_SUCCEEDED(r)&&it!=swapchains.end()&&index){it->second.acquired.push_back(*index);it->second.waited=false;}return r;}
XrResult XRAPI_PTR layerWait(XrSwapchain handle,const XrSwapchainImageWaitInfo* info){XrResult r=g.waitImage(handle,info);auto it=swapchains.find(handle);if(XR_SUCCEEDED(r)&&it!=swapchains.end())it->second.waited=true;return r;}
XrResult XRAPI_PTR layerRelease(XrSwapchain handle,const XrSwapchainImageReleaseInfo* info){auto it=swapchains.find(handle);if(it!=swapchains.end()&&it->second.role!=Role::Unknown){auto s=sessions.find(it->second.session);
    if(s!=sessions.end()&&s->second.active&&info&&!info->next&&it->second.waited&&it->second.acquired.size()==1&&!it->second.deferred){it->second.deferred=true;
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
    uint64_t frame=++frameCounter;Fingerprint f=inspect(info);auto sit=sessions.find(handle);
    if(sit==sessions.end()||!f.valid||f.session!=handle)
        return submitPassthrough(handle,sit==sessions.end()?nullptr:&sit->second,info,frame,f.reason,false);
    SessionState& s=sit->second;
    if(!s.learned||s.sources!=f.handles){const bool sourceIdentityChanged=s.learned&&s.sources!=f.handles;
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
        if(!acquireOutputs(s))return submitPassthrough(handle,&s,info,frame,"output_acquire_failed",true);
        if(!compose(s,*f.p[1],*f.p[2]))return submitPassthrough(handle,&s,info,frame,"compose_failed",true);
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
    qualitySettings.layerFlags=XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB;
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.next=qualitySettingsEnabled?&qualitySettings:nullptr;projection.space=f.p[1]->space;projection.viewCount=2;projection.views=views.data();
    const auto* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);XrFrameEndInfo output=*info;output.layerCount=1;output.layers=&layer;
    emit("single_projection_reconstruction_transform","\"session\":"+std::to_string(hv(handle))+",\"frame\":"+std::to_string(frame)+",\"sourceLayerCount\":3,\"sourceProjectionCount\":3,\"forwardedLayerCount\":1,\"outputProjectionCount\":1,\"sourceViewCount\":6,\"outputViewCount\":2,\"unsafeLayerCount\":0,\"reconstructed\":true,\"changed\":true,\"releaseSuccess\":true,\"outputWidth\":"+std::to_string(s.outputWidth)+",\"outputHeight\":"+std::to_string(s.outputHeight)+
        ",\"reusedOutput\":"+std::string(reusedOutput?"true":"false")+",\"sourceUpdate\":\""+(reusedOutput?"cached":"fresh")+"\",\"poseChanged\":"+
        std::string(poseChanged?"true":"false")+",\"deferredMask\":"+std::to_string(mask)+
        ",\"foveaFilter\":\"linear_4tap_subpixel_box\",\"outputQualitySettingsAttached\":"+
        (qualitySettingsEnabled?std::string("true"):std::string("false"))+
        ",\"outputQualitySettingsFlags\":2");
    XrResult r=g.endFrame(handle,&output);if(XR_SUCCEEDED(r)){s.everActivated=true;if(!reusedOutput)rememberMapping(s,*f.p[1],*f.p[2]);}
    emit("end_frame_result","\"session\":"+std::to_string(hv(handle))+",\"frame\":"+std::to_string(frame)+",\"result\":"+std::to_string(r));return r;
}

XrResult XRAPI_PTR layerDestroySwapchain(XrSwapchain handle){auto it=swapchains.find(handle);if(it!=swapchains.end()&&it->second.deferred){auto s=sessions.find(it->second.session);if(s!=sessions.end())flushSources(s->second);}swapchains.erase(handle);return g.destroySwapchain(handle);}
XrResult XRAPI_PTR layerDestroySession(XrSession handle){auto it=sessions.find(handle);if(it!=sessions.end()){flushSources(it->second);destroyOutputs(it->second);sessions.erase(it);}for(auto si=swapchains.begin();si!=swapchains.end();)if(si->second.session==handle)si=swapchains.erase(si);else++si;return g.destroySession(handle);}
XrResult XRAPI_PTR layerDestroyInstance(XrInstance instance){for(auto& pair:sessions){flushSources(pair.second);destroyOutputs(pair.second);}sessions.clear();swapchains.clear();emit("destroy_instance");XrResult r=g.destroyInstance(instance);g={};qualitySettingsEnabled=false;return r;}
XrResult XRAPI_PTR layerPollEvent(XrInstance instance,XrEventDataBuffer* data){XrResult r=g.pollEvent(instance,data);if(XR_SUCCEEDED(r)&&data&&data->type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){auto* e=reinterpret_cast<XrEventDataSessionStateChanged*>(data);emit("session_state_changed","\"state\":"+std::to_string(static_cast<int>(e->state)));}return r;}
XrResult XRAPI_PTR layerWaitFrame(XrSession session,const XrFrameWaitInfo* info,XrFrameState* state){XrResult r=g.waitFrame(session,info,state);if(sample(frameCounter.load()))emit("wait_frame","\"result\":"+std::to_string(r)+(state?",\"predictedDisplayPeriod\":"+std::to_string(state->predictedDisplayPeriod):""));return r;}

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

bool runtimeAdvertisesQualitySettings(PFN_xrGetInstanceProcAddr getInstanceProcAddr){
    PFN_xrVoidFunction address{};
    if(!getInstanceProcAddr||XR_FAILED(getInstanceProcAddr(XR_NULL_HANDLE,
        "xrEnumerateInstanceExtensionProperties",&address))||!address)return false;
    auto enumerate=reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(address);
    uint32_t count{};
    if(XR_FAILED(enumerate(nullptr,0,&count,nullptr))||!count)return false;
    std::vector<XrExtensionProperties> properties(count,{XR_TYPE_EXTENSION_PROPERTIES});
    if(XR_FAILED(enumerate(nullptr,count,&count,properties.data())))return false;
    for(const auto& property:properties)if(std::strcmp(property.extensionName,
        XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME)==0)return true;
    return false;
}

XrResult XRAPI_PTR layerCreateApiLayerInstance(const XrInstanceCreateInfo* ci,const XrApiLayerCreateInfo* ai,XrInstance* instance){
    if(!ci||!ai||!ai->nextInfo)return XR_ERROR_INITIALIZATION_FAILED;
    bool appEnabled=false;
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)if(std::strcmp(ci->enabledExtensionNames[i],
        XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME)==0){appEnabled=true;break;}
    const bool advertised=runtimeAdvertisesQualitySettings(ai->nextInfo->nextGetInstanceProcAddr);
    std::vector<const char*> extensions;
    extensions.reserve(ci->enabledExtensionCount+1);
    for(uint32_t i=0;i<ci->enabledExtensionCount;++i)
        extensions.push_back(ci->enabledExtensionNames[i]);
    const bool appended=advertised&&!appEnabled;
    if(appended)extensions.push_back(XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME);
    XrInstanceCreateInfo patched=*ci;
    if(appended){patched.enabledExtensionCount=static_cast<uint32_t>(extensions.size());patched.enabledExtensionNames=extensions.data();}
    XrApiLayerCreateInfo next=*ai;next.nextInfo=ai->nextInfo->next;
    XrResult r=ai->nextInfo->nextCreateApiLayerInstance(&patched,&next,instance);if(XR_FAILED(r))return r;
    qualitySettingsEnabled=appEnabled||appended;g.instance=*instance;g.getInstanceProcAddr=ai->nextInfo->nextGetInstanceProcAddr;
    load("xrDestroyInstance",g.destroyInstance);load("xrEnumerateViewConfigurationViews",g.enumerateViews);load("xrCreateSession",g.createSession);load("xrDestroySession",g.destroySession);
    load("xrCreateSwapchain",g.createSwapchain);load("xrDestroySwapchain",g.destroySwapchain);load("xrEnumerateSwapchainImages",g.enumerateImages);load("xrAcquireSwapchainImage",g.acquireImage);
    load("xrWaitSwapchainImage",g.waitImage);load("xrReleaseSwapchainImage",g.releaseImage);load("xrEndFrame",g.endFrame);load("xrPollEvent",g.pollEvent);load("xrWaitFrame",g.waitFrame);
    emit("layer_initialized","\"layerName\":"+quote(kLayerName)+",\"reconstructionBuildId\":"+quote(kBuildId)+
        ",\"qualityExtensionAdvertised\":"+(advertised?std::string("true"):std::string("false"))+
        ",\"qualityExtensionAppEnabled\":"+(appEnabled?std::string("true"):std::string("false"))+
        ",\"qualityExtensionAppended\":"+(appended?std::string("true"):std::string("false"))+
        ",\"qualitySettingsEnabled\":"+(qualitySettingsEnabled?std::string("true"):std::string("false")));return XR_SUCCESS;
}

} // namespace

GXR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loader,const char* name,XrNegotiateApiLayerRequest* request){
    if(!loader||!name||!request||std::strcmp(name,kLayerName)!=0)return XR_ERROR_INITIALIZATION_FAILED;
    if(loader->maxInterfaceVersion<XR_CURRENT_LOADER_API_LAYER_VERSION||loader->maxApiVersion<XR_CURRENT_API_VERSION)return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion=XR_CURRENT_LOADER_API_LAYER_VERSION;request->layerApiVersion=XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr=layerGetInstanceProcAddr;request->createApiLayerInstance=layerCreateApiLayerInstance;return XR_SUCCESS;
}
