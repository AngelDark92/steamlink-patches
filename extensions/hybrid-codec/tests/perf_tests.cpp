// Client-side hot-path cost measurement for the GXH1 helper.
//
// Answers two questions with numbers on the host (x86-64, MSVC /O2; a phone core is slower,
// so treat these as lower bounds):
//   1. what an ordinary access unit costs when the helper is packaged but the PC is not
//      sending hybrid frames (the "dormant" path), and
//   2. what one selected ASTC / PyroWave envelope costs on the MediaCodec input thread
//      (parse + CRC + coverage + payload copy or inline decode), and how long the worker
//      takes to finish before the layer would be available.
#include "../src/astc_decode.h"
#include "../src/hybrid_session.h"
#include "../src/pyrowave_decode.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" int gxr_hybrid_on_access_unit(const uint8_t** data, size_t* size);
extern "C" int gxr_hybrid_take_layer(int eye, uint8_t* rgba, int width, int height, int stride, uint64_t* frameId);
extern "C" void gxr_hybrid_reset(uint64_t sessionEpoch);
extern "C" const char* gxr_hybrid_status(void);

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(const Clock::time_point& start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::vector<uint8_t> MakeCanvas(int width, int height) {
	std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4u, 255);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			uint8_t* p = rgba.data() + (static_cast<size_t>(y) * width + x) * 4u;
			const bool ink = (x % 11 < 3) || (y % 13 < 2);
			p[0] = ink ? 16 : static_cast<uint8_t>(70 + (x * 150) / width);
			p[1] = ink ? 200 : static_cast<uint8_t>(50 + (y * 160) / height);
			p[2] = ink ? 90 : static_cast<uint8_t>(190 - (x * 120) / width);
			p[3] = 255;
		}
	}
	return rgba;
}

std::vector<uint8_t> RgbaToRgb(const std::vector<uint8_t>& rgba) {
	std::vector<uint8_t> rgb(rgba.size() / 4u * 3u);
	for (size_t i = 0, j = 0; i + 3 < rgba.size(); i += 4, j += 3) {
		rgb[j + 0] = rgba[i + 0];
		rgb[j + 1] = rgba[i + 1];
		rgb[j + 2] = rgba[i + 2];
	}
	return rgb;
}

struct BuiltFrame {
	std::vector<uint8_t> wire;
	uint16_t canvasW = 0;
	uint16_t canvasH = 0;
	size_t foveaBytes = 0;
	size_t baseBytes = 0;
};

