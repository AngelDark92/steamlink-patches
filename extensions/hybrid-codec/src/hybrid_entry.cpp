// C ABI for the GXH1 hybrid-codec helper.
//
// Loaded with dlopen() by the codecs-support layer; nothing links against this library at
// build time and nothing here runs at load (no global constructors beyond a constant
// initialiser). Rules:
//  - no C++ types cross the boundary (only pointers, size_t, int, uint64_t, char*);
//  - no exception escapes: every entry point is noexcept-by-construction (no allocation
//    except inside the session's bounded decode buffers, which are guarded in the session);
//  - gxr_hybrid_probe() is idempotent, allocation-free and side-effect-free.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "hybrid_session.h"

#if defined(__GNUC__) || defined(__clang__)
#define GXR_HYBRID_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define GXR_HYBRID_EXPORT extern "C" __declspec(dllexport)
#endif

namespace {

// The helper is usable exactly when the compiled wire layout matches the schema. All of
// these are compile-time constants, so probe() is a cached-bool return with no work.
constexpr bool kLayoutMatchesSchema =
	sizeof(gxh1::EnvelopeHeader) == gxh1::kHeaderBytes &&
	sizeof(gxh1::EyeMapping) == gxh1::kEyeMappingBytes &&
	sizeof(gxh1::FoveaUnitHeader) == gxh1::kFoveaUnitHeaderBytes &&
	sizeof(gxh1::PatchEntry) == gxh1::kPatchEntryBytes;

gxh1::HybridSession& Session() {
	// Intentionally leaked: a detached decode worker must never outlive the state it
	// reads, and process teardown must not join it.
	static gxh1::HybridSession* session = [] {
		auto* created = new gxh1::HybridSession();
		// CPU patch decoding is a bench/qualification mode: production runs demux-only until
		// GPU sampling and composition exist (GalaxyXR-Driver/Docs/HybridFovea.md, open gates).
		const char* decode = std::getenv("GXR_HYBRID_CPU_DECODE");
		created->SetDecodeEnabled(decode && decode[0] && std::strcmp(decode, "0") != 0);
		return created;
	}();
	return *session;
}

}  // namespace

GXR_HYBRID_EXPORT int gxr_hybrid_probe(void) { return kLayoutMatchesSchema ? 1 : 0; }

GXR_HYBRID_EXPORT int gxr_hybrid_on_access_unit(const uint8_t** data, size_t* size) {
	if (!data || !size || !*data || *size < sizeof(uint32_t)) return 0;
	uint32_t magic = 0;
	std::memcpy(&magic, *data, sizeof(magic));
	// Ordinary streams are rejected here, before the session (and its worker) is touched.
	if (magic != gxh1::kMagic) return 0;
	try {
		return Session().OnAccessUnit(data, size);
	} catch (...) {
		return -1;
	}
}

GXR_HYBRID_EXPORT int gxr_hybrid_take_layer(int eye, uint8_t* rgba, int width, int height, int stride,
                                            uint64_t* frameId) {
	if (!rgba || width <= 0 || height <= 0) return -1;
	try {
		return Session().CopyLayer(eye, rgba, width, height, stride, frameId) ? 0 : -1;
	} catch (...) {
		return -1;
	}
}

GXR_HYBRID_EXPORT void gxr_hybrid_reset(uint64_t sessionEpoch) {
	try {
		Session().Reset(sessionEpoch);
	} catch (...) {
		// A failed reset leaves the previous session cached; the caller's next envelope
		// still passes through the epoch admission checks.
	}
}

GXR_HYBRID_EXPORT const char* gxr_hybrid_status(void) {
	try {
		return Session().Status();
	} catch (...) {
		return "unavailable";
	}
}
