#include <android/hardware_buffer.h>
#include <android/log.h>
#include <dlfcn.h>
#include <media/NdkImage.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <time.h>
#include <unistd.h>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

constexpr char kLogTag[] = "GXRResolutionTrace";
constexpr char kModeName[] = "single_projection_native_probe_v1";
constexpr char kBuildId[] = "single-projection-native-probe-v1.0-20260831";

std::atomic<uint64_t> configureCalls{0};
std::atomic<uint64_t> dequeueCalls{0};
std::atomic<uint64_t> imageBufferCalls{0};
std::atomic<uint64_t> outputFormatChanges{0};
std::mutex descriptorMutex;
AHardwareBuffer_Desc lastDescriptor{};
bool haveLastDescriptor{};

uint64_t elapsedMs() {
    timespec value{};
    clock_gettime(CLOCK_BOOTTIME, &value);
    return static_cast<uint64_t>(value.tv_sec) * 1000ULL +
        static_cast<uint64_t>(value.tv_nsec) / 1000000ULL;
}

std::string quote(const char* value) {
    std::ostringstream output;
    output << '"';
    if (value) {
        for (const unsigned char character : std::string(value)) {
            if (character == '\\') output << "\\\\";
            else if (character == '"') output << "\\\"";
            else if (character == '\n') output << "\\n";
            else if (character == '\r') output << "\\r";
            else if (character == '\t') output << "\\t";
            else if (character < 0x20) output << '?';
            else output << static_cast<char>(character);
        }
    }
    output << '"';
    return output.str();
}

void emit(const char* event, const std::string& fields = {}) {
    std::ostringstream output;
    output << "{\"schema\":2,\"runId\":\"pid-" << getpid()
           << "\",\"source\":\"decoder\",\"mode\":\"" << kModeName
           << "\",\"buildId\":\"" << kBuildId << "\",\"elapsedMs\":" << elapsedMs()
           << ",\"event\":\"" << event << '"';
    if (!fields.empty()) output << ',' << fields;
    output << '}';
    __android_log_write(ANDROID_LOG_INFO, kLogTag, output.str().c_str());
}

void* librarySymbol(const char* library, const char* symbol) {
    void* handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        emit("decoder_probe_symbol_failure",
            "\"library\":" + quote(library) + ",\"symbol\":" + quote(symbol) +
            ",\"dlerror\":" + quote(dlerror()));
        return nullptr;
    }
    void* address = dlsym(handle, symbol);
    if (!address) {
        emit("decoder_probe_symbol_failure",
            "\"library\":" + quote(library) + ",\"symbol\":" + quote(symbol) +
            ",\"dlerror\":" + quote(dlerror()));
    }
    return address;
}

const char* hardwareBufferPrecisionHint(uint32_t format) {
    if (format == AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM) return "rgb10a2";
    if (format == AHARDWAREBUFFER_FORMAT_YCbCr_P010) return "p010";
    return "unclassified";
}

bool sameDescriptor(const AHardwareBuffer_Desc& left, const AHardwareBuffer_Desc& right) {
    return left.width == right.width && left.height == right.height &&
        left.layers == right.layers && left.format == right.format &&
        left.usage == right.usage && left.stride == right.stride;
}

}  // namespace

__attribute__((constructor)) static void initializeDecoderProbe() {
    emit("decoder_probe_initialized",
        "\"configureWrapper\":true,\"dequeueWrapper\":true,\"imageHardwareBufferWrapper\":true");
}

GXR_EXPORT media_status_t AMediaCodec_configure(
    AMediaCodec* codec,
    const AMediaFormat* format,
    ANativeWindow* surface,
    AMediaCrypto* crypto,
    uint32_t flags) {
    using Function = media_status_t (*)(
        AMediaCodec*, const AMediaFormat*, ANativeWindow*, AMediaCrypto*, uint32_t);
    static Function real = reinterpret_cast<Function>(
        librarySymbol("libmediandk.so", "AMediaCodec_configure"));
    const uint64_t call = ++configureCalls;
    const char* description = format ? AMediaFormat_toString(const_cast<AMediaFormat*>(format)) : nullptr;
    emit("decoder_codec_configure",
        "\"call\":" + std::to_string(call) + ",\"format\":" + quote(description) +
        ",\"surfaceOutput\":" + std::string(surface ? "true" : "false") +
        ",\"flags\":" + std::to_string(flags) +
        ",\"realSymbolResolved\":" + std::string(real ? "true" : "false"));
    return real ? real(codec, format, surface, crypto, flags) : AMEDIA_ERROR_UNSUPPORTED;
}

GXR_EXPORT ssize_t AMediaCodec_dequeueOutputBuffer(
    AMediaCodec* codec,
    AMediaCodecBufferInfo* info,
    int64_t timeoutUs) {
    using Function = ssize_t (*)(AMediaCodec*, AMediaCodecBufferInfo*, int64_t);
    static Function real = reinterpret_cast<Function>(
        librarySymbol("libmediandk.so", "AMediaCodec_dequeueOutputBuffer"));
    const uint64_t call = ++dequeueCalls;
    if (!real) return AMEDIA_ERROR_UNSUPPORTED;
    const ssize_t result = real(codec, info, timeoutUs);
    if (result == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        const uint64_t change = ++outputFormatChanges;
        AMediaFormat* outputFormat = AMediaCodec_getOutputFormat(codec);
        const char* description = outputFormat ? AMediaFormat_toString(outputFormat) : nullptr;
        emit("decoder_output_format_changed",
            "\"call\":" + std::to_string(call) + ",\"change\":" + std::to_string(change) +
            ",\"format\":" + quote(description));
        if (outputFormat) AMediaFormat_delete(outputFormat);
    }
    return result;
}

GXR_EXPORT media_status_t AImage_getHardwareBuffer(
    const AImage* image,
    AHardwareBuffer** buffer) {
    using Function = media_status_t (*)(const AImage*, AHardwareBuffer**);
    static Function real = reinterpret_cast<Function>(
        librarySymbol("libmediandk.so", "AImage_getHardwareBuffer"));
    const uint64_t call = ++imageBufferCalls;
    if (!real) return AMEDIA_ERROR_UNSUPPORTED;
    const media_status_t result = real(image, buffer);
    if (result != AMEDIA_OK || !buffer || !*buffer) {
        if (call <= 3) {
            emit("decoder_hardware_buffer_unavailable",
                "\"call\":" + std::to_string(call) + ",\"result\":" + std::to_string(result));
        }
        return result;
    }

    AHardwareBuffer_Desc descriptor{};
    AHardwareBuffer_describe(*buffer, &descriptor);
    bool changed{};
    {
        std::lock_guard<std::mutex> lock(descriptorMutex);
        changed = !haveLastDescriptor || !sameDescriptor(lastDescriptor, descriptor);
        if (changed) {
            lastDescriptor = descriptor;
            haveLastDescriptor = true;
        }
    }
    if (changed) {
        emit("decoder_hardware_buffer_descriptor",
            "\"call\":" + std::to_string(call) +
            ",\"width\":" + std::to_string(descriptor.width) +
            ",\"height\":" + std::to_string(descriptor.height) +
            ",\"layers\":" + std::to_string(descriptor.layers) +
            ",\"format\":" + std::to_string(descriptor.format) +
            ",\"usage\":" + std::to_string(descriptor.usage) +
            ",\"stride\":" + std::to_string(descriptor.stride) +
            ",\"precisionHint\":" + quote(hardwareBufferPrecisionHint(descriptor.format)));
    }
    return result;
}