// 17 horizontal strips per eye tile the canvas exactly; 34 patches forces the worker path.
BuiltFrame BuildFrame(uint8_t codec, int stripHeight, int footprint, uint32_t patchBudget, uint64_t frameId) {
	const uint16_t canvasW = 500;
	const uint16_t canvasH = static_cast<uint16_t>(stripHeight * 17);
	const int eyes = 2;
	const int strips = 17;
	const size_t patchCount = static_cast<size_t>(eyes) * strips;

	const std::vector<uint8_t> canvas = MakeCanvas(canvasW, canvasH);
	const std::vector<uint8_t> rgb = RgbaToRgb(canvas);

	std::vector<std::vector<uint8_t>> payloads(patchCount);
	for (int eye = 0; eye < eyes; ++eye) {
		for (int s = 0; s < strips; ++s) {
			const int y = s * stripHeight;
			const int h = std::min<int>(stripHeight, canvasH - y);
			std::vector<uint8_t> strip(static_cast<size_t>(canvasW) * h * 3u);
			for (int row = 0; row < h; ++row) {
				std::memcpy(strip.data() + static_cast<size_t>(row) * canvasW * 3u,
				            rgb.data() + (static_cast<size_t>(y + row) * canvasW) * 3u,
				            static_cast<size_t>(canvasW) * 3u);
			}
			const size_t index = static_cast<size_t>(eye) * strips + s;
			if (codec == gxh1::kFoveaCodecAstc) {
				if (!gxh1::AstcEncodeRgb(strip.data(), static_cast<size_t>(canvasW) * 3u, canvasW, h,
				                         static_cast<uint8_t>(footprint), static_cast<uint8_t>(footprint),
				                         payloads[index])) {
					std::fprintf(stderr, "ASTC encode failed\n");
					return {};
				}
			} else {
				gxh1::PwEncodeOptions options;
				options.maxBytes = patchBudget;
				gxh1::PwStats stats;
				if (!gxh1::PwEncodeRgb(strip.data(), static_cast<size_t>(canvasW) * 3u, canvasW, h, options,
				                       payloads[index], &stats)) {
					std::fprintf(stderr, "PyroWave encode failed\n");
					return {};
				}
			}
		}
	}

	std::vector<uint8_t> base(1500000);
	uint32_t state = 0x12345678u;
	for (uint8_t& byte : base) {
		state = state * 1664525u + 1013904223u;
		byte = static_cast<uint8_t>(state >> 24);
	}

	BuiltFrame built;
	built.canvasW = canvasW;
	built.canvasH = canvasH;
	built.baseBytes = base.size();

	const size_t tableBytes = gxh1::kFoveaUnitHeaderBytes + patchCount * gxh1::kPatchEntryBytes;
	size_t foveaBytes = tableBytes;
	for (const std::vector<uint8_t>& payload : payloads) foveaBytes += payload.size();
	built.foveaBytes = foveaBytes;

	gxh1::EnvelopeHeader header{};
	header.magic = gxh1::kMagic;
	header.versionMajor = gxh1::kVersionMajor;
	header.versionMinor = gxh1::kVersionMinor;
	header.headerBytes = gxh1::kHeaderBytes;
	header.flags = gxh1::kFlagFoveaPresent | gxh1::kFlagExactCore;
	header.sessionEpoch = 7;
	header.sourceEpoch = 7;
	header.mappingEpoch = 7;
	header.frameId = frameId;
	header.baseBytes = static_cast<uint32_t>(base.size());
	header.foveaBytes = static_cast<uint32_t>(foveaBytes);
	header.baseOffset = gxh1::kHeaderBytes;
	header.foveaOffset = gxh1::kHeaderBytes + static_cast<uint32_t>(base.size());
	header.baseCodec = gxh1::kBaseCodecHevc;
	header.baseFormat = gxh1::kBaseFormatNv12;
	header.foveaCodec = codec;
	header.precision = gxh1::kFoveaPrecisionRgb8;
	header.canvasW = canvasW;
	header.canvasH = canvasH;
	header.patchCount = static_cast<uint16_t>(patchCount);
	header.featherPx = 8;

	gxh1::FoveaUnitHeader unit{};
	unit.unitBytes = static_cast<uint32_t>(foveaBytes);
	unit.canvasW = canvasW;
	unit.canvasH = canvasH;
	unit.foveaCodec = codec;
	unit.precision = gxh1::kFoveaPrecisionRgb8;
	unit.patchCount = static_cast<uint8_t>(patchCount);
	unit.mappingEpoch = 7;
	unit.featherPx = 8;
	for (int eye = 0; eye < 2; ++eye) {
		unit.eyes[eye].sourceW = static_cast<uint16_t>(canvasW);
		unit.eyes[eye].sourceH = static_cast<uint16_t>(canvasH);
		unit.eyes[eye].tanLeft = -1.0f;
		unit.eyes[eye].tanRight = 1.0f;
		unit.eyes[eye].tanTop = -1.0f;
		unit.eyes[eye].tanBottom = 1.0f;
		unit.eyes[eye].gazeX = canvasW * 0.5f;
		unit.eyes[eye].gazeY = canvasH * 0.5f;
	}

	built.wire.assign(gxh1::kHeaderBytes + base.size() + foveaBytes, 0);
	std::memcpy(built.wire.data(), &header, sizeof(header));
	std::memcpy(built.wire.data() + header.baseOffset, base.data(), base.size());
	std::memcpy(built.wire.data() + header.foveaOffset, &unit, sizeof(unit));
	size_t offset = tableBytes;
	for (size_t i = 0; i < patchCount; ++i) {
		const int eye = static_cast<int>(i / strips);
		const int s = static_cast<int>(i % strips);
		gxh1::PatchEntry entry{};
		entry.payloadBytes = static_cast<uint32_t>(payloads[i].size());
		entry.payloadOffset = static_cast<uint32_t>(offset);
		entry.x = 0;
		entry.y = static_cast<uint16_t>(s * stripHeight);
		entry.width = canvasW;
		entry.height = static_cast<uint16_t>(std::min<int>(stripHeight, canvasH - entry.y));
		entry.eye = static_cast<uint8_t>(eye);
		entry.annulus = static_cast<uint8_t>(std::min(4, s / 4 + 1));
		entry.blockW = static_cast<uint8_t>(footprint);
		entry.blockH = static_cast<uint8_t>(footprint);
		entry.codec = codec;
		entry.sourceEpoch = 7;
		entry.outerRadiusPx = 8;
		entry.outerDegrees = 5.0f;
		std::memcpy(built.wire.data() + header.foveaOffset + gxh1::kFoveaUnitHeaderBytes +
		                i * gxh1::kPatchEntryBytes,
		            &entry, sizeof(entry));
		std::memcpy(built.wire.data() + header.foveaOffset + offset, payloads[i].data(), payloads[i].size());
		offset += payloads[i].size();
	}

	auto* written = reinterpret_cast<gxh1::EnvelopeHeader*>(built.wire.data());
	written->crc32 = 0;
	uint32_t crc = gxh1::Crc32(built.wire.data(), sizeof(gxh1::EnvelopeHeader));
	crc = gxh1::Crc32(built.wire.data() + header.baseOffset, header.baseBytes, crc);
	crc = gxh1::Crc32(built.wire.data() + header.foveaOffset, header.foveaBytes, crc);
	written->crc32 = crc;
	return built;
}

