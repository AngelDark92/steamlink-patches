// GXH1 hybrid fovea envelope - canonical wire schema v2.
//
// This is the client-side copy of the schema. The GXH1-SCHEMA block below is
// byte-for-byte identical to the SteamVR driver's copy at
// GalaxyXR-Driver/GalaxyXRDriver/src/Hybrid/Gxh1Protocol.h; the driver's
// tools/tests/hybrid-protocol-parity.test.cjs fails if either copy drifts. Edit the
// schema only by changing both copies in the same commit.
//
// Design rules from the ASTC streaming plan (2026-10-20, revised 2026-10-31):
//  - one FoveaUnit per frame describes both eyes; ring patches are internal storage,
//    never extra Steam Link layers or decoders;
//  - all offsets/lengths are checked before allocation or upload; overflow, coverage
//    gaps, overlaps and inconsistent epochs are rejected;
//  - the envelope wraps the unmodified selected NVENC base access unit. The base AU is
//    never re-encoded, resized, re-quantised or reordered by this layer.
#ifndef GXR_HYBRID_GXH1_PROTOCOL_H
#define GXR_HYBRID_GXH1_PROTOCOL_H

#include <cstddef>
#include <cstdint>

namespace gxh1 {
// >>> GXH1-SCHEMA
#pragma pack(push, 1)

// Little-endian on every supported target (x86-64 host, AArch64 client).
constexpr uint32_t kMagic = 0x31485847u;  // 'G''X''H''1' packed low byte first
constexpr uint16_t kVersionMajor = 2;
constexpr uint16_t kVersionMinor = 0;
constexpr uint16_t kHeaderBytes = 80;
constexpr uint16_t kEyeMappingBytes = 32;
constexpr uint16_t kFoveaUnitHeaderBytes = 96;
constexpr uint16_t kPatchEntryBytes = 48;
constexpr uint16_t kMaxPatches = 64;
constexpr uint32_t kMaxBaseBytes = 8u * 1024u * 1024u;
constexpr uint32_t kMaxFoveaBytes = 8u * 1024u * 1024u;
constexpr uint16_t kCoverageCellPixels = 16;
// Largest canvas side and largest single decoded patch (after block or transform padding)
// a client must accept; the producer refuses bigger plans before capturing anything.
constexpr uint16_t kMaxCanvasPixels = 4096;
constexpr uint32_t kMaxPatchPixels = 1u << 21;

enum EnvelopeFlags : uint16_t {
	kFlagFoveaPresent = 1u << 0,
	kFlagReset = 1u << 1,      // epochs changed; client must drop all cached enhancement state
	kFlagDegraded = 1u << 2,   // host could not attach a complete fovea for this frame
	kFlagExactCore = 1u << 3,  // optional exact-core payload accompanies the rings
	kFlagSnapshot = 1u << 4,   // mapping snapshot changed this frame
};

enum BaseCodec : uint8_t {
	kBaseCodecHevc = 1,
	kBaseCodecAv1 = 2,
};

enum BaseFormat : uint8_t {
	kBaseFormatNv12 = 0,
	kBaseFormatP010 = 1,
};

enum FoveaCodec : uint8_t {
	kFoveaCodecNone = 0,
	kFoveaCodecAstc = 1,
	kFoveaCodecPyroWave = 2,
};

// Declared precision of the fovea layer, not of the base video. ASTC in this
// implementation is an 8-bit sRGB path (8-bit intermediates); it never claims RGB10.
enum FoveaPrecision : uint8_t {
	kFoveaPrecisionRgb8 = 8,
	kFoveaPrecisionRgb10 = 10,
};

enum PatchFlags : uint8_t {
	kPatchFlagDirty = 1u << 0,
	kPatchFlagExact = 1u << 1,
	kPatchFlagLastColumn = 1u << 2,  // patch touches the canvas right/bottom edge
};

struct EnvelopeHeader {
	uint32_t magic;
	uint16_t versionMajor;
	uint16_t versionMinor;
	uint16_t headerBytes;
	uint16_t flags;
	uint32_t sessionEpoch;
	uint32_t sourceEpoch;
	uint32_t mappingEpoch;
	uint64_t frameId;
	uint64_t ptsNs;
	uint32_t baseBytes;
	uint32_t foveaBytes;
	uint32_t baseOffset;
	uint32_t foveaOffset;
	uint8_t baseCodec;
	uint8_t baseFormat;
	uint8_t foveaCodec;
	uint8_t precision;
	uint16_t canvasW;
	uint16_t canvasH;
	uint16_t patchCount;
	uint16_t featherPx;
	uint32_t crc32;  // CRC-32/IEEE over the header with this field zeroed, then base, then fovea
	uint32_t reserved0;
	uint32_t reserved1;
};

// Where one eye's canvas was cut from, so a compositor can map canvas pixels to view rays.
// Coordinates are continuous eye-tile pixels: pixel i spans [i, i + 1).
struct EyeMapping {
	uint16_t sourceW;  // eye tile extent the canvas was cut from (source pixels)
	uint16_t sourceH;
	uint16_t originX;  // canvas top-left inside the eye tile; originX + canvasW <= sourceW
	uint16_t originY;
	float tanLeft;     // signed projection bounds of the eye tile, y-down: left < right, top < bottom
	float tanRight;
	float tanTop;
	float tanBottom;
	float gazeX;       // gaze ray in eye-tile pixels for this frame's plan
	float gazeY;
};

struct FoveaUnitHeader {
	uint32_t unitBytes;
	uint16_t canvasW;  // shared by both eyes
	uint16_t canvasH;
	uint8_t foveaCodec;
	uint8_t precision;
	uint8_t patchCount;
	uint8_t flags;
	uint32_t mappingEpoch;
	uint16_t featherPx;
	uint16_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;
	uint32_t reserved3;
	EyeMapping eyes[2];
};

struct PatchEntry {
	uint32_t payloadBytes;
	uint32_t payloadOffset;  // from the start of the FoveaUnit
	uint16_t x;
	uint16_t y;
	uint16_t width;
	uint16_t height;
	uint8_t eye;
	uint8_t annulus;
	uint8_t blockW;
	uint8_t blockH;
	uint8_t codec;
	uint8_t flags;
	uint16_t texelW;  // encoded texture size when the ring is resampled below native density;
	uint16_t texelH;  // 0 = native (texels == width x height). The compositor stretches texels over the rect.
	uint32_t sourceEpoch;
	uint16_t innerRadiusPx;
	uint16_t outerRadiusPx;
	uint16_t gutters;
	float innerDegrees;
	float outerDegrees;
	uint32_t reserved1;
};

struct FrameLayout {
	const EnvelopeHeader* header = nullptr;
	const uint8_t* base = nullptr;
	const uint8_t* fovea = nullptr;
};

#pragma pack(pop)
// <<< GXH1-SCHEMA

static_assert(sizeof(EnvelopeHeader) == kHeaderBytes, "GXH1 header layout drifted");
static_assert(sizeof(EyeMapping) == kEyeMappingBytes, "GXH1 eye mapping layout drifted");
static_assert(sizeof(FoveaUnitHeader) == kFoveaUnitHeaderBytes, "GXH1 fovea header layout drifted");
static_assert(offsetof(FoveaUnitHeader, eyes) == 32, "GXH1 eye mapping offset drifted");
static_assert(sizeof(PatchEntry) == kPatchEntryBytes, "GXH1 patch entry layout drifted");
static_assert(offsetof(EnvelopeHeader, crc32) == 68, "GXH1 crc offset drifted");
static_assert(offsetof(PatchEntry, payloadOffset) == 4, "GXH1 patch payload offset drifted");
}  // namespace gxh1

#endif  // GXR_HYBRID_GXH1_PROTOCOL_H
