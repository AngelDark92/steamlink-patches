// PyroWave v1 CPU encoder/decoder (GXPW-CPU-v1).
//
// Implements the frozen v1 bitstream in pyrowave/bitstream/bitstream.md: intra-only,
// 5-level CDF 9/7 DWT with symmetric extension, 32x32 block packets with the 8-byte
// header, ballot-selected 8x8 blocks, 4x2 bitplane payload and packed sign bits, and the
// specified dequantization. It deliberately does NOT implement the Vulkan/Granite compute
// path: the host driver and the Android layer are dependency-free, so the codec runs on
// the CPU with bounded per-frame work.
//
// Fidelity: the lifting scheme follows ITU-T T.800 F.4.8.2. The transform is reversible in
// floating point and the decoder is the exact inverse of this encoder; bit-exact agreement
// with the upstream GPU implementation is neither claimed nor required (the spec states the
// decode is not bit-exact in general).
#ifndef GXR_HYBRID_PYROWAVE_CPU_H
#define GXR_HYBRID_PYROWAVE_CPU_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gxh1 {

constexpr uint8_t kPwQuantDefault = 135;  // step ~2^-13, the reference low-pass normalization
constexpr int kPwDecompositionLevels = 5;

// Last structural rejection reason, for tests and diagnostics.
extern const char* g_pwLastError;
extern int g_pwDebug;

struct PwEncodeOptions {
	uint8_t quantCode = kPwQuantDefault;
	// Soft rate ceiling for this image; 0 means "use the precision target only".
	uint32_t maxBytes = 0;
	// Precision target when maxBytes is 0: retained bitplanes per subblock.
	uint8_t baseBitplanes = 18;
	// Optional spatial rate weighting. Called once per 32x32 packet with its decomposition
	// level (0 = finest), band (0 = low-pass) and the image-space region its coefficients can
	// reach (including synthesis-filter support); returns the step multiplier (>= 1), or 0 to
	// leave the packet out (its coefficients decode as zero, i.e. that detail band is not
	// sent there). The decoder needs nothing extra: every packet carries its own quant code.
	float (*stepScale)(const void* context, int level, int band, int x0, int y0, int x1, int y1) = nullptr;
	const void* stepContext = nullptr;
};

struct PwStats {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t alignedWidth = 0;
	uint32_t alignedHeight = 0;
	uint32_t blocks = 0;
	uint32_t bytes = 0;
	uint8_t quantCode = 0;
	uint8_t droppedPlanes = 0;
	// True when the requested maxBytes could not be met. Callers must reject the
	// profile visibly instead of lowering base quality or dropping a ring.
	bool budgetExceeded = false;
};

uint32_t PwAlign(int value, int alignment);
float PwBlock32x32Scale(uint8_t quantCode);
float PwBlock8x8Scale(uint8_t code);

// Encodes an RGB8 image. Output is one start-of-frame packet followed by every non-zero
// 32x32 block. Returns false only on invalid input or an internal size overflow.
bool PwEncodeRgb(const uint8_t* rgb, size_t strideBytes, int width, int height,
                 const PwEncodeOptions& options, std::vector<uint8_t>& out, PwStats* stats);

// Decodes a sequence produced by PwEncodeRgb. `width`/`height` must match the sequence
// header. Bounded: every length is validated before use.
bool PwDecodeRgb(const uint8_t* data, size_t bytes, std::vector<uint8_t>& rgb,
                 int& width, int& height);

// Same decode, but the embedded dimensions and the aligned scratch size are checked against
// the caller's expected patch and a pixel ceiling before any allocation.
bool PwDecodeRgbChecked(const uint8_t* data, size_t bytes, int expectedWidth, int expectedHeight,
                        uint32_t maxPixels, std::vector<uint8_t>& rgb, int& width, int& height);

// Exposed for tests: YCbCr 4:4:4 round trip of one plane set.
void PwForwardTransform(std::vector<float>& plane, int width, int height);
void PwInverseTransform(std::vector<float>& plane, int width, int height);
void PwForwardLine(std::vector<float>& line, int count);
void PwInverseLine(std::vector<float>& line, int count);

}  // namespace gxh1

#endif  // GXR_HYBRID_PYROWAVE_CPU_H