// Re-seals a frame after its frameId changed.
void SetFrameId(std::vector<uint8_t>& wire, uint64_t frameId) {
	auto* header = reinterpret_cast<gxh1::EnvelopeHeader*>(wire.data());
	header->frameId = frameId;
	header->crc32 = 0;
	uint32_t crc = gxh1::Crc32(wire.data(), sizeof(gxh1::EnvelopeHeader));
	crc = gxh1::Crc32(wire.data() + header->baseOffset, header->baseBytes, crc);
	crc = gxh1::Crc32(wire.data() + header->foveaOffset, header->foveaBytes, crc);
	header->crc32 = crc;
}

void MeasureDormant() {
	std::vector<uint8_t> au(1024, 0x5A);
	const uint8_t* data = au.data();
	size_t size = au.size();
	const auto start = Clock::now();
	constexpr int kCalls = 1000000;
	int consumed = 0;
	for (int i = 0; i < kCalls; ++i) {
		consumed += gxr_hybrid_on_access_unit(&data, &size);
	}
	const double ms = MsSince(start);
	std::printf("dormant: %.3f ms / %d calls = %.1f ns per access unit (consumed=%d)\n", ms, kCalls,
	            ms * 1e6 / kCalls, consumed);
}

void MeasureParse(const BuiltFrame& frame, const char* label) {
	gxh1::HybridSession session;
	gxh1::ParseLimits limits;
	std::string reason;
	constexpr int kCalls = 20;
	const auto start = Clock::now();
	for (int i = 0; i < kCalls; ++i) {
		if (session.Admit(frame.wire.data(), frame.wire.size(), limits, &reason) != 0) {
			std::fprintf(stderr, "%s parse failed: %s\n", label, reason.c_str());
			return;
		}
	}
	const double ms = MsSince(start) / kCalls;
	std::printf("%s parse+CRC+coverage: %.3f ms per frame (base %.2f MB, fovea %.0f KB)\n", label,
	            ms, frame.baseBytes / 1e6, frame.foveaBytes / 1024.0);
}

