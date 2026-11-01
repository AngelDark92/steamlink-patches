// Bounded GXH1 envelope parser and coverage validation (client copy).
// Pure C++17, no platform dependencies. The driver copy of this file is
// GalaxyXR-Driver/GalaxyXRDriver/src/Hybrid/Gxh1Codec.{h,cpp}; the parser shape is
// shared so the writer and the reader agree on every bound.
#ifndef GXR_HYBRID_GXH1_PARSE_H
#define GXR_HYBRID_GXH1_PARSE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../protocol/gxh1.h"

namespace gxh1 {
uint32_t Crc32(const uint8_t* data, size_t bytes, uint32_t seed = 0);

// Structural limits applied before any allocation or upload on the client.
struct ParseLimits {
	uint32_t maxBaseBytes = kMaxBaseBytes;
	uint32_t maxFoveaBytes = kMaxFoveaBytes;
	uint16_t maxPatches = kMaxPatches;
	uint16_t maxCanvas = kMaxCanvasPixels;
	uint16_t maxFeatherPx = 64;
};

enum class ParseError {
	None,
	TooSmall,
	BadMagic,
	BadVersion,
	BadHeaderBytes,
	BadFlags,
	LengthOverflow,
	BadOffsets,
	BadCodec,
	BadPrecision,
	BadCanvas,
	TooManyPatches,
	BadPatchTable,
	BadPatchGeometry,
	PatchOutOfCanvas,
	PatchOverlap,
	CoverageGap,
	BadEpochs,
	BadCrc,
	UnsupportedColorLevel,
	BadMapping,
};

const char* ParseErrorName(ParseError error);

// Encoded texture size of a patch: its rect at native density, or the declared texel grid.
inline uint16_t PatchTexelW(const PatchEntry& patch) { return patch.texelW ? patch.texelW : patch.width; }
inline uint16_t PatchTexelH(const PatchEntry& patch) { return patch.texelH ? patch.texelH : patch.height; }

struct ParsedPatch {
	PatchEntry entry{};
	const uint8_t* payload = nullptr;
};

struct ParsedFrame {
	EnvelopeHeader header{};
	const uint8_t* base = nullptr;
	const FoveaUnitHeader* unit = nullptr;
	std::vector<ParsedPatch> patches;
	bool foveaPresent = false;
	// True when both eyes tile the declared canvas exactly.
	bool coverageComplete = false;
};

// Validates magic/version/lengths/codecs/epochs/CRC, then every patch rectangle and
// payload range, then exact canvas tiling per eye. Returns ParseError::None on success.
// `parsed` is only populated on success; no pointer escapes `data`.
ParseError ParseFrame(const uint8_t* data, size_t bytes, const ParseLimits& limits, ParsedFrame& parsed);

// Canonical tiling check used by both the host planner and the client validator.
// `cells` is (canvasW/cell) * (canvasH/cell) bytes, one per cell, and must start zeroed.
bool ValidateCoverage(
	const PatchEntry* patches, size_t patchCount, uint16_t canvasW, uint16_t canvasH,
	uint16_t cellPixels, std::vector<uint8_t>& cells);

}  // namespace gxh1

#endif  // GXR_HYBRID_GXH1_PARSE_H
