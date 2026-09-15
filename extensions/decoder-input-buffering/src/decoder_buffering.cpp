#include "staging_pool.h"
#include "install_hooks.h"
#ifdef GXR_DBUF_HOST_TEST
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_ERROR 6
static int __android_log_print(int, const char*, const char*, ...) { return 0; }
#else
#include <android/log.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

// Installer changes only mode (0 observe, 1 buffered). Keep this object visible
// to the compiler as mutable external state; the packaged file configures it.
struct DecoderBufferingConfig { char magic[16]; uint32_t mode; };
extern "C" {
__attribute__((visibility("protected"), used)) DecoderBufferingConfig
gxr_decoder_buffering_config = {{'G','X','R','D','B','U','F','C','O','N','F','I','G','0','1','!'}, 1};
}

namespace {
using Pool = gxr::decoder::StagingPool<>;
using Clock = std::chrono::steady_clock;
using AcquireFn = uint8_t* (*)(void*, uint32_t, size_t*);
using SubmitFn = bool (*)(void*, uint8_t**, int, uint32_t, bool);
using InitFn = bool (*)(void*);
using VoidFn = void (*)(void*);
using PacketFn = void (*)(void*, void*);
// Definitions shared with install_hooks.h are initialized before scene ctors.
gxr::dbuf::HookBindings hooks{};
constexpr size_t RendererOffset = GXR_BUILD_CODE == 5002363 ? 0x28 : 0x20;
constexpr size_t ActiveOffset = GXR_BUILD_CODE == 5002363 ? 0xb9 : 0xb1;
constexpr uintptr_t FaultAddress = GXR_BUILD_CODE == 5002363 ? 0x166dec : 0x165fb8;
template <class T> T read(void* p, size_t offset) {
    T value{}; if (p) std::memcpy(&value, static_cast<uint8_t*>(p) + offset, sizeof(value));
    return value;
}
void* fecForCodec(void* codec) {
    auto* renderer = read<void*>(codec, RendererOffset);
    auto* client = read<void*>(renderer, 0x18);
    auto* link = read<void*>(client, 0x140);
    return read<void*>(link, 0x10);
}
void* codecForFec(void* fec) {
    auto* client = read<void*>(fec, 0xc30);
    auto* renderer = read<void*>(client, 0x138);
    return read<void*>(renderer, 0x10);
}
bool buffered() { return gxr_decoder_buffering_config.mode == 1; }
struct State {
    Pool pool;
    uint64_t instance = 0;
    uint64_t reclaimedGeneration = 1;
    uint64_t acquires = 0, submitted = 0, faults = 0, flushes = 0, stops = 0;
    uint64_t maxAcquireUs = 0, copyBytes = 0, maxCopyUs = 0;
    unsigned pinned = 0, observedHigh = 0;
    uint32_t lastAcquireFrame = 0, lastSubmitFrame = 0;
    std::array<bool, 16> observed{};
    Clock::time_point lastLog{};
};
// The inspected output/render paths do not call these input/lifecycle hooks.
// Recursive locking is necessary for Acquire(-10000) -> virtual Stop -> Init.
std::recursive_mutex& mutex() { static std::recursive_mutex value; return value; }
thread_local unsigned receiveDepth = 0;
auto& states() { static std::map<void*, std::shared_ptr<State>> value; return value; }
std::shared_ptr<State> state(void* codec) {
    static uint64_t nextInstance = 0;
    auto& item = states()[codec];
    if (!item) { item = std::make_shared<State>(); item->instance = ++nextInstance; }
    return item;
}
uint64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}
void report(State& s, const char* event, bool force = false) {
    const auto now = Clock::now();
    if (!force && now - s.lastLog < std::chrono::seconds(1)) return;
    s.lastLog = now;
    __android_log_print(ANDROID_LOG_INFO, "GxrDecoderBuffer",
        "v1 build=%d mode=%s event=%s instance=%llu gen=%llu acquire=%llu submit=%llu fault=%llu flush=%llu stop=%llu staging=%zu high=%zu allocated=%zu reserved=%zu reservedHigh=%u acquireMaxUs=%llu copied=%llu copyMaxUs=%llu lastAcquire=%u lastSubmit=%u",
        GXR_BUILD_CODE, buffered() ? "buffered" : "observe", event,
        (unsigned long long)s.instance,
        (unsigned long long)s.pool.generation, (unsigned long long)s.acquires,
        (unsigned long long)s.submitted, (unsigned long long)s.faults,
        (unsigned long long)s.flushes, (unsigned long long)s.stops,
        s.pool.leased, s.pool.highWater, s.pool.allocated,
        size_t(std::count(s.observed.begin(), s.observed.end(), true)), s.observedHigh,
        (unsigned long long)s.maxAcquireUs, (unsigned long long)s.copyBytes,
        (unsigned long long)s.maxCopyUs, s.lastAcquireFrame, s.lastSubmitFrame);
}
void fault(void* codec, State& s, const char* reason) {
    ++s.faults;
    if (void* fec = fecForCodec(codec))
        reinterpret_cast<VoidFn>(hooks.base + FaultAddress)(fec);
    report(s, reason);
}
template <class T> T original(gxr::dbuf::HookId index) {
    return reinterpret_cast<T>(hooks.orig[static_cast<size_t>(index)]);
}
using HookIndex = gxr::dbuf::HookId;
uint8_t* acquire(void* codec, uint32_t frame, size_t* capacity) {
    if (!gxr::dbuf::hooksActive()) return original<AcquireFn>(HookIndex::Acquire)(codec, frame, capacity);
    std::lock_guard<std::recursive_mutex> lock(mutex());
    auto s = state(codec);
    ++s->acquires;
    s->lastAcquireFrame = frame;
    const auto start = Clock::now();
    uint8_t* result;
    if (buffered() && __atomic_load_n(static_cast<uint8_t*>(codec) + ActiveOffset, __ATOMIC_ACQUIRE)) {
        result = s->pool.acquire(frame, capacity);
        if (!result) fault(codec, *s, "staging-capacity");
    } else {
        result = original<AcquireFn>(HookIndex::Acquire)(codec, frame, capacity);
        if (result && !buffered()) s->observed[frame & 15] = true;
        if (!result && !buffered()) ++s->faults;
    }
    s->maxAcquireUs = std::max(s->maxAcquireUs, elapsed(start));
    s->observedHigh = std::max(s->observedHigh, unsigned(std::count(s->observed.begin(), s->observed.end(), true)));
    report(*s, result ? "acquire" : "input-unavailable");
    return result;
}
bool submit(void* codec, uint8_t** data, int bytes, uint32_t frame, bool marker) {
    if (!gxr::dbuf::hooksActive()) return original<SubmitFn>(HookIndex::Submit)(codec, data, bytes, frame, marker);
    std::lock_guard<std::recursive_mutex> lock(mutex());
    auto s = state(codec);
    s->lastSubmitFrame = frame;
    if (!buffered()) {
        bool result = original<SubmitFn>(HookIndex::Submit)(codec, data, bytes, frame, marker);
        s->observed[frame & 15] = false;
        if (result) ++s->submitted;
        else ++s->faults;
        report(*s, "submit");
        return result;
    }
    auto* slot = data ? s->pool.find(*data) : nullptr;
    const auto generation = s->pool.generation;
    if (!s->pool.valid(slot, generation) || bytes < 0 || size_t(bytes) > 4 * 1024 * 1024) {
        fault(codec, *s, "invalid-staging-owner-or-size");
        return false;
    }
    // Pin across the stock Acquire's nested Stop/Init recovery. No metadata or
    // timestamps move to another thread; only compressed bytes are staged.
    struct Pin { unsigned& value; explicit Pin(unsigned& v) : value(v) { ++value; } ~Pin() { --value; } } pin(s->pinned);
    size_t actualCapacity = 0;
    auto start = Clock::now();
    uint8_t* input = original<AcquireFn>(HookIndex::Acquire)(codec, frame, &actualCapacity);
    s->maxAcquireUs = std::max(s->maxAcquireUs, elapsed(start));
    bool result = false;
    if (!input || !s->pool.valid(slot, generation) || size_t(bytes) > actualCapacity) {
        // Returning false alone is insufficient: the FEC caller ignores it.
        // The stock fault path flushes any real input index acquired above.
        fault(codec, *s, input ? "codec-capacity-or-generation" : "codec-input-unavailable");
    } else {
        start = Clock::now();
        std::memcpy(input, slot->data, size_t(bytes));
        s->copyBytes += size_t(bytes);
        s->maxCopyUs = std::max(s->maxCopyUs, elapsed(start));
        result = original<SubmitFn>(HookIndex::Submit)(codec, &input, bytes, frame, marker);
        if (result) {
            ++s->submitted;
            *data = nullptr;
            s->pool.release(slot, generation);
        } else {
            fault(codec, *s, "codec-submit-failed");
        }
    }
    report(*s, "submit");
    return result;
}
bool init(void* codec) {
    if (!gxr::dbuf::hooksActive()) return original<InitFn>(HookIndex::Init)(codec);
    std::lock_guard<std::recursive_mutex> lock(mutex());
    auto s = state(codec);
    s->pool.invalidate();
    s->observed.fill(false);
    return original<InitFn>(HookIndex::Init)(codec);
}
void flush(void* codec) {
    if (!gxr::dbuf::hooksActive()) { original<VoidFn>(HookIndex::Flush)(codec); return; }
    std::lock_guard<std::recursive_mutex> lock(mutex());
    auto s = state(codec);
    ++s->flushes;
    s->pool.invalidate();
    s->observed.fill(false);
    original<VoidFn>(HookIndex::Flush)(codec);
    report(*s, "flush");
}
void stop(void* codec) {
    if (!gxr::dbuf::hooksActive()) { original<VoidFn>(HookIndex::Stop)(codec); return; }
    std::lock_guard<std::recursive_mutex> lock(mutex());
    auto s = state(codec);
    ++s->stops;
    s->pool.invalidate();
    s->observed.fill(false);
    original<VoidFn>(HookIndex::Stop)(codec);
    report(*s, "stop");
}
void destroy(void* codec) {
    if (!gxr::dbuf::hooksActive()) { original<VoidFn>(HookIndex::Destructor)(codec); return; }
    std::lock_guard<std::recursive_mutex> lock(mutex());
    auto it = states().find(codec);
    auto s = it == states().end() ? std::shared_ptr<State>{} : it->second;
    original<VoidFn>(HookIndex::Destructor)(codec);
    if (s) report(*s, "destroy", true);
    states().erase(codec);
}
void periodic(void* fec) {
    if (!gxr::dbuf::hooksActive()) { original<VoidFn>(HookIndex::Periodic)(fec); return; }
    if (!buffered()) {
        original<VoidFn>(HookIndex::Periodic)(fec);
        return;
    }
    // A timeout from another transfer thread can enter here. A pre-read of the
    // FEC fault byte is insufficient: the output thread can set it afterward.
    // AcceptPacket already holds this lock for the usual per-packet invocation.
    std::unique_lock<std::recursive_mutex> lock(mutex(), std::defer_lock);
    if (!receiveDepth) lock.lock();
    original<VoidFn>(HookIndex::Periodic)(fec);
    auto it = states().find(codecForFec(fec));
    if (it == states().end() || !buffered() || it->second->pinned) return;
    auto& s = *it->second;
    if (s.reclaimedGeneration == s.pool.generation) return;
    // The stock recovery call has now cleared its RX and pending references.
    // No age-based eviction or reclamation on ordinary per-packet checks.
    s.pool.reclaimUnreferenced([fec](const uint8_t* p) {
        for (size_t i = 0; i < 16; ++i) {
            auto* rx = read<void*>(fec, 0x98 + i * 8);
            if (read<uint8_t*>(rx, 0x30) == p) return true;
        }
        for (size_t i = 0; i < 128; ++i)
            if (read<uint8_t*>(fec, 0x320 + i * 8) == p) return true;
        return false;
    });
    s.reclaimedGeneration = s.pool.generation;
    report(s, "periodic");
}
void acceptPacket(void* fec, void* packet) {
    if (!gxr::dbuf::hooksActive() || !buffered()) {
        original<PacketFn>(HookIndex::AcceptPacket)(fec, packet);
        return;
    }
    // Cover acquisition-to-publication, fragment memcpy, and pending transfer,
    // not just the helper calls within them. Never recycle while these run.
    std::lock_guard<std::recursive_mutex> lock(mutex());
    struct Receiving { Receiving() { ++receiveDepth; } ~Receiving() { --receiveDepth; } } receiving;
    original<PacketFn>(HookIndex::AcceptPacket)(fec, packet);
}
#ifndef GXR_DBUF_HOST_TEST
__attribute__((constructor))
#endif
void install() {
    if (gxr_decoder_buffering_config.mode > 1) {
        __android_log_print(ANDROID_LOG_ERROR, "GxrDecoderBuffer", "invalid config; stock path retained");
        return;
    }
    hooks.replacement[static_cast<size_t>(HookIndex::Acquire)] = reinterpret_cast<void*>(&acquire);
    hooks.replacement[static_cast<size_t>(HookIndex::Submit)] = reinterpret_cast<void*>(&submit);
    hooks.replacement[static_cast<size_t>(HookIndex::Init)] = reinterpret_cast<void*>(&init);
    hooks.replacement[static_cast<size_t>(HookIndex::Flush)] = reinterpret_cast<void*>(&flush);
    hooks.replacement[static_cast<size_t>(HookIndex::Stop)] = reinterpret_cast<void*>(&stop);
    hooks.replacement[static_cast<size_t>(HookIndex::Destructor)] = reinterpret_cast<void*>(&destroy);
    hooks.replacement[static_cast<size_t>(HookIndex::Periodic)] = reinterpret_cast<void*>(&periodic);
    hooks.replacement[static_cast<size_t>(HookIndex::AcceptPacket)] = reinterpret_cast<void*>(&acceptPacket);
    if (gxr::dbuf::installHooks(hooks))
        __android_log_print(ANDROID_LOG_INFO, "GxrDecoderBuffer", "v1 installed build=%d mode=%s stagingLimit=100663296 synchronous=1", GXR_BUILD_CODE, buffered() ? "buffered" : "observe");
}
}
