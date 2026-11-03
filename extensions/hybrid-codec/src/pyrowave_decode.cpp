#include "pyrowave_decode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace gxh1 {
const char* g_pwLastError = "ok";
int g_pwDebug = 0;
namespace {

constexpr float kAlpha = -1.586134342059924f;
constexpr float kBeta = -0.052980118572961f;
constexpr float kGamma = 0.882911075530934f;
constexpr float kDelta = 0.443506852043971f;
constexpr float kKappa = 1.230174104914001f;
constexpr float kInvKappa = 1.0f / kKappa;

constexpr int kMinImageSize = 4 << kPwDecompositionLevels;  // 128
constexpr int kAlignment = 1 << kPwDecompositionLevels;     // 32
constexpr int kMaxDimension = 16384;
constexpr int kWordsPerPacketLimit = 4095;

void WriteU16(uint8_t* p, uint16_t v) {
	p[0] = static_cast<uint8_t>(v);
	p[1] = static_cast<uint8_t>(v >> 8);
}

void WriteU32(uint8_t* p, uint32_t v) {
	p[0] = static_cast<uint8_t>(v);
	p[1] = static_cast<uint8_t>(v >> 8);
	p[2] = static_cast<uint8_t>(v >> 16);
	p[3] = static_cast<uint8_t>(v >> 24);
}

uint16_t ReadU16(const uint8_t* p) {
	return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t ReadU32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void ForwardLine(float* line, int n) {
	if (n < 2) return;
	const int ne = (n + 1) / 2;
	const int no = n / 2;
	// Reused across calls: per-line allocation was ~24k malloc/free pairs per megapixel.
	static thread_local std::vector<float> even;
	static thread_local std::vector<float> odd;
	even.resize(static_cast<size_t>(ne));
	odd.resize(static_cast<size_t>(no));
	for (int i = 0; i < ne; ++i) even[i] = line[2 * i];
	for (int i = 0; i < no; ++i) odd[i] = line[2 * i + 1];

	for (int i = 0; i < no; ++i) {
		const float right = even[std::min(i + 1, ne - 1)];
		odd[i] += kAlpha * (even[i] + right);
	}
	for (int i = 0; i < ne; ++i) {
		const float left = odd[std::max(i - 1, 0)];
		const float right = (i < no) ? odd[i] : odd[no - 1];
		even[i] += kBeta * (left + right);
	}
	for (int i = 0; i < no; ++i) {
		const float right = even[std::min(i + 1, ne - 1)];
		odd[i] += kGamma * (even[i] + right);
	}
	for (int i = 0; i < ne; ++i) {
		const float left = odd[std::max(i - 1, 0)];
		const float right = (i < no) ? odd[i] : odd[no - 1];
		even[i] += kDelta * (left + right);
	}
	for (int i = 0; i < ne; ++i) line[i] = even[i] * kInvKappa;
	for (int i = 0; i < no; ++i) line[ne + i] = odd[i] * kKappa;
}

void InverseLine(float* line, int n) {
	if (n < 2) return;
	const int ne = (n + 1) / 2;
	const int no = n / 2;
	// Reused across calls: per-line allocation was ~24k malloc/free pairs per megapixel.
	static thread_local std::vector<float> even;
	static thread_local std::vector<float> odd;
	even.resize(static_cast<size_t>(ne));
	odd.resize(static_cast<size_t>(no));
	for (int i = 0; i < ne; ++i) even[i] = line[i] * kKappa;
	for (int i = 0; i < no; ++i) odd[i] = line[ne + i] * kInvKappa;

	for (int i = 0; i < ne; ++i) {
		const float left = odd[std::max(i - 1, 0)];
		const float right = (i < no) ? odd[i] : odd[no - 1];
		even[i] -= kDelta * (left + right);
	}
	for (int i = 0; i < no; ++i) {
		const float right = even[std::min(i + 1, ne - 1)];
		odd[i] -= kGamma * (even[i] + right);
	}
	for (int i = 0; i < ne; ++i) {
		const float left = odd[std::max(i - 1, 0)];
		const float right = (i < no) ? odd[i] : odd[no - 1];
		even[i] -= kBeta * (left + right);
	}
	for (int i = 0; i < no; ++i) {
		const float right = even[std::min(i + 1, ne - 1)];
		odd[i] -= kAlpha * (even[i] + right);
	}
	for (int i = 0; i < ne; ++i) line[2 * i] = even[i];
	for (int i = 0; i < no; ++i) line[2 * i + 1] = odd[i];
}

// Row-major mapping for the 8x8 subblock pixel order (spec: 4x2 subblocks, element order
// within a subblock is 0,2,4,6 / 1,3,5,7).
void Unswizzle8x8(int index, int& x, int& y) {
	const unsigned u = static_cast<unsigned>(index);
	y = static_cast<int>((u & 1u) | ((u >> 3) & 3u) << 1);
	x = static_cast<int>((u >> 1) & 3u) | (static_cast<int>((u >> 5) & 1u) << 2);
}

struct BlockRef {
	int level = 0;
	int component = 0;
	int band = 0;
	int blockX = 0;
	int blockY = 0;
};

struct BandLayout {
	int width = 0;
	int height = 0;
	int blocksX = 0;
	int blocksY = 0;
	int originX = 0;
	int originY = 0;
};

BandLayout Layout(int level, int band, int alignedW, int alignedH) {
	BandLayout layout;
	layout.width = alignedW >> (level + 1);
	layout.height = alignedH >> (level + 1);
	layout.blocksX = (layout.width + 31) / 32;
	layout.blocksY = (layout.height + 31) / 32;
	layout.originX = (band == 1 || band == 3) ? layout.width : 0;
	layout.originY = (band == 2 || band == 3) ? layout.height : 0;
	return layout;
}

std::vector<BlockRef> BuildBlockTable(int alignedW, int alignedH) {
	std::vector<BlockRef> table;
	for (int level = kPwDecompositionLevels - 1; level >= 0; --level) {
		for (int component = 0; component < 3; ++component) {
			for (int band = (level == kPwDecompositionLevels - 1 ? 0 : 1); band < 4; ++band) {
				const BandLayout layout = Layout(level, band, alignedW, alignedH);
				for (int y = 0; y < layout.blocksY; ++y) {
					for (int x = 0; x < layout.blocksX; ++x) {
						table.push_back(BlockRef{level, component, band, x, y});
					}
				}
			}
		}
	}
	return table;
}

inline float Clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

struct PendingBlock {
	bool present = false;
	uint16_t ballot = 0;
	uint8_t qscale[16] = {};
	uint16_t codewords[16] = {};
	uint8_t bits[16][8] = {};
	int32_t values[16][64] = {};
	bool negative[16][64] = {};
	uint32_t payloadBytes = 0;
	uint32_t signBytes = 0;
	uint32_t packetWords = 0;
};

int BitsFor(int32_t magnitude) {
	int bits = 0;
	while (magnitude > 0) {
		++bits;
		magnitude >>= 1;
	}
	return bits;
}

int PopCount(uint32_t value) {
	int count = 0;
	while (value != 0) {
		value &= value - 1u;
		++count;
	}
	return count;
}

}  // namespace

uint32_t PwAlign(int value, int alignment) {
	return static_cast<uint32_t>((value + alignment - 1) & ~(alignment - 1));
}

float PwBlock32x32Scale(uint8_t quantCode) {
	const int maxScaleExp = 4;
	const int e = maxScaleExp - (quantCode >> 3);
	const int m = quantCode & 0x7;
	const int exponent = 20 + e;
	if (exponent < 0 || exponent > 30) {
		// Codes 200..255 would ask for a negative or absurd shift; they are outside the
		// defined scale range and must never be selected or evaluated.
		return 0.0f;
	}
	const float invQuant = (1.0f / (8.0f * 1024.0f * 1024.0f)) * static_cast<float>((8 + m) << exponent);
	return invQuant;
}

float PwBlock8x8Scale(uint8_t code) {
	return static_cast<float>(code) / 8.0f + 0.25f;
}

void PwForwardLine(std::vector<float>& line, int count) {
	if (static_cast<int>(line.size()) >= count) ForwardLine(line.data(), count);
}

void PwInverseLine(std::vector<float>& line, int count) {
	if (static_cast<int>(line.size()) >= count) InverseLine(line.data(), count);
}

void PwForwardTransform(std::vector<float>& plane, int width, int height) {
	if (width <= 0 || height <= 0) return;
	std::vector<float> line(static_cast<size_t>(std::max(width, height)));
	for (int level = 0; level < kPwDecompositionLevels; ++level) {
		const int w = width >> level;
		const int h = height >> level;
		if (w < 2 || h < 2) break;
		for (int y = 0; y < h; ++y) {
			float* row = plane.data() + static_cast<size_t>(y) * width;
			std::memcpy(line.data(), row, static_cast<size_t>(w) * sizeof(float));
			ForwardLine(line.data(), w);
			std::memcpy(row, line.data(), static_cast<size_t>(w) * sizeof(float));
		}
		for (int x = 0; x < w; ++x) {
			for (int y = 0; y < h; ++y) line[static_cast<size_t>(y)] = plane[static_cast<size_t>(y) * width + x];
			ForwardLine(line.data(), h);
			for (int y = 0; y < h; ++y) plane[static_cast<size_t>(y) * width + x] = line[static_cast<size_t>(y)];
		}
	}
}

void PwInverseTransform(std::vector<float>& plane, int width, int height) {
	if (width <= 0 || height <= 0) return;
	std::vector<float> line(static_cast<size_t>(std::max(width, height)));
	for (int level = kPwDecompositionLevels - 1; level >= 0; --level) {
		const int w = width >> level;
		const int h = height >> level;
		if (w < 2 || h < 2) continue;
		for (int x = 0; x < w; ++x) {
			for (int y = 0; y < h; ++y) line[static_cast<size_t>(y)] = plane[static_cast<size_t>(y) * width + x];
			InverseLine(line.data(), h);
			for (int y = 0; y < h; ++y) plane[static_cast<size_t>(y) * width + x] = line[static_cast<size_t>(y)];
		}
		for (int y = 0; y < h; ++y) {
			float* row = plane.data() + static_cast<size_t>(y) * width;
			std::memcpy(line.data(), row, static_cast<size_t>(w) * sizeof(float));
			InverseLine(line.data(), w);
			std::memcpy(row, line.data(), static_cast<size_t>(w) * sizeof(float));
		}
	}
}

static bool PwFail(int line) {
	static char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "pw_fail_%d", line);
	g_pwLastError = buffer;
	return false;
}

