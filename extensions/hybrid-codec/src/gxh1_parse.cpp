#include "gxh1_parse.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gxh1 {
namespace {
constexpr size_t kUnitHeaderBytes = kFoveaUnitHeaderBytes;

bool RangeFits(uint64_t offset, uint64_t length, uint64_t total) {
	return offset <= total && length <= total - offset;
}

bool IsKnownBaseCodec(uint8_t codec) {
	return codec == kBaseCodecHevc || codec == kBaseCodecAv1;
}

bool IsKnownFoveaCodec(uint8_t codec) {
	return codec == kFoveaCodecAstc || codec == kFoveaCodecPyroWave;
}

bool IsKnownPrecision(uint8_t precision) {
	return precision == kFoveaPrecisionRgb8 || precision == kFoveaPrecisionRgb10;
}

bool Finite(float value) {
	return std::isfinite(value);
}

// The canvas must lie inside the eye tile it claims to come from, and the frustum and gaze
// must describe a real projection; a compositor maps canvas pixels to rays with these.
bool ValidEyeMapping(const EyeMapping& eye, uint16_t canvasW, uint16_t canvasH) {
	if (eye.sourceW == 0 || eye.sourceH == 0) return false;
	if (static_cast<uint32_t>(eye.originX) + canvasW > eye.sourceW) return false;
	if (static_cast<uint32_t>(eye.originY) + canvasH > eye.sourceH) return false;
	if (!Finite(eye.tanLeft) || !Finite(eye.tanRight) || !Finite(eye.tanTop) || !Finite(eye.tanBottom)) {
		return false;
	}
	if (!(eye.tanLeft < eye.tanRight) || !(eye.tanTop < eye.tanBottom)) return false;
	if (!Finite(eye.gazeX) || !Finite(eye.gazeY)) return false;
	if (eye.gazeX < 0.0f || eye.gazeY < 0.0f) return false;
	if (eye.gazeX > static_cast<float>(eye.sourceW) || eye.gazeY > static_cast<float>(eye.sourceH)) return false;
	return true;
}

}  // namespace

uint32_t Crc32(const uint8_t* data, size_t bytes, uint32_t seed) {
	static uint32_t table[256];
	static bool init = false;
	if (!init) {
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for (int k = 0; k < 8; ++k) {
				c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			}
			table[i] = c;
		}
		init = true;
	}
	uint32_t crc = seed ^ 0xFFFFFFFFu;
	for (size_t i = 0; i < bytes; ++i) {
		crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFFu;
}

const char* ParseErrorName(ParseError error) {
	switch (error) {
	case ParseError::None: return "ok";
	case ParseError::TooSmall: return "too_small";
	case ParseError::BadMagic: return "bad_magic";
	case ParseError::BadVersion: return "bad_version";
	case ParseError::BadHeaderBytes: return "bad_header_bytes";
	case ParseError::BadFlags: return "bad_flags";
	case ParseError::LengthOverflow: return "length_overflow";
	case ParseError::BadOffsets: return "bad_offsets";
	case ParseError::BadCodec: return "bad_codec";
	case ParseError::BadPrecision: return "bad_precision";
	case ParseError::BadCanvas: return "bad_canvas";
	case ParseError::TooManyPatches: return "too_many_patches";
	case ParseError::BadPatchTable: return "bad_patch_table";
	case ParseError::BadPatchGeometry: return "bad_patch_geometry";
	case ParseError::PatchOutOfCanvas: return "patch_out_of_canvas";
	case ParseError::PatchOverlap: return "patch_overlap";
	case ParseError::CoverageGap: return "coverage_gap";
	case ParseError::BadEpochs: return "bad_epochs";
	case ParseError::BadCrc: return "bad_crc";
	case ParseError::UnsupportedColorLevel: return "unsupported_color_level";
	case ParseError::BadMapping: return "bad_mapping";
	}
	return "unknown";
}

bool ValidateCoverage(
	const PatchEntry* patches, size_t patchCount, uint16_t canvasW, uint16_t canvasH,
	uint16_t cellPixels, std::vector<uint8_t>& cells) {
	(void)cellPixels;
	(void)cells;
	if (canvasW == 0 || canvasH == 0) return false;

	// Exact axis-aligned coverage: compress every patch edge into a coordinate grid, then
	// require exactly one covering patch for every resulting cell. This catches 1-pixel
	// gaps and overlaps that a sampled cell-centre check misses, and a canvas smaller than
	// one sample cell is still validated.
	for (int eye = 0; eye < 2; ++eye) {
		std::vector<int32_t> xs;
		std::vector<int32_t> ys;
		xs.push_back(0);
		xs.push_back(canvasW);
		ys.push_back(0);
		ys.push_back(canvasH);
		for (size_t i = 0; i < patchCount; ++i) {
			const PatchEntry& p = patches[i];
			if (p.eye != eye) continue;
			if (p.width == 0 || p.height == 0) return false;
			xs.push_back(p.x);
			xs.push_back(static_cast<int32_t>(p.x) + p.width);
			ys.push_back(p.y);
			ys.push_back(static_cast<int32_t>(p.y) + p.height);
		}
		std::sort(xs.begin(), xs.end());
		xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
		std::sort(ys.begin(), ys.end());
		ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

		for (size_t j = 0; j + 1 < ys.size(); ++j) {
			for (size_t i = 0; i + 1 < xs.size(); ++i) {
				const int32_t px = xs[i];
				const int32_t py = ys[j];
				if (xs[i + 1] <= px || ys[j + 1] <= py) continue;
				int covering = 0;
				for (size_t k = 0; k < patchCount; ++k) {
					const PatchEntry& p = patches[k];
					if (p.eye != eye) continue;
					if (px >= p.x && px < static_cast<int32_t>(p.x) + p.width && py >= p.y &&
					    py < static_cast<int32_t>(p.y) + p.height) {
						++covering;
					}
				}
				if (covering != 1) return false;
			}
		}
	}
	return true;
}