void MeasureSelected(const BuiltFrame& frame, const char* label, uint64_t& nextFrameId, bool waitForLayer) {
	gxr_hybrid_reset(7);
	std::vector<uint8_t> layer(static_cast<size_t>(frame.canvasW) * frame.canvasH * 4u);
	constexpr int kFrames = 15;
	double inputMs = 0.0;
	int inputCount = 0;
	double inputMax = 0.0;
	double completeMs = 0.0;
	int completed = 0;
	for (int i = 0; i < kFrames; ++i) {
		const uint64_t fedFrame = nextFrameId++;
		SetFrameId(const_cast<std::vector<uint8_t>&>(frame.wire), fedFrame);
		const uint8_t* data = frame.wire.data();
		size_t size = frame.wire.size();
		const auto start = Clock::now();
		const int result = gxr_hybrid_on_access_unit(&data, &size);
		const double ms = MsSince(start);
		if (result != 1) {
			std::fprintf(stderr, "%s: on_access_unit returned %d\n", label, result);
			return;
		}
		// First frames include worker creation; report steady-state separately.
		if (i >= 2) {
			inputMs += ms;
			++inputCount;
			inputMax = std::max(inputMax, ms);
		}
		const auto waitStart = Clock::now();
		uint64_t frameId = 0;
		bool got = false;
		while (MsSince(waitStart) < 4000.0) {
			if (gxr_hybrid_take_layer(0, layer.data(), frame.canvasW, frame.canvasH, frame.canvasW * 4,
			                          &frameId) == 0 &&
			    frameId >= fedFrame) {
				got = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
		if (!got) {
			if (waitForLayer) {
				std::printf("%s: frame %llu not ready within 4 s (status=%s)\n", label,
				            static_cast<unsigned long long>(fedFrame), gxr_hybrid_status());
			}
			continue;
		}
		if (i >= 2) {
			completeMs += MsSince(waitStart);
			++completed;
		}
	}
	if (inputCount > 0) {
		if (completed > 0) {
			std::printf("%s selected: input thread %.3f ms avg (max %.3f), worker completes %.3f ms after return, "
			            "%zu patches, %.0f KB fovea, status=%s\n",
			            label, inputMs / inputCount, inputMax, completeMs / completed,
			            static_cast<size_t>(reinterpret_cast<const gxh1::FoveaUnitHeader*>(
			                                frame.wire.data() + gxh1::kHeaderBytes + frame.baseBytes)
			                                ->patchCount),
			            frame.foveaBytes / 1024.0, gxr_hybrid_status());
		} else {
			std::printf("%s selected: input thread %.3f ms avg (max %.3f), no decoded layer (status=%s)\n", label,
			            inputMs / inputCount, inputMax, gxr_hybrid_status());
		}
	}
}

}  // namespace

int main(int argc, char** argv) {
	const bool decodeMode = argc > 1 && std::strcmp(argv[1], "--decode") == 0;
	if (decodeMode) {
		// Must be set before the helper's session singleton is created by the first call.
		_putenv_s("GXR_HYBRID_CPU_DECODE", "1");
	}
	std::printf("Hybrid client hot-path measurements (x86-64 /O2; phone cores are slower), mode=%s\n",
	            decodeMode ? "cpu-decode" : "demux-only");
	MeasureDormant();

	uint64_t frameId = 1;
	BuiltFrame astc = BuildFrame(gxh1::kFoveaCodecAstc, 29, 8, 0, frameId++);
	BuiltFrame pyro = BuildFrame(gxh1::kFoveaCodecPyroWave, 29, 8, 96u * 1024u, frameId++);
	if (astc.wire.empty() || pyro.wire.empty()) return 2;

	MeasureParse(astc, "astc");
	MeasureParse(pyro, "pyrowave");
	MeasureSelected(astc, "astc", frameId, decodeMode);
	MeasureSelected(pyro, "pyrowave", frameId, decodeMode);

	// Inline path: a small two-patch frame stays on the input thread.
	{
		gxh1::HybridSession session;
		BuiltFrame small = BuildFrame(gxh1::kFoveaCodecAstc, 100, 8, 0, frameId++);
		// Only the first two patches would not tile the canvas, so keep the frame as built;
		// this measures the inline budget decision for a 17-strip frame instead.
		std::printf("inline probe skipped (frames above are all worker-path)\n");
		(void)small;
	}
	return 0;
}