bool PwEncodeRgb(const uint8_t* rgb, size_t strideBytes, int width, int height,
                 const PwEncodeOptions& options, std::vector<uint8_t>& out, PwStats* stats) {
	out.clear();
	if (!rgb || width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension) return PwFail(__LINE__);
	const uint32_t alignedW = std::max<uint32_t>(PwAlign(width, kAlignment), kMinImageSize);
	const uint32_t alignedH = std::max<uint32_t>(PwAlign(height, kAlignment), kMinImageSize);
	const size_t pixels = static_cast<size_t>(alignedW) * alignedH;
	if (pixels > 16384ull * 16384ull) return PwFail(__LINE__);

	// The default base step is scale32(quantCode) with the 8x8 nibble at its minimum (0.25),
	// kept from the original rate model. Each packet re-expresses its step as a 32x32 code
	// with the nibble at 1.0, so steps are exact and every dropped plane is exactly one code
	// octave (code - 8): the decoder's step always equals the quantizer's.
	const float baseStep = PwBlock32x32Scale(options.quantCode) * PwBlock8x8Scale(0);
	if (!(baseStep > 0.0f)) return PwFail(__LINE__);
	auto nearestCode = [](float step) {
		int best = -1;
		float bestDistance = std::numeric_limits<float>::max();
		for (int candidate = 0; candidate < 256; ++candidate) {
			const float scale = PwBlock32x32Scale(static_cast<uint8_t>(candidate));
			if (!(scale > 0.0f)) continue;
			const float distance = std::fabs(std::log2(scale / step));
			if (distance < bestDistance) {
				bestDistance = distance;
				best = candidate;
			}
		}
		return best;
	};
	constexpr uint8_t kUnitNibble = 6;  // PwBlock8x8Scale(6) == 1.0

	// Convert to YCbCr 4:4:4 with the DC shift removed, then transform each plane.
	std::vector<float> planes[3];
	for (int c = 0; c < 3; ++c) planes[c].assign(pixels, 0.0f);
	for (int y = 0; y < height; ++y) {
		const uint8_t* src = rgb + static_cast<size_t>(y) * strideBytes;
		for (int x = 0; x < width; ++x) {
			const float r = src[x * 3 + 0] / 255.0f;
			const float g = src[x * 3 + 1] / 255.0f;
			const float b = src[x * 3 + 2] / 255.0f;
			const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
			const size_t index = static_cast<size_t>(y) * alignedW + x;
			planes[0][index] = luma - 0.5f;
			planes[1][index] = (b - luma) / 1.8556f;
			planes[2][index] = (r - luma) / 1.5748f;
		}
	}
	// Clamp-to-edge extension on the right and bottom edges.
	for (int y = 0; y < static_cast<int>(alignedH); ++y) {
		const int sy = std::min(y, height - 1);
		for (int x = width; x < static_cast<int>(alignedW); ++x) {
			const size_t dst = static_cast<size_t>(y) * alignedW + x;
			const size_t srcIndex = static_cast<size_t>(sy) * alignedW + (width - 1);
			for (int c = 0; c < 3; ++c) planes[c][dst] = planes[c][srcIndex];
		}
		if (y >= height) {
			const size_t rowStart = static_cast<size_t>(y) * alignedW;
			const size_t srcStart = static_cast<size_t>(sy) * alignedW;
			for (int c = 0; c < 3; ++c) {
				std::memcpy(planes[c].data() + rowStart, planes[c].data() + srcStart,
				            static_cast<size_t>(alignedW) * sizeof(float));
			}
		}
	}

	for (int c = 0; c < 3; ++c) PwForwardTransform(planes[c], static_cast<int>(alignedW), static_cast<int>(alignedH));

	const std::vector<BlockRef> table = BuildBlockTable(static_cast<int>(alignedW), static_cast<int>(alignedH));

	// Quantize every 8x8 magnitude first so the rate controller can pick a global plane drop.
	struct BlockState {
		uint8_t code = 0;  // 32x32 quant code of this packet before any plane drop
		uint16_t ballot = 0;
		uint8_t subBits[16][8] = {};
		int32_t q[16][64] = {};
		bool negative[16][64] = {};
		int maxBits = 0;
		int minBits = 32;
	};
	std::vector<BlockState> states(table.size());
	float cachedMultiplier[8] = {};
	int cachedCode[8] = {};
	int cachedCodes = 0;

	for (size_t i = 0; i < table.size(); ++i) {
		const BlockRef& ref = table[i];
		const BandLayout layout = Layout(ref.level, ref.band, static_cast<int>(alignedW), static_cast<int>(alignedH));
		BlockState& state = states[i];
		float multiplier = 1.0f;
		if (options.stepScale) {
			// Image-space support of this packet: band coordinates scale by 2^(level+1), and
			// the 9/7 synthesis filters spread each coefficient by about nine samples per level,
			// so a coefficient near a finer ring still gets that ring's step.
			const int spacing = 1 << (ref.level + 1);
			const int support = 10 << ref.level;
			const int x0 = ref.blockX * 32 * spacing - support;
			const int y0 = ref.blockY * 32 * spacing - support;
			const int x1 = (ref.blockX * 32 + 32) * spacing + support;
			const int y1 = (ref.blockY * 32 + 32) * spacing + support;
			multiplier = options.stepScale(options.stepContext, ref.level, ref.band, x0, y0, x1, y1);
			if (multiplier == 0.0f) continue;  // band not sent in this region: ballot stays empty
			if (!(multiplier >= 1.0f) || !std::isfinite(multiplier)) multiplier = 1.0f;
		}
		// Only a handful of distinct ring multipliers exist; memoize their codes.
		int code = -1;
		for (int c = 0; c < cachedCodes; ++c) {
			if (cachedMultiplier[c] == multiplier) code = cachedCode[c];
		}
		if (code < 0) {
			code = nearestCode(baseStep * multiplier);
			if (code < 0) return PwFail(__LINE__);
			if (cachedCodes < 8) {
				cachedMultiplier[cachedCodes] = multiplier;
				cachedCode[cachedCodes] = code;
				++cachedCodes;
			}
		}
		state.code = static_cast<uint8_t>(code);
		const float step = PwBlock32x32Scale(state.code);
		for (int by8 = 0; by8 < 16; ++by8) {
			const int blockX = by8 % 4;
			const int blockY = by8 / 4;
			bool any = false;
			for (int index = 0; index < 64; ++index) {
				int sx = 0, sy = 0;
				Unswizzle8x8(index, sx, sy);
				const int localX = ref.blockX * 32 + blockX * 8 + sx;
				const int localY = ref.blockY * 32 + blockY * 8 + sy;
				float coefficient = 0.0f;
				if (localX < layout.width && localY < layout.height) {
					const int gx = layout.originX + localX;
					const int gy = layout.originY + localY;
					coefficient = planes[ref.component][static_cast<size_t>(gy) * alignedW + gx];
				}
				// Dead-zone (truncating) quantizer: the decoder reconstructs at the middle of
				// the bin, (q + 0.5) * step, so rounding here would bias every coefficient.
				const float magnitude = std::fabs(coefficient) / step;
				int32_t quantized = static_cast<int32_t>(std::floor(magnitude));
				// The packet's per-subblock field carries at most 18 planes; saturating here
				// keeps the encoder inside the format instead of overflowing a 2-bit code.
				if (quantized > 262143) quantized = 262143;
				state.q[by8][index] = quantized;
				state.negative[by8][index] = coefficient < 0.0f;
				if (quantized != 0) {
					any = true;
				}
				const int bits = BitsFor(quantized);
				if (bits > state.subBits[by8][(index >> 3)]) state.subBits[by8][(index >> 3)] = static_cast<uint8_t>(bits);
			}
			if (any) state.ballot |= static_cast<uint16_t>(1u << by8);
		}
		if (state.ballot == 0) continue;
		state.maxBits = 0;
		state.minBits = 32;
		for (int by8 = 0; by8 < 16; ++by8) {
			if ((state.ballot & (1u << by8)) == 0) continue;
			for (int s = 0; s < 8; ++s) {
				state.maxBits = std::max<int>(state.maxBits, state.subBits[by8][s]);
				state.minBits = std::min<int>(state.minBits, state.subBits[by8][s]);
			}
		}
	}

	// Pick a global plane drop. Dropping k low bitplanes multiplies the effective step by
	// 2^k, and the per-32x32 quant code plus the per-8x8 scale nibble can represent that step
	// only for k <= 15; deeper budgets are reported as infeasible rather than mis-scaled.
	// A drop of k planes lowers every packet code by 8k, which must stay a valid code.
	int maxDrop = 15;
	for (const BlockState& state : states) {
		if (state.ballot != 0) maxDrop = std::min(maxDrop, state.code / 8);
	}
	int drop = 0;
	if (options.maxBytes != 0) {
		for (drop = 0; drop <= maxDrop; ++drop) {
			size_t bytes = 0;
			for (const BlockState& state : states) {
				if (state.ballot == 0) continue;
				size_t bits = 0;
				size_t signs = 0;
				for (int by8 = 0; by8 < 16; ++by8) {
					if ((state.ballot & (1u << by8)) == 0) continue;
					int maxSub = 0;
					for (int s = 0; s < 8; ++s) maxSub = std::max<int>(maxSub, state.subBits[by8][s]);
					const int base = std::min(15, std::max(0, maxSub - drop - 3));
					for (int s = 0; s < 8; ++s) {
						const int total = std::max(base, static_cast<int>(state.subBits[by8][s]) - drop);
						bits += static_cast<size_t>(total);
						for (int e = 0; e < 8; ++e) {
							if ((state.q[by8][s * 8 + e] >> drop) != 0) ++signs;
						}
					}
				}
				bytes += 8 + 3 * static_cast<size_t>(PopCount(state.ballot)) + bits + (signs + 7) / 8;
			}
			if (bytes <= options.maxBytes) break;
		}
		if (drop > maxDrop) drop = maxDrop;
	} else {
		drop = std::min(maxDrop, std::max(0, 18 - static_cast<int>(options.baseBitplanes)));
	}

	// Emit start-of-frame header.
	const uint32_t first = (static_cast<uint32_t>(width - 1) & 0x3FFFu) |
	                       ((static_cast<uint32_t>(height - 1) & 0x3FFFu) << 14) | (1u << 31);
	const uint32_t second = 0;  // total_blocks patched after counting; code=0, 4:4:4, sRGB
	out.resize(8);
	WriteU32(out.data(), first);
	WriteU32(out.data() + 4, second);

	// Dropping k low bitplanes multiplies the step by exactly 2^k: the packet's code drops by
	// 8k (one exponent per plane) and the nibble stays at 1.0.
	uint32_t blockCount = 0;
	for (size_t i = 0; i < table.size(); ++i) {
		const BlockState& state = states[i];
		if (state.ballot == 0) continue;
		const uint8_t packetQuant = static_cast<uint8_t>(state.code - 8 * drop);

		std::vector<uint8_t> payload;
		std::vector<uint8_t> signs;
		uint8_t signByte = 0;
		int signCount = 0;

		const int compacted = PopCount(state.ballot);
		std::vector<uint16_t> codewords(static_cast<size_t>(compacted), 0);
		std::vector<uint8_t> qscale(static_cast<size_t>(compacted), 0);
		int compactIndex = -1;
		for (int by8 = 0; by8 < 16; ++by8) {
			if ((state.ballot & (1u << by8)) == 0) continue;
			++compactIndex;
			int maxSub = 0;
			for (int s = 0; s < 8; ++s) maxSub = std::max<int>(maxSub, state.subBits[by8][s]);
			const int base = std::min(15, std::max(0, maxSub - drop - 3));
			int totalBits[8] = {};
			uint16_t words = 0;
			for (int s = 0; s < 8; ++s) {
				// Every subblock of this 8x8 carries the value q >> drop. A subblock with
				// fewer significant planes is left-padded with zero planes up to the shared
				// base; its value is never shifted, because the decoder applies one scale to
				// the whole 8x8.
				const int total = std::max(base, static_cast<int>(state.subBits[by8][s]) - drop);
				const int code = total - base;
				words |= static_cast<uint16_t>(code << (2 * s));
				totalBits[s] = total;
			}
			codewords[static_cast<size_t>(compactIndex)] = words;
			qscale[static_cast<size_t>(compactIndex)] = static_cast<uint8_t>((kUnitNibble << 4) | (base & 0x0F));
			for (int s = 0; s < 8; ++s) {
				for (int plane = totalBits[s] - 1; plane >= 0; --plane) {
					const int sourcePlane = plane + drop;
					uint8_t packed = 0;
					if (sourcePlane < 31) {
						for (int e = 0; e < 8; ++e) {
							const int index = s * 8 + e;
							const int32_t value = state.q[by8][index];
							const int bit = (value >> sourcePlane) & 1;
							packed |= static_cast<uint8_t>(bit << e);
						}
					}
					payload.push_back(packed);
				}
				for (int e = 0; e < 8; ++e) {
					const int index = s * 8 + e;
					const int32_t retained = state.q[by8][index] >> drop;
					if (retained == 0) continue;
					if (state.negative[by8][index]) signByte |= static_cast<uint8_t>(1u << signCount);
					if (++signCount == 8) {
						signs.push_back(signByte);
						signByte = 0;
						signCount = 0;
					}
				}
			}
		}
		if (signCount > 0) signs.push_back(signByte);

		const uint32_t payloadBytes = static_cast<uint32_t>(payload.size());
		const uint32_t signBytes = static_cast<uint32_t>(signs.size());
		uint32_t words = (8u + 2u * static_cast<uint32_t>(compacted) + static_cast<uint32_t>(compacted) +
		                  payloadBytes + signBytes + 3u) / 4u;
		if (words > kWordsPerPacketLimit) return PwFail(__LINE__);

		const size_t start = out.size();
		out.resize(start + static_cast<size_t>(words) * 4, 0);
		uint8_t* packet = out.data() + start;
		WriteU16(packet, state.ballot);
		WriteU16(packet + 2, static_cast<uint16_t>((words & 0x0FFF) | (0u << 12)));
		WriteU32(packet + 4, (static_cast<uint32_t>(packetQuant) & 0xFFu) |
		                         ((static_cast<uint32_t>(i) & 0xFFFFFFu) << 8));
		uint8_t* cursor = packet + 8;
		for (uint16_t word : codewords) {
			WriteU16(cursor, word);
			cursor += 2;
		}
		for (uint8_t q : qscale) *cursor++ = q;
		std::memcpy(cursor, payload.data(), payloadBytes);
		cursor += payloadBytes;
		std::memcpy(cursor, signs.data(), signBytes);
		++blockCount;
	}

	WriteU32(out.data() + 4, (blockCount & 0xFFFFFFu) | (1u << 26));  // 4:4:4, sRGB, BT.709, full range
	if (stats) {
		stats->width = static_cast<uint32_t>(width);
		stats->height = static_cast<uint32_t>(height);
		stats->alignedWidth = alignedW;
		stats->alignedHeight = alignedH;
		stats->blocks = blockCount;
		stats->bytes = static_cast<uint32_t>(out.size());
		stats->quantCode = options.quantCode;
		stats->droppedPlanes = static_cast<uint8_t>(drop);
		stats->budgetExceeded = options.maxBytes != 0 && out.size() > options.maxBytes;
	}
	return true;
}

