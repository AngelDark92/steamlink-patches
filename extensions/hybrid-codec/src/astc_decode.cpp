#include "astc_decode.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gxh1 {
namespace {

// Weight quantization tables used by this subset: the bits-only ISE levels, whose stored
// code is the table index (no trit/quint scramble). Values are the spec's bit replication
// to six bits, plus one above 32.
constexpr uint8_t kWeight2Unquant[2] = {0, 64};
constexpr uint8_t kWeight4Unquant[4] = {0, 21, 43, 64};
constexpr uint8_t kWeight8Unquant[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr uint8_t kWeight16Unquant[16] = {0, 4, 8, 12, 17, 21, 25, 29, 35, 39, 43, 47, 52, 56, 60, 64};

// ASTC quant_method values of the supported weight ranges.
constexpr uint8_t kQuant2 = 0;
constexpr uint8_t kQuant4 = 2;
constexpr uint8_t kQuant8 = 5;
constexpr uint8_t kQuant16 = 8;

int QuantBits(uint8_t quant) {
	switch (quant) {
	case kQuant2: return 1;
	case kQuant4: return 2;
	case kQuant8: return 3;
	case kQuant16: return 4;
	default: return 0;
	}
}

int QuantLevels(uint8_t quant) {
	const int bits = QuantBits(quant);
	return bits == 0 ? 0 : 1 << bits;
}

int WeightUnquant(uint8_t quant, int code) {
	switch (quant) {
	case kQuant2: return kWeight2Unquant[code & 1];
	case kQuant4: return kWeight4Unquant[code & 3];
	case kQuant8: return kWeight8Unquant[code & 7];
	case kQuant16: return kWeight16Unquant[code & 15];
	default: return 0;
	}
}

// Nearest stored code for an interpolation parameter in [0, 1] (the levels are not evenly
// spaced, so rounding t * (levels - 1) can pick the wrong neighbour).
int NearestWeightCode(uint8_t quant, float t) {
	const int levels = QuantLevels(quant);
	const float target = t * 64.0f;
	int best = 0;
	float bestDistance = 1e30f;
	for (int code = 0; code < levels; ++code) {
		const float distance = std::fabs(static_cast<float>(WeightUnquant(quant, code)) - target);
		if (distance < bestDistance) {
			bestDistance = distance;
			best = code;
		}
	}
	return best;
}

constexpr uint32_t kColorFormatRgb = 8;   // FMT_RGB: linear LDR direct RGB, 8-bit endpoints
constexpr uint32_t kColorQuant256 = 256;
// ASTC endpoint field space for one partition and one weight plane:
// 128 - 11 (block mode) - 2 (partition count) - 4 (endpoint format) = 111 bits.
constexpr int kSinglePartitionColorBits = 111;

struct BlockMode {
	uint32_t mode = 0;
	uint8_t gridX = 0;
	uint8_t gridY = 0;
	bool dualPlane = false;
	uint8_t quant = 0;
};

// Mirror of the ASTC 2D block-mode decode (format definition, not encoder source).
bool DecodeBlockMode2D(uint32_t blockMode, BlockMode& out) {
	unsigned baseQuant = (blockMode >> 4) & 1u;
	unsigned H = (blockMode >> 9) & 1u;
	unsigned D = (blockMode >> 10) & 1u;
	unsigned A = (blockMode >> 5) & 0x3u;
	unsigned wx = 0;
	unsigned wy = 0;

	if ((blockMode & 3u) != 0) {
		baseQuant |= (blockMode & 3u) << 1;
		unsigned B = (blockMode >> 7) & 3u;
		switch ((blockMode >> 2) & 3u) {
		case 0:
			wx = B + 4;
			wy = A + 2;
			break;
		case 1:
			wx = B + 8;
			wy = A + 2;
			break;
		case 2:
			wx = A + 2;
			wy = B + 8;
			break;
		case 3:
			B &= 1u;
			if (blockMode & 0x100u) {
				wx = B + 2;
				wy = A + 2;
			} else {
				wx = A + 2;
				wy = B + 6;
			}
			break;
		}
	} else {
		baseQuant |= ((blockMode >> 2) & 3u) << 1;
		if (((blockMode >> 2) & 3u) == 0) return false;
		unsigned B = (blockMode >> 9) & 3u;
		switch ((blockMode >> 7) & 3u) {
		case 0:
			wx = 12;
			wy = A + 2;
			break;
		case 1:
			wx = A + 2;
			wy = 12;
			break;
		case 2:
			wx = A + 6;
			wy = B + 6;
			D = 0;
			H = 0;
			break;
		case 3:
			switch ((blockMode >> 5) & 3u) {
			case 0:
				wx = 6;
				wy = 10;
				break;
			case 1:
				wx = 10;
				wy = 6;
				break;
			default:
				return false;
			}
			break;
		}
	}

	unsigned quant = (baseQuant - 2u) + 6u * H;
	const unsigned weightCount = wx * wy * (D + 1u);
	if (weightCount > 64 || quant > 11) return false;
	const int bitsPerWeight = QuantBits(static_cast<uint8_t>(quant));
	if (bitsPerWeight == 0) return false;
	const unsigned weightBits = weightCount * static_cast<unsigned>(bitsPerWeight);
	if (weightBits < 24u || weightBits > 96u) return false;

	out.mode = blockMode;
	out.gridX = static_cast<uint8_t>(wx);
	out.gridY = static_cast<uint8_t>(wy);
	out.dualPlane = D != 0;
	out.quant = static_cast<uint8_t>(quant);
	return true;
}

inline void SetBit(uint8_t* block, int pos, unsigned bit) {
	uint8_t& byte = block[pos >> 3];
	const uint8_t mask = static_cast<uint8_t>(1u << (pos & 7));
	if (bit) {
		byte = static_cast<uint8_t>(byte | mask);
	} else {
		byte = static_cast<uint8_t>(byte & ~mask);
	}
}

inline unsigned GetBit(const uint8_t* block, int pos) {
	return (block[pos >> 3] >> (pos & 7)) & 1u;
}

// ASTC writes weights as a reversed ISE stream: stream bit i lands on block bit 127-i.
// Endpoints are written directly at bit 17, LSB-first.
struct TexelWeight {
	int index[4] = {0, 0, 0, 0};
	int contribution[4] = {0, 0, 0, 0};
	int count = 0;
};

void BuildWeightInfill(uint8_t texelsX, uint8_t texelsY, uint8_t gridX, uint8_t gridY,
                       std::vector<TexelWeight>& out) {
	out.assign(static_cast<size_t>(texelsX) * texelsY, TexelWeight{});
	const unsigned tx = texelsX;
	const unsigned ty = texelsY;
	const unsigned gx = gridX;
	const unsigned gy = gridY;
	const unsigned weightsPerBlock = gx * gy;
	(void)weightsPerBlock;

	for (unsigned y = 0; y < ty; ++y) {
		for (unsigned x = 0; x < tx; ++x) {
			unsigned xw = ((((1024u + tx / 2u) / (tx - 1u)) * x * (gx - 1u)) + 32u) >> 6;
			unsigned yw = ((((1024u + ty / 2u) / (ty - 1u)) * y * (gy - 1u)) + 32u) >> 6;
			const unsigned xf = xw & 0xFu;
			const unsigned yf = yw & 0xFu;
			const unsigned xi = xw >> 4;
			const unsigned yi = yw >> 4;

			unsigned q[4];
			q[0] = xi + yi * gx;
			q[1] = q[0] + 1u;
			q[2] = q[0] + gx;
			q[3] = q[2] + 1u;

			const unsigned prod = xf * yf;
			unsigned c[4];
			c[3] = (prod + 8u) >> 4;
			c[1] = xf - c[3];
			c[2] = yf - c[3];
			c[0] = 16u - xf - yf + c[3];

			TexelWeight& tw = out[static_cast<size_t>(y) * tx + x];
			for (int i = 0; i < 4; ++i) {
				if (c[i] == 0) continue;
				if (q[i] >= gx * gy) continue;  // only reachable with zero contribution
				tw.index[tw.count] = static_cast<int>(q[i]);
				tw.contribution[tw.count] = static_cast<int>(c[i]);
				++tw.count;
			}
		}
	}
}

inline uint8_t Clamp8(int value) {
	return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

// Effective texel weight (0..64): the spec interpolates the decimated weight grid first and
// rounds, and only then mixes the endpoints.
inline int TexelWeight64(const TexelWeight& tw, uint8_t quant, const uint8_t* codes) {
	int acc = 0;
	for (int k = 0; k < tw.count; ++k) acc += tw.contribution[k] * WeightUnquant(quant, codes[tw.index[k]]);
	return (acc + 8) >> 4;
}

// LDR UNORM8 endpoint interpolation (ASTC decode_unorm8, also what astcenc writes for U8
// output): endpoints are bit-replicated to 16 bits, mixed, and the top 8 bits are kept.
inline uint8_t InterpolateUnorm8(int e0, int e1, int weight64) {
	const int c0 = e0 * 257;
	const int c1 = e1 * 257;
	return static_cast<uint8_t>(((c0 * (64 - weight64) + c1 * weight64 + 32) >> 6) >> 8);
}

void GatherBlock(const uint8_t* rgb, size_t stride, int width, int height, int bx, int by,
                 uint8_t blockW, uint8_t blockH, uint8_t* texels) {
	for (int y = 0; y < blockH; ++y) {
		const int sy = std::min(by + y, height - 1);
		for (int x = 0; x < blockW; ++x) {
			const int sx = std::min(bx + x, width - 1);
			const uint8_t* src = rgb + static_cast<size_t>(sy) * stride + static_cast<size_t>(sx) * 3u;
			uint8_t* dst = texels + (static_cast<size_t>(y) * blockW + x) * 3u;
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
		}
	}
}

double EvaluateGrid(const uint8_t* texels, uint8_t blockW, uint8_t blockH, uint8_t gridX, uint8_t gridY,
                    uint8_t quant, const std::vector<TexelWeight>& infill, uint8_t* endpointsOut,
                    uint8_t* weightsOut, double worstError) {
	const int count = blockW * blockH;
	const int nodes = gridX * gridY;
	const int levels = QuantLevels(quant);
	if (levels == 0) return 1e30;

	// Initial endpoints from the extreme luminance texels.
	int lo = 0, hi = 0;
	int loLuma = 1 << 30, hiLuma = -1;
	for (int i = 0; i < count; ++i) {
		const int luma = texels[i * 3] * 299 + texels[i * 3 + 1] * 587 + texels[i * 3 + 2] * 114;
		if (luma < loLuma) { loLuma = luma; lo = i; }
		if (luma > hiLuma) { hiLuma = luma; hi = i; }
	}
	int e0[3] = {texels[lo * 3], texels[lo * 3 + 1], texels[lo * 3 + 2]};
	int e1[3] = {texels[hi * 3], texels[hi * 3 + 1], texels[hi * 3 + 2]};

	// Target interpolation parameter per texel from the current endpoints.
	std::vector<float> target(static_cast<size_t>(count), 0.0f);
	std::vector<float> nodeTarget(static_cast<size_t>(nodes), 0.0f);
	std::vector<float> nodeWeightSum(static_cast<size_t>(nodes), 0.0f);
	uint8_t bestWeights[64] = {0};
	uint8_t bestEndpoints[6] = {0};
	double bestError = 1e30;

	for (int pass = 0; pass < 3; ++pass) {
		// Project texel colours onto the endpoint axis.
		for (int i = 0; i < count; ++i) {
			const float dr = static_cast<float>(e1[0] - e0[0]);
			const float dg = static_cast<float>(e1[1] - e0[1]);
			const float db = static_cast<float>(e1[2] - e0[2]);
			const float denom = dr * dr + dg * dg + db * db;
			float t = 0.0f;
			if (denom > 1e-6f) {
				t = ((texels[i * 3] - e0[0]) * dr + (texels[i * 3 + 1] - e0[1]) * dg +
				     (texels[i * 3 + 2] - e0[2]) * db) / denom;
			}
			target[i] = std::min(1.0f, std::max(0.0f, t));
		}

		std::fill(nodeTarget.begin(), nodeTarget.end(), 0.0f);
		std::fill(nodeWeightSum.begin(), nodeWeightSum.end(), 0.0f);
		for (int i = 0; i < count; ++i) {
			const TexelWeight& tw = infill[i];
			for (int k = 0; k < tw.count; ++k) {
				nodeTarget[tw.index[k]] += target[i] * static_cast<float>(tw.contribution[k]);
				nodeWeightSum[tw.index[k]] += static_cast<float>(tw.contribution[k]);
			}
		}

		uint8_t weights[64] = {0};
		for (int n = 0; n < nodes; ++n) {
			const float t = nodeWeightSum[n] > 0.0f ? nodeTarget[n] / nodeWeightSum[n] : 0.0f;
			weights[n] = static_cast<uint8_t>(NearestWeightCode(quant, t));
		}

		// Least-squares endpoint refit with the quantized weights.
		double a00 = 0, a01 = 0, a11 = 0, b0[3] = {0, 0, 0}, b1[3] = {0, 0, 0};
		for (int i = 0; i < count; ++i) {
			const TexelWeight& tw = infill[i];
			double wsum = 0.0;
			for (int k = 0; k < tw.count; ++k) {
				wsum += static_cast<double>(tw.contribution[k]) * WeightUnquant(quant, weights[tw.index[k]]);
			}
			const double t = wsum / (16.0 * 64.0);
			const double u = 1.0 - t;
			a00 += u * u;
			a01 += u * t;
			a11 += t * t;
			for (int c = 0; c < 3; ++c) {
				b0[c] += u * texels[i * 3 + c];
				b1[c] += t * texels[i * 3 + c];
			}
		}
		const double det = a00 * a11 - a01 * a01;
		if (std::fabs(det) >= 1e-9) {
			for (int c = 0; c < 3; ++c) {
				const double v0 = (b0[c] * a11 - b1[c] * a01) / det;
				const double v1 = (b1[c] * a00 - b0[c] * a01) / det;
				e0[c] = std::min(255, std::max(0, static_cast<int>(std::lround(v0))));
				e1[c] = std::min(255, std::max(0, static_cast<int>(std::lround(v1))));
			}
		}
		// Score the block in the order it is emitted: FMT_RGB with sum(e0) > sum(e1) would
		// decode through blue contraction, so the pair is swapped and the weights inverted
		// here. The inverted infill can round differently by one, so it must be scored as is.
		if (e0[0] + e0[1] + e0[2] > e1[0] + e1[1] + e1[2]) {
			for (int c = 0; c < 3; ++c) std::swap(e0[c], e1[c]);
			for (int n = 0; n < nodes; ++n) weights[n] = static_cast<uint8_t>(levels - 1 - weights[n]);
		}

		// Reconstruction error with exactly the decoder's arithmetic: weight infill first,
		// then UNORM8 endpoint interpolation.
		double error = 0.0;
		for (int i = 0; i < count; ++i) {
			const int weight = TexelWeight64(infill[i], quant, weights);
			for (int c = 0; c < 3; ++c) {
				const int diff = InterpolateUnorm8(e0[c], e1[c], weight) - texels[i * 3 + c];
				error += static_cast<double>(diff * diff);
			}
		}
		if (error < bestError) {
			bestError = error;
			std::memcpy(bestWeights, weights, static_cast<size_t>(nodes));
			for (int c = 0; c < 3; ++c) {
				bestEndpoints[c] = Clamp8(e0[c]);
				bestEndpoints[3 + c] = Clamp8(e1[c]);
			}
		}
	}

	if (bestError >= worstError) return bestError;
	std::memcpy(endpointsOut, bestEndpoints, 6);
	std::memcpy(weightsOut, bestWeights, static_cast<size_t>(nodes));
	return bestError;
}

int ColorQuantLevel(int colorBits) {
	// quant_mode_table[3][bits] for six endpoint integers; only power-of-two levels used.
	if (colorBits >= 48) return 256;
	return -1;
}

}  // namespace

uint32_t FindAstcBlockMode(uint8_t blockW, uint8_t blockH, uint8_t gridX, uint8_t gridY, uint8_t quant) {
	if (blockW == 0 || blockH == 0) return 0;
	if (gridX < 2 || gridY < 2) return 0;
	const int bitsPerWeight = QuantBits(quant);
	if (bitsPerWeight == 0) return 0;
	const unsigned weightCount = static_cast<unsigned>(gridX) * gridY;
	const unsigned weightBits = weightCount * static_cast<unsigned>(bitsPerWeight);
	if (weightBits < 24u || weightBits > 96u) return 0;
	if (kSinglePartitionColorBits - static_cast<int>(weightBits) < 48) return 0;
	if (gridX > blockW || gridY > blockH) return 0;

	for (uint32_t mode = 0; mode < 2048; ++mode) {
		BlockMode info;
		if (!DecodeBlockMode2D(mode, info)) continue;
		if (info.dualPlane) continue;
		if (info.gridX == gridX && info.gridY == gridY && info.quant == quant) return mode;
	}
	return 0;
}

const std::vector<AstcGrid>& AstcGridCandidates(uint8_t blockW, uint8_t blockH) {
	static thread_local std::vector<AstcGrid> cache;
	static thread_local uint32_t cachedKey = 0xFFFFFFFFu;
	const uint32_t key = (static_cast<uint32_t>(blockW) << 16) | blockH;
	if (key == cachedKey) return cache;
	cachedKey = key;
	cache.clear();

	// One candidate per weight precision (4, 3, 2 and 1 bits): the densest legal grid that
	// still leaves 48 bits for the 8-bit endpoints, preferring the aspect of the footprint.
	// Any grid up to the footprint is legal ASTC, not only power-of-two decimations, and
	// trading weight precision for density is what keeps fine strokes in the larger rings.
	// Four candidates bound the GPU search; the CPU reference searches the same set.
	static const uint8_t quants[4] = {kQuant16, kQuant8, kQuant4, kQuant2};
	for (uint8_t quant : quants) {
		const int bits = QuantBits(quant);
		AstcGrid best{};
		int bestCount = 0;
		double bestAspect = 1e30;
		for (int gy = 2; gy <= blockH; ++gy) {
			for (int gx = 2; gx <= blockW; ++gx) {
				const int count = gx * gy;
				const int weightBits = count * bits;
				if (weightBits < 24 || kSinglePartitionColorBits - weightBits < 48) continue;
				const double aspect = std::fabs(static_cast<double>(gx) / blockW - static_cast<double>(gy) / blockH);
				if (count < bestCount || (count == bestCount && aspect >= bestAspect)) continue;
				const uint32_t mode = FindAstcBlockMode(blockW, blockH, static_cast<uint8_t>(gx),
				                                        static_cast<uint8_t>(gy), quant);
				if (mode == 0) continue;
				best = AstcGrid{static_cast<uint8_t>(gx), static_cast<uint8_t>(gy), quant, static_cast<uint8_t>(bits), mode};
				bestCount = count;
				bestAspect = aspect;
			}
		}
		if (bestCount > 0) cache.push_back(best);
	}
	return cache;
}

bool AstcEncodeRgb(
	const uint8_t* rgb, size_t strideBytes, int width, int height,
	uint8_t blockW, uint8_t blockH, std::vector<uint8_t>& outBlocks) {
	outBlocks.clear();
	if (!rgb || width <= 0 || height <= 0) return false;
	if (blockW < 4 || blockH < 4 || blockW > 12 || blockH > 12) return false;
	const std::vector<AstcGrid>& grids = AstcGridCandidates(blockW, blockH);
	if (grids.empty()) return false;

	const int blocksX = (width + blockW - 1) / blockW;
	const int blocksY = (height + blockH - 1) / blockH;
	outBlocks.resize(static_cast<size_t>(blocksX) * blocksY * kAstcBlockBytes, 0);

	std::vector<std::vector<TexelWeight>> infills(grids.size());
	std::vector<TexelWeight> infill;
	uint8_t texels[12 * 12 * 3] = {};

	for (size_t g = 0; g < grids.size(); ++g) {
		BuildWeightInfill(blockW, blockH, grids[g].gridX, grids[g].gridY, infill);
		infills[g] = infill;
	}

	for (int by = 0; by < blocksY; ++by) {
		for (int bx = 0; bx < blocksX; ++bx) {
			GatherBlock(rgb, strideBytes, width, height, bx * blockW, by * blockH, blockW, blockH, texels);

			double bestError = 1e30;
			size_t bestGrid = 0;
			uint8_t bestEndpoints[6] = {};
			uint8_t bestWeights[64] = {};
			for (size_t g = 0; g < grids.size(); ++g) {
				uint8_t ge[6] = {}, gw[64] = {};
				const double error = EvaluateGrid(texels, blockW, blockH, grids[g].gridX, grids[g].gridY,
				                                  grids[g].quant, infills[g], ge, gw, bestError);
				if (error < bestError) {
					bestError = error;
					bestGrid = g;
					std::memcpy(bestEndpoints, ge, 6);
					std::memcpy(bestWeights, gw, static_cast<size_t>(grids[g].gridX * grids[g].gridY));
				}
			}

			uint8_t* block = outBlocks.data() + (static_cast<size_t>(by) * blocksX + bx) * kAstcBlockBytes;
			const uint32_t mode = grids[bestGrid].blockMode;

			// FMT_RGB stores the endpoints interleaved per channel pair: R0,R1,G0,G1,B0,B1.
			// astcenc's LDR path applies blue-uncontraction when sum(e0) > sum(e1) and then
			// swaps; keeping e0 <= e1 (and inverting the weights when we swap) means a
			// conformant decoder reconstructs exactly these colours.
			const int levels = QuantLevels(grids[bestGrid].quant);
			int e0[3] = {bestEndpoints[0], bestEndpoints[1], bestEndpoints[2]};
			int e1[3] = {bestEndpoints[3], bestEndpoints[4], bestEndpoints[5]};
			if (e0[0] + e0[1] + e0[2] > e1[0] + e1[1] + e1[2]) {
				for (int c = 0; c < 3; ++c) std::swap(e0[c], e1[c]);
				const int nodes = grids[bestGrid].gridX * grids[bestGrid].gridY;
				for (int n = 0; n < nodes; ++n) bestWeights[n] = static_cast<uint8_t>(levels - 1 - bestWeights[n]);
			}
			const uint8_t physical[6] = {static_cast<uint8_t>(e0[0]), static_cast<uint8_t>(e1[0]),
			                             static_cast<uint8_t>(e0[1]), static_cast<uint8_t>(e1[1]),
			                             static_cast<uint8_t>(e0[2]), static_cast<uint8_t>(e1[2])};

			for (int bit = 0; bit < 11; ++bit) SetBit(block, bit, (mode >> bit) & 1u);
			// partition_count - 1 == 0 already zeroed.
			for (int bit = 0; bit < 4; ++bit) SetBit(block, 11 + bit, 0u);
			for (int bit = 0; bit < 4; ++bit) SetBit(block, 13 + bit, (kColorFormatRgb >> bit) & 1u);

			// Six 8-bit endpoint values, LSB-first from bit 17.
			for (int i = 0; i < 6; ++i) {
				for (int bit = 0; bit < 8; ++bit) {
					SetBit(block, 17 + i * 8 + bit, (physical[i] >> bit) & 1u);
				}
			}

			// Weight ISE stream reversed into the top bits: stream bit i -> block bit 127-i.
			const int nodeCount = grids[bestGrid].gridX * grids[bestGrid].gridY;
			const int bits = grids[bestGrid].bits;
			for (int node = 0; node < nodeCount; ++node) {
				const uint8_t code = bestWeights[node];
				for (int bit = 0; bit < bits; ++bit) {
					const int streamBit = node * bits + bit;
					SetBit(block, 127 - streamBit, (code >> bit) & 1u);
				}
			}
		}
	}
	return true;
}

bool AstcInspectBlock(const uint8_t* block, uint8_t blockW, uint8_t blockH, AstcBlockInfo& info) {
	info = AstcBlockInfo{};
	if (!block || blockW < 4 || blockH < 4) return false;
	uint32_t mode = 0;
	for (int bit = 0; bit < 11; ++bit) mode |= GetBit(block, bit) << bit;
	info.blockMode = static_cast<uint16_t>(mode);
	uint32_t partitions = 0;
	for (int bit = 0; bit < 2; ++bit) partitions |= GetBit(block, 11 + bit) << bit;
	info.partitionCount = static_cast<uint8_t>(partitions + 1);
	uint32_t format = 0;
	for (int bit = 0; bit < 4; ++bit) format |= GetBit(block, 13 + bit) << bit;
	info.colorFormat = static_cast<uint8_t>(format);

	if ((mode & 0x1FFu) == 0x1FCu) {
		info.supported = false;  // constant-colour blocks are not emitted by this subset
		return true;
	}
	BlockMode decoded;
	if (!DecodeBlockMode2D(mode, decoded)) {
		info.supported = false;
		return true;
	}
	info.gridX = decoded.gridX;
	info.gridY = decoded.gridY;
	info.dualPlane = decoded.dualPlane;
	info.weightQuant = decoded.quant;
	const int bitsPerWeight = QuantBits(decoded.quant);
	info.weightBits = bitsPerWeight > 0
		? static_cast<uint32_t>(decoded.gridX) * decoded.gridY * static_cast<uint32_t>(bitsPerWeight)
		: 0u;
	info.colorBits = kSinglePartitionColorBits - static_cast<int>(info.weightBits);
	info.colorQuant = ColorQuantLevel(info.colorBits);
	info.supported = partitions == 0 && !decoded.dualPlane && format == kColorFormatRgb &&
	                 info.colorQuant == static_cast<int>(kColorQuant256) && info.gridX <= blockW &&
	                 info.gridY <= blockH && bitsPerWeight > 0;
	return true;
}

bool AstcDecodeRgb(
	const uint8_t* blocks, size_t blockBytes, int width, int height,
	uint8_t blockW, uint8_t blockH, std::vector<uint8_t>& outRgb) {
	outRgb.clear();
	if (!blocks || width <= 0 || height <= 0) return false;
	if (blockW < 4 || blockH < 4 || blockW > 12 || blockH > 12) return false;
	const int blocksX = (width + blockW - 1) / blockW;
	const int blocksY = (height + blockH - 1) / blockH;
	const size_t required = static_cast<size_t>(blocksX) * blocksY * kAstcBlockBytes;
	if (blockBytes < required) return false;

	outRgb.assign(static_cast<size_t>(width) * height * 3u, 0);
	std::vector<TexelWeight> infill;

	for (int by = 0; by < blocksY; ++by) {
		for (int bx = 0; bx < blocksX; ++bx) {
			const uint8_t* block = blocks + (static_cast<size_t>(by) * blocksX + bx) * kAstcBlockBytes;
			AstcBlockInfo info;
			if (!AstcInspectBlock(block, blockW, blockH, info)) return false;
			if (!info.supported) return false;

			uint8_t physical[6] = {};
			for (int i = 0; i < 6; ++i) {
				uint32_t value = 0;
				for (int bit = 0; bit < 8; ++bit) value |= GetBit(block, 17 + i * 8 + bit) << bit;
				physical[i] = static_cast<uint8_t>(value);
			}
			// FMT_RGB: R0,R1,G0,G1,B0,B1 with blue-uncontraction when endpoint 0 sums
			// higher than endpoint 1 (then the pair is swapped).
			int e0[3] = {physical[0], physical[2], physical[4]};
			int e1[3] = {physical[1], physical[3], physical[5]};
			if (e0[0] + e0[1] + e0[2] > e1[0] + e1[1] + e1[2]) {
				for (int c = 0; c < 3; ++c) {
					e0[c] = (e0[c] + e0[2]) >> 1;
					e1[c] = (e1[c] + e1[2]) >> 1;
				}
				e0[2] = physical[4];
				e1[2] = physical[5];
				for (int c = 0; c < 3; ++c) std::swap(e0[c], e1[c]);
			}
			const uint8_t endpoints[6] = {static_cast<uint8_t>(e0[0]), static_cast<uint8_t>(e0[1]),
			                               static_cast<uint8_t>(e0[2]), static_cast<uint8_t>(e1[0]),
			                               static_cast<uint8_t>(e1[1]), static_cast<uint8_t>(e1[2])};

			const int nodeCount = info.gridX * info.gridY;
			const int bits = QuantBits(info.weightQuant);
			uint8_t codes[64] = {};
			for (int node = 0; node < nodeCount; ++node) {
				uint32_t value = 0;
				for (int bit = 0; bit < bits; ++bit) value |= GetBit(block, 127 - (node * bits + bit)) << bit;
				codes[node] = static_cast<uint8_t>(value);
			}

				BuildWeightInfill(blockW, blockH, info.gridX, info.gridY, infill);
			for (int y = 0; y < blockH; ++y) {
				const int dy = by * blockH + y;
				if (dy >= height) break;
				for (int x = 0; x < blockW; ++x) {
					const int dx = bx * blockW + x;
					if (dx >= width) break;
					const int w = TexelWeight64(infill[static_cast<size_t>(y) * blockW + x], info.weightQuant, codes);
					uint8_t* dst = outRgb.data() + (static_cast<size_t>(dy) * width + dx) * 3u;
					for (int c = 0; c < 3; ++c) dst[c] = InterpolateUnorm8(endpoints[c], endpoints[3 + c], w);
				}
			}
		}
	}
	return true;
}

}  // namespace gxh1
