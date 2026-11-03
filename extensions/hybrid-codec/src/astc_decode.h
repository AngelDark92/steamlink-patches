// ASTC LDR single-partition subset (GXAST-v1): 16-byte blocks, one plane, direct RGB
// endpoints at QUANT_256, bits-only weight grids (QUANT_2/4/8/16). Decoding follows the
// spec order (weight infill, then UNORM8 endpoint interpolation), so blocks decode
// identically on GPU texture hardware and on this CPU reference. Both the host encoder and the client decoder in
// extensions/hybrid-codec implement exactly this subset; unsupported block modes are
// rejected, never guessed.
//
// Why a subset instead of a vendored encoder: the upstream astcenc implementation is
// Apache-2.0, which is not licence-compatible with this GPLv2 project, and the plan's
// local benchmark shows CPU astcenc is far too slow for 72/90 Hz. This encoder writes a
// real ASTC physical block layout (block mode, partition field, endpoint format, ISE bit
// order) so the bitstream is structurally conformant, while both ends of this pipeline
// agree on the same bounded subset.
#ifndef GXR_HYBRID_ASTC_SUBSET_H
#define GXR_HYBRID_ASTC_SUBSET_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gxh1 {

constexpr size_t kAstcBlockBytes = 16;

// 0 means "no block mode exists in the subset for this footprint/grid".
uint32_t FindAstcBlockMode(uint8_t blockW, uint8_t blockH, uint8_t gridX, uint8_t gridY, uint8_t quant);

struct AstcGrid {
	uint8_t gridX = 0;
	uint8_t gridY = 0;
	uint8_t quant = 0;   // ASTC quant_method: 0 = QUANT_2, 2 = QUANT_4, 5 = QUANT_8, 8 = QUANT_16
	uint8_t bits = 0;
	uint32_t blockMode = 0;
};

// Candidate weight grids for a footprint: at most one per weight precision (4..1 bits).
const std::vector<AstcGrid>& AstcGridCandidates(uint8_t blockW, uint8_t blockH);

// Encodes an RGB8 image into independently padded ASTC blocks. `width`/`height` are
// rounded up to whole blocks by edge clamping, matching the ring planner's patch rects.
bool AstcEncodeRgb(
	const uint8_t* rgb, size_t strideBytes, int width, int height,
	uint8_t blockW, uint8_t blockH, std::vector<uint8_t>& outBlocks);

// Decodes blocks produced by AstcEncodeRgb (and any block in the same subset).
bool AstcDecodeRgb(
	const uint8_t* blocks, size_t blockBytes, int width, int height,
	uint8_t blockW, uint8_t blockH, std::vector<uint8_t>& outRgb);

// Exposed for tests: decodes one block and reports the parsed header fields.
struct AstcBlockInfo {
	uint16_t blockMode = 0;
	uint8_t partitionCount = 0;
	uint8_t gridX = 0;
	uint8_t gridY = 0;
	bool dualPlane = false;
	uint8_t weightQuant = 0;
	uint32_t weightBits = 0;
	uint8_t colorFormat = 0;
	int colorBits = 0;
	int colorQuant = -1;
	bool supported = false;
};
bool AstcInspectBlock(const uint8_t* block, uint8_t blockW, uint8_t blockH, AstcBlockInfo& info);

}  // namespace gxh1

#endif  // GXR_HYBRID_ASTC_SUBSET_H