bool PwDecodeRgb(const uint8_t* data, size_t bytes, std::vector<uint8_t>& rgb, int& width, int& height) {
	return PwDecodeRgbChecked(data, bytes, 0, 0, 0xFFFFFFFFu, rgb, width, height);
}

bool PwDecodeRgbChecked(const uint8_t* data, size_t bytes, int expectedWidth, int expectedHeight,
                        uint32_t maxPixels, std::vector<uint8_t>& rgb, int& width, int& height) {
	rgb.clear();
	if (!data || bytes < 8) return PwFail(__LINE__);
	const uint32_t first = ReadU32(data);
	const uint32_t second = ReadU32(data + 4);
	if (((first >> 31) & 1u) == 0) return PwFail(__LINE__);
	const int sequenceWidth = static_cast<int>((first & 0x3FFFu) + 1);
	const int sequenceHeight = static_cast<int>(((first >> 14) & 0x3FFFu) + 1);
	if (expectedWidth > 0 && (sequenceWidth != expectedWidth || sequenceHeight != expectedHeight)) {
		return PwFail(__LINE__);
	}
	{
		const uint32_t probeW = std::max<uint32_t>(PwAlign(sequenceWidth, kAlignment), kMinImageSize);
		const uint32_t probeH = std::max<uint32_t>(PwAlign(sequenceHeight, kAlignment), kMinImageSize);
		if (static_cast<uint64_t>(probeW) * probeH > maxPixels) return PwFail(__LINE__);
	}
	if (((second >> 24) & 3u) != 0) return PwFail(__LINE__);  // only code 0 is defined
	if (((second >> 26) & 1u) != 1u) return PwFail(__LINE__);  // only 4:4:4 is produced
	if (sequenceWidth <= 0 || sequenceHeight <= 0 || sequenceWidth > kMaxDimension ||
	    sequenceHeight > kMaxDimension) return PwFail(__LINE__);
	width = sequenceWidth;
	height = sequenceHeight;

	const uint32_t alignedW = std::max<uint32_t>(PwAlign(width, kAlignment), kMinImageSize);
	const uint32_t alignedH = std::max<uint32_t>(PwAlign(height, kAlignment), kMinImageSize);
	const size_t pixels = static_cast<size_t>(alignedW) * alignedH;
	std::vector<float> planes[3];
	for (int c = 0; c < 3; ++c) planes[c].assign(pixels, 0.0f);
	const std::vector<BlockRef> table = BuildBlockTable(static_cast<int>(alignedW), static_cast<int>(alignedH));

	size_t offset = 8;
	while (offset + 8 <= bytes) {
		const uint16_t ballot = ReadU16(data + offset);
		const uint16_t wordField = ReadU16(data + offset + 2);
		const uint32_t words = wordField & 0x0FFFu;
		if (words < 2) return PwFail(__LINE__);
		const size_t packetBytes = static_cast<size_t>(words) * 4;
		if (offset + packetBytes > bytes) return PwFail(__LINE__);
		const uint32_t quantField = ReadU32(data + offset + 4);
		const uint8_t quantCode = static_cast<uint8_t>(quantField & 0xFFu);
		const uint32_t blockIndex = (quantField >> 8) & 0xFFFFFFu;
		if (blockIndex >= table.size()) return PwFail(__LINE__);

		const int compacted = PopCount(ballot);
		const size_t tableBytes = static_cast<size_t>(compacted) * 2 + static_cast<size_t>(compacted);
		if (8 + tableBytes > packetBytes) return PwFail(__LINE__);

		const uint8_t* codewordBytes = data + offset + 8;
		const uint8_t* qscaleBytes = codewordBytes + static_cast<size_t>(compacted) * 2;
		const uint8_t* payload = qscaleBytes + compacted;
		const uint8_t* packetEnd = data + offset + packetBytes;

		// First pass: locate the sign section (one byte per magnitude bitplane).
		const uint8_t* signBase = nullptr;
		{
			const uint8_t* cursor = payload;
			for (int compact = 0; compact < compacted; ++compact) {
				const uint16_t words16 = ReadU16(codewordBytes + static_cast<size_t>(compact) * 2);
				const int baseBits = qscaleBytes[compact] & 0x0F;
				for (int s = 0; s < 8; ++s) {
					const int bits = baseBits + ((words16 >> (2 * s)) & 3);
					if (cursor + bits > packetEnd) return PwFail(__LINE__);
					cursor += bits;
				}
			}
			signBase = cursor;
			if (signBase > packetEnd) return PwFail(__LINE__);
		}

		const uint8_t* magnitudeCursor = payload;
		const uint8_t* signCursor = signBase;
		int signBit = 0;
		const BlockRef& ref = table[blockIndex];
		const BandLayout layout = Layout(ref.level, ref.band, static_cast<int>(alignedW), static_cast<int>(alignedH));
		const float scale32 = PwBlock32x32Scale(quantCode);

		int compact = -1;
		for (int by8 = 0; by8 < 16; ++by8) {
			if ((ballot & (1u << by8)) == 0) continue;
			++compact;
			const uint16_t words16 = ReadU16(codewordBytes + static_cast<size_t>(compact) * 2);
			const uint8_t qs = qscaleBytes[compact];
			const int baseBits = qs & 0x0F;
			const float scale = scale32 * PwBlock8x8Scale((qs >> 4) & 0x0F);
			int32_t values[64] = {};
			bool negative[64] = {};

			for (int s = 0; s < 8; ++s) {
				const int bits = baseBits + ((words16 >> (2 * s)) & 3);
				if (bits > 18) return PwFail(__LINE__);
				uint32_t magnitude[8] = {};
				for (int plane = 0; plane < bits; ++plane) {
					if (magnitudeCursor >= signCursor) return PwFail(__LINE__);
					const uint8_t packed = *magnitudeCursor++;
					for (int e = 0; e < 8; ++e) {
						magnitude[e] = (magnitude[e] << 1) | ((packed >> e) & 1u);
					}
				}
				for (int e = 0; e < 8; ++e) {
					const int index = s * 8 + e;
					values[index] = static_cast<int32_t>(magnitude[e]);
				}
			}

			for (int index = 0; index < 64; ++index) {
				if (values[index] == 0) continue;
				const size_t byteIndex = static_cast<size_t>(signBit >> 3);
				if (signCursor + byteIndex >= packetEnd) return PwFail(__LINE__);
				negative[index] = ((signCursor[byteIndex] >> (signBit & 7)) & 1u) != 0;
				++signBit;
			}

			for (int index = 0; index < 64; ++index) {
				if (values[index] == 0) continue;
				float decoded = static_cast<float>(values[index]);
				if (negative[index]) decoded = -decoded;
				decoded += (decoded > 0.0f) ? 0.5f : -0.5f;
				const float dequantized = scale * decoded;
				int sx = 0, sy = 0;
				Unswizzle8x8(index, sx, sy);
				const int localX = ref.blockX * 32 + (by8 % 4) * 8 + sx;
				const int localY = ref.blockY * 32 + (by8 / 4) * 8 + sy;
				if (localX < layout.width && localY < layout.height) {
					const int gx = layout.originX + localX;
					const int gy = layout.originY + localY;
					planes[ref.component][static_cast<size_t>(gy) * alignedW + gx] = dequantized;
				}
			}
		}
		offset += packetBytes;
	}

	for (int c = 0; c < 3; ++c) PwInverseTransform(planes[c], static_cast<int>(alignedW), static_cast<int>(alignedH));

	rgb.assign(static_cast<size_t>(width) * height * 3u, 0);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const size_t index = static_cast<size_t>(y) * alignedW + x;
			const float luma = planes[0][index] + 0.5f;
			const float cb = planes[1][index];
			const float cr = planes[2][index];
			const float r = luma + 1.5748f * cr;
			const float b = luma + 1.8556f * cb;
			const float g = (luma - 0.2126f * r - 0.0722f * b) / 0.7152f;
			uint8_t* dst = rgb.data() + (static_cast<size_t>(y) * width + x) * 3u;
			dst[0] = static_cast<uint8_t>(Clamp01(r) * 255.0f + 0.5f);
			dst[1] = static_cast<uint8_t>(Clamp01(g) * 255.0f + 0.5f);
			dst[2] = static_cast<uint8_t>(Clamp01(b) * 255.0f + 0.5f);
		}
	}
	return true;
}

}  // namespace gxh1