ParseError ParseFrame(const uint8_t* data, size_t bytes, const ParseLimits& limits, ParsedFrame& parsed) {
	parsed = ParsedFrame{};
	if (!data) return ParseError::TooSmall;
	if (bytes < kHeaderBytes) return ParseError::TooSmall;

	EnvelopeHeader header{};
	std::memcpy(&header, data, sizeof(header));
	if (header.magic != kMagic) return ParseError::BadMagic;
	if (header.versionMajor != kVersionMajor) return ParseError::BadVersion;
	if (header.headerBytes != kHeaderBytes) return ParseError::BadHeaderBytes;
	if (header.versionMinor > kVersionMinor) return ParseError::BadVersion;

	const uint16_t knownFlags = kFlagFoveaPresent | kFlagReset | kFlagDegraded | kFlagExactCore | kFlagSnapshot;
	if ((header.flags & ~knownFlags) != 0) return ParseError::BadFlags;

	if (header.baseBytes == 0 || header.baseBytes > limits.maxBaseBytes) return ParseError::LengthOverflow;
	if (header.foveaBytes > limits.maxFoveaBytes) return ParseError::LengthOverflow;
	if (!IsKnownBaseCodec(header.baseCodec)) return ParseError::BadCodec;
	if (header.baseFormat != kBaseFormatNv12 && header.baseFormat != kBaseFormatP010) return ParseError::BadCodec;
	if (!IsKnownPrecision(header.precision)) return ParseError::BadPrecision;
	// Both implemented fovea codecs are 8-bit sRGB payloads. A 10-bit declaration would be
	// a claim the decoders cannot honour, so reject it before any allocation.
	if (header.precision != kFoveaPrecisionRgb8) return ParseError::UnsupportedColorLevel;

	const bool foveaPresent = (header.flags & kFlagFoveaPresent) != 0;
	if (foveaPresent) {
		if (!IsKnownFoveaCodec(header.foveaCodec)) return ParseError::BadCodec;
		if (header.canvasW == 0 || header.canvasH == 0 || header.canvasW > limits.maxCanvas ||
		    header.canvasH > limits.maxCanvas) return ParseError::BadCanvas;
		if (header.featherPx > limits.maxFeatherPx) return ParseError::BadCanvas;
		if (header.foveaBytes < kUnitHeaderBytes) return ParseError::LengthOverflow;
		if (header.patchCount == 0 || header.patchCount > limits.maxPatches) return ParseError::TooManyPatches;
	} else {
		if (header.foveaBytes != 0 || header.patchCount != 0) return ParseError::BadFlags;
		if (header.foveaCodec != kFoveaCodecNone) return ParseError::BadCodec;
	}

	const uint64_t baseEnd = static_cast<uint64_t>(header.baseOffset) + header.baseBytes;
	const uint64_t total = baseEnd + header.foveaBytes;
	if (total > bytes) return ParseError::LengthOverflow;
	if (header.baseOffset < kHeaderBytes) return ParseError::BadOffsets;
	if (foveaPresent && header.foveaOffset != baseEnd) return ParseError::BadOffsets;
	if (!foveaPresent && header.foveaOffset != 0) return ParseError::BadOffsets;
	if (header.foveaBytes > 0 && header.foveaOffset < kHeaderBytes) return ParseError::BadOffsets;

	// CRC over header (crc zeroed), then base, then fovea.
	{
		EnvelopeHeader copy = header;
		copy.crc32 = 0;
		uint32_t crc = Crc32(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
		crc = Crc32(data + header.baseOffset, header.baseBytes, crc);
		if (header.foveaBytes) {
			crc = Crc32(data + header.foveaOffset, header.foveaBytes, crc);
		}
		if (crc != header.crc32) return ParseError::BadCrc;
	}

	parsed.header = header;
	parsed.base = data + header.baseOffset;
	if (!foveaPresent) {
		if ((header.flags & kFlagDegraded) == 0) return ParseError::BadFlags;
		return ParseError::None;
	}

	const uint8_t* unitBytes = data + header.foveaOffset;
	FoveaUnitHeader unit{};
	std::memcpy(&unit, unitBytes, sizeof(unit));
	if (unit.unitBytes != header.foveaBytes) return ParseError::LengthOverflow;
	if (unit.patchCount != header.patchCount) return ParseError::BadPatchTable;
	if (unit.canvasW != header.canvasW || unit.canvasH != header.canvasH) return ParseError::BadCanvas;
	if (unit.foveaCodec != header.foveaCodec) return ParseError::BadCodec;
	if (unit.precision != header.precision) return ParseError::BadPrecision;
	if (unit.mappingEpoch != header.mappingEpoch) return ParseError::BadEpochs;
	if (unit.patchCount > limits.maxPatches) return ParseError::TooManyPatches;
	if (unit.featherPx != header.featherPx) return ParseError::BadCanvas;
	for (int eye = 0; eye < 2; ++eye) {
		if (!ValidEyeMapping(unit.eyes[eye], unit.canvasW, unit.canvasH)) return ParseError::BadMapping;
	}

	const uint64_t tableBytes = static_cast<uint64_t>(unit.patchCount) * kPatchEntryBytes;
	if (kUnitHeaderBytes + tableBytes > header.foveaBytes) return ParseError::BadPatchTable;

	parsed.unit = reinterpret_cast<const FoveaUnitHeader*>(unitBytes);
	parsed.patches.reserve(unit.patchCount);
	std::vector<PatchEntry> entries;
	entries.reserve(unit.patchCount);

	for (uint16_t i = 0; i < unit.patchCount; ++i) {
		PatchEntry entry{};
		std::memcpy(&entry, unitBytes + kUnitHeaderBytes + static_cast<size_t>(i) * kPatchEntryBytes, sizeof(entry));
		if (entry.eye > 1) return ParseError::BadPatchGeometry;
		if (entry.codec != header.foveaCodec) return ParseError::BadCodec;
		if (entry.sourceEpoch != header.sourceEpoch) return ParseError::BadEpochs;
		if (entry.width == 0 || entry.height == 0) return ParseError::BadPatchGeometry;
		// A resampled patch never has more texels than pixels, and declares both axes.
		if ((entry.texelW == 0) != (entry.texelH == 0)) return ParseError::BadPatchGeometry;
		if (entry.texelW > entry.width || entry.texelH > entry.height) return ParseError::BadPatchGeometry;
		if (entry.blockW < 4 || entry.blockW > 12 || entry.blockH < 4 || entry.blockH > 12) {
			return ParseError::BadPatchGeometry;
		}
		if (static_cast<uint32_t>(entry.x) + entry.width > unit.canvasW ||
		    static_cast<uint32_t>(entry.y) + entry.height > unit.canvasH) return ParseError::PatchOutOfCanvas;
		if (entry.innerRadiusPx > entry.outerRadiusPx) return ParseError::BadPatchGeometry;
		if (!(entry.innerDegrees <= entry.outerDegrees)) return ParseError::BadPatchGeometry;

		const uint64_t payloadStart = entry.payloadOffset;
		if (payloadStart < kUnitHeaderBytes + tableBytes) return ParseError::BadPatchTable;
		if (!RangeFits(payloadStart, entry.payloadBytes, header.foveaBytes)) return ParseError::BadPatchTable;
		if (payloadStart + entry.payloadBytes > header.foveaBytes) return ParseError::BadPatchTable;

		ParsedPatch patch;
		patch.entry = entry;
		patch.payload = unitBytes + payloadStart;
		parsed.patches.push_back(patch);
		entries.push_back(entry);
	}

	// Reject overlapping payload ranges; identical offsets are only allowed for
	// zero-length patches, which the geometry check above already excludes.
	for (size_t i = 0; i < parsed.patches.size(); ++i) {
		const uint64_t a0 = parsed.patches[i].entry.payloadOffset;
		const uint64_t a1 = a0 + parsed.patches[i].entry.payloadBytes;
		for (size_t j = i + 1; j < parsed.patches.size(); ++j) {
			const uint64_t b0 = parsed.patches[j].entry.payloadOffset;
			const uint64_t b1 = b0 + parsed.patches[j].entry.payloadBytes;
			if (a0 < b1 && b0 < a1) return ParseError::BadPatchTable;
		}
	}

	// Exact coverage is computed from the patch edges; the scratch vector is unused, so do
	// not allocate a canvas-sized grid on every frame.
	std::vector<uint8_t> cells;
	if (!ValidateCoverage(entries.data(), entries.size(), unit.canvasW, unit.canvasH, kCoverageCellPixels, cells)) {
		return ParseError::CoverageGap;
	}

	parsed.foveaPresent = true;
	parsed.coverageComplete = true;
	return ParseError::None;
}

}  // namespace gxh1
