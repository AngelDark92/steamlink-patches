// Host tests for the GXH1 hybrid-codec helper: envelope construction/parsing,
// ASTC and PyroWave decode, epoch admission, fail-closed rejection and the C ABI.
//
// Everything is built from the copied encoders, so a change to the wire layout or to
// either codec fails here before it can reach a device. Compiled and run on the host
// with the portable MSVC; no Android dependencies.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "../protocol/gxh1.h"
#include "../src/astc_decode.h"
#include "../src/gxh1_parse.h"
#include "../src/hybrid_session.h"
#include "../src/pyrowave_decode.h"

extern "C" {
int gxr_hybrid_probe(void);
int gxr_hybrid_on_access_unit(const uint8_t** data, size_t* size);
int gxr_hybrid_take_layer(int eye, uint8_t* rgba, int width, int height, int stride, uint64_t* frameId);
void gxr_hybrid_reset(uint64_t sessionEpoch);
const char* gxr_hybrid_status(void);
}

// ---------------------------------------------------------------------------
// Allocation counter: proves gxr_hybrid_probe() only returns a cached bool.
// ---------------------------------------------------------------------------
namespace {
std::atomic<long> g_allocations{0};
}

void* operator new(std::size_t size) {
	g_allocations.fetch_add(1, std::memory_order_relaxed);
	void* p = std::malloc(size ? size : 1);
	if (!p) throw std::bad_alloc();
	return p;
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, label)                                              \
	do {                                                                \
		g_checks++;                                                     \
		if (!(cond)) {                                                  \
			g_failures++;                                               \
			std::printf("FAIL: %s (line %d)\n", label, __LINE__);       \
		}                                                               \
	} while (0)

namespace {

using gxh1::EnvelopeHeader;

struct PatchSpec {
	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t width = 1;
	uint16_t height = 1;
	uint8_t eye = 0;
	uint8_t blockW = 4;
	uint8_t blockH = 4;
	uint16_t texelW = 0;  // resampled ring: encoded grid smaller than the rect
	uint16_t texelH = 0;
	std::vector<uint8_t> payload;
};

struct EnvelopeSpec {
	uint32_t sessionEpoch = 1;
	uint32_t sourceEpoch = 1;
	uint32_t mappingEpoch = 1;
	uint64_t frameId = 1;
	uint16_t canvasW = 1;
	uint16_t canvasH = 1;
	uint16_t flags = gxh1::kFlagFoveaPresent;
	uint8_t foveaCodec = gxh1::kFoveaCodecAstc;
	uint8_t precision = gxh1::kFoveaPrecisionRgb8;
	std::vector<uint8_t> base = {0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xAA, 0x55};
	std::vector<PatchSpec> patches;
};

// Builds a complete, CRC-correct envelope: header | base AU | fovea unit.
std::vector<uint8_t> BuildEnvelope(const EnvelopeSpec& spec) {
	const size_t patchCount = spec.patches.size();
	const size_t tableBytes = patchCount * gxh1::kPatchEntryBytes;
	const bool fovea = spec.foveaCodec != gxh1::kFoveaCodecNone && patchCount > 0;

	std::vector<uint8_t> unit;
	if (fovea) {
		unit.assign(gxh1::kFoveaUnitHeaderBytes + tableBytes, 0);
		uint32_t cursor = static_cast<uint32_t>(unit.size());
		for (size_t i = 0; i < patchCount; ++i) {
			const PatchSpec& p = spec.patches[i];
			gxh1::PatchEntry entry{};
			entry.payloadBytes = static_cast<uint32_t>(p.payload.size());
			entry.payloadOffset = cursor;
			entry.x = p.x;
			entry.y = p.y;
			entry.width = p.width;
			entry.height = p.height;
			entry.eye = p.eye;
			entry.annulus = 0;
			entry.blockW = p.blockW;
			entry.blockH = p.blockH;
			entry.codec = spec.foveaCodec;
			entry.flags = gxh1::kPatchFlagDirty;
			entry.texelW = p.texelW;
			entry.texelH = p.texelH;
			entry.sourceEpoch = spec.sourceEpoch;
			entry.innerRadiusPx = 0;
			entry.outerRadiusPx = 100;
			entry.gutters = 0;
			entry.innerDegrees = 0.0f;
			entry.outerDegrees = 10.0f;
			entry.reserved1 = 0;
			std::memcpy(unit.data() + gxh1::kFoveaUnitHeaderBytes + i * gxh1::kPatchEntryBytes, &entry,
			            sizeof(entry));
			cursor += static_cast<uint32_t>(p.payload.size());
			unit.insert(unit.end(), p.payload.begin(), p.payload.end());
		}
		gxh1::FoveaUnitHeader header{};
		header.unitBytes = static_cast<uint32_t>(unit.size());
		header.canvasW = spec.canvasW;
		header.canvasH = spec.canvasH;
		header.foveaCodec = spec.foveaCodec;
		header.precision = spec.precision;
		header.patchCount = static_cast<uint8_t>(patchCount);
		header.flags = 0;
		header.mappingEpoch = spec.mappingEpoch;
		header.featherPx = 0;
		for (int eye = 0; eye < 2; ++eye) {
			gxh1::EyeMapping& mapping = header.eyes[eye];
			mapping.sourceW = static_cast<uint16_t>(spec.canvasW + 32);
			mapping.sourceH = static_cast<uint16_t>(spec.canvasH + 32);
			mapping.originX = 16;
			mapping.originY = 16;
			mapping.tanLeft = -1.0f;
			mapping.tanRight = 1.0f;
			mapping.tanTop = -1.0f;
			mapping.tanBottom = 1.0f;
			mapping.gazeX = mapping.sourceW * 0.5f;
			mapping.gazeY = mapping.sourceH * 0.5f;
		}
		std::memcpy(unit.data(), &header, sizeof(header));
	}

	std::vector<uint8_t> out(gxh1::kHeaderBytes + spec.base.size() + unit.size(), 0);
	if (!spec.base.empty()) std::memcpy(out.data() + gxh1::kHeaderBytes, spec.base.data(), spec.base.size());
	if (!unit.empty()) {
		std::memcpy(out.data() + gxh1::kHeaderBytes + spec.base.size(), unit.data(), unit.size());
	}

	EnvelopeHeader header{};
	header.magic = gxh1::kMagic;
	header.versionMajor = gxh1::kVersionMajor;
	header.versionMinor = gxh1::kVersionMinor;
	header.headerBytes = gxh1::kHeaderBytes;
	header.flags = spec.flags;
	header.sessionEpoch = spec.sessionEpoch;
	header.sourceEpoch = spec.sourceEpoch;
	header.mappingEpoch = spec.mappingEpoch;
	header.frameId = spec.frameId;
	header.ptsNs = 0;
	header.baseBytes = static_cast<uint32_t>(spec.base.size());
	header.foveaBytes = static_cast<uint32_t>(unit.size());
	header.baseOffset = gxh1::kHeaderBytes;
	header.foveaOffset = unit.empty() ? 0 : gxh1::kHeaderBytes + static_cast<uint32_t>(spec.base.size());
	header.baseCodec = gxh1::kBaseCodecHevc;
	header.baseFormat = gxh1::kBaseFormatNv12;
	header.foveaCodec = fovea ? spec.foveaCodec : gxh1::kFoveaCodecNone;
	header.precision = spec.precision;
	header.canvasW = spec.canvasW;
	header.canvasH = spec.canvasH;
	header.patchCount = static_cast<uint16_t>(patchCount);
	header.featherPx = 0;
	header.crc32 = 0;

	uint32_t crc = gxh1::Crc32(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
	crc = gxh1::Crc32(out.data() + gxh1::kHeaderBytes, spec.base.size(), crc);
	if (!unit.empty()) {
		crc = gxh1::Crc32(out.data() + gxh1::kHeaderBytes + spec.base.size(), unit.size(), crc);
	}
	header.crc32 = crc;
	std::memcpy(out.data(), &header, sizeof(header));
	return out;
}

// Recomputes the CRC after a test mutated a header field.
void FixCrc(std::vector<uint8_t>& bytes) {
	EnvelopeHeader header{};
	std::memcpy(&header, bytes.data(), sizeof(header));
	header.crc32 = 0;
	uint32_t crc = gxh1::Crc32(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
	crc = gxh1::Crc32(bytes.data() + header.baseOffset, header.baseBytes, crc);
	if (header.foveaBytes && header.foveaOffset) {
		crc = gxh1::Crc32(bytes.data() + header.foveaOffset, header.foveaBytes, crc);
	}
	header.crc32 = crc;
	std::memcpy(bytes.data(), &header, sizeof(header));
}

std::vector<uint8_t> SolidRgb(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
	std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3u);
	for (size_t i = 0; i < rgb.size(); i += 3) {
		rgb[i + 0] = r;
		rgb[i + 1] = g;
		rgb[i + 2] = b;
	}
	return rgb;
}

std::vector<uint8_t> AstcPayload(const std::vector<uint8_t>& rgb, int width, int height) {
	std::vector<uint8_t> blocks;
	if (!gxh1::AstcEncodeRgb(rgb.data(), static_cast<size_t>(width) * 3u, width, height, 4, 4, blocks)) {
		return {};
	}
	return blocks;
}

std::vector<uint8_t> PyroWavePayload(const std::vector<uint8_t>& rgb, int width, int height) {
	gxh1::PwEncodeOptions options;
	std::vector<uint8_t> out;
	gxh1::PwStats stats;
	if (!gxh1::PwEncodeRgb(rgb.data(), static_cast<size_t>(width) * 3u, width, height, options, out, &stats)) {
		return {};
	}
	return out;
}

// One 1x1 patch per eye, both encoded with the copied encoder.
EnvelopeSpec SimpleSpec(uint8_t foveaCodec) {
	const uint8_t r = 200, g = 30, b = 60;
	const std::vector<uint8_t> rgb = SolidRgb(1, 1, r, g, b);
	EnvelopeSpec spec;
	spec.canvasW = 1;
	spec.canvasH = 1;
	spec.foveaCodec = foveaCodec;
	PatchSpec eye0;
	eye0.eye = 0;
	eye0.payload = foveaCodec == gxh1::kFoveaCodecAstc ? AstcPayload(rgb, 1, 1) : PyroWavePayload(rgb, 1, 1);
	PatchSpec eye1 = eye0;
	eye1.eye = 1;
	spec.patches.push_back(eye0);
	spec.patches.push_back(eye1);
	return spec;
}

std::string AdmitReason(gxh1::HybridSession& session, const std::vector<uint8_t>& bytes) {
	std::string reason;
	gxh1::ParseLimits limits;
	session.Admit(bytes.data(), bytes.size(), limits, &reason);
	return reason;
}

void CheckProbe() {
	// Warm stdio so the counter below only sees probe work.
	std::printf("");
	const long before = g_allocations.load();
	const int first = gxr_hybrid_probe();
	const int second = gxr_hybrid_probe();
	const long after = g_allocations.load();
	CHECK(first == 1, "probe reports the helper usable");
	CHECK(second == first, "probe is idempotent");
	CHECK(after == before, "probe does not allocate");
	const char* status = gxr_hybrid_status();
	CHECK(status != nullptr && status[0] != '\0', "status is a live C string");
}

void CheckAstcBlockDecode() {
	const std::vector<uint8_t> rgb = SolidRgb(4, 4, 200, 30, 60);
	const std::vector<uint8_t> blocks = AstcPayload(rgb, 4, 4);
	CHECK(blocks.size() == gxh1::kAstcBlockBytes, "astc: one 16-byte block for a 4x4 patch");
	gxh1::AstcBlockInfo info;
	CHECK(gxh1::AstcInspectBlock(blocks.data(), 4, 4, info), "astc: inspect parses the block header");
	CHECK(info.supported, "astc: block is inside the supported subset");
	CHECK(info.partitionCount == 1 && !info.dualPlane, "astc: single partition, single plane");

	std::vector<uint8_t> decoded;
	CHECK(gxh1::AstcDecodeRgb(blocks.data(), blocks.size(), 4, 4, 4, 4, decoded), "astc: decode succeeds");
	CHECK(decoded.size() == 4u * 4u * 3u, "astc: decode size");
	bool exact = decoded.size() == rgb.size();
	for (size_t i = 0; exact && i < rgb.size(); ++i) exact = decoded[i] == rgb[i];
	CHECK(exact, "astc: constant block round trips exactly");

	std::vector<uint8_t> shortBlocks(blocks.begin(), blocks.end() - 1);
	CHECK(!gxh1::AstcDecodeRgb(shortBlocks.data(), shortBlocks.size(), 4, 4, 4, 4, decoded),
	      "astc: short payload is rejected");
}

void CheckPyroWaveDecode() {
	const int w = 16;
	const int h = 16;
	const std::vector<uint8_t> rgb = SolidRgb(w, h, 200, 30, 60);
	const std::vector<uint8_t> encoded = PyroWavePayload(rgb, w, h);
	CHECK(encoded.size() > 8, "pyrowave: encoder emits a start-of-frame header plus packets");

	std::vector<uint8_t> decoded;
	int outW = 0;
	int outH = 0;
	CHECK(gxh1::PwDecodeRgb(encoded.data(), encoded.size(), decoded, outW, outH), "pyrowave: decode succeeds");
	CHECK(outW == w && outH == h, "pyrowave: decoded dimensions match the frame");
	bool close = decoded.size() == rgb.size();
	int worst = 0;
	for (size_t i = 0; close && i < rgb.size(); ++i) {
		const int diff = std::abs(static_cast<int>(decoded[i]) - static_cast<int>(rgb[i]));
		if (diff > worst) worst = diff;
	}
	close = close && worst <= 8;
	CHECK(close, "pyrowave: solid frame decodes within 8 levels");

	std::vector<uint8_t> truncated(encoded.begin(), encoded.begin() + 6);
	CHECK(!gxh1::PwDecodeRgb(truncated.data(), truncated.size(), decoded, outW, outH),
	      "pyrowave: truncated header is rejected");
}

void CheckGoldenRoundTrip(uint8_t foveaCodec, const char* label) {
	const EnvelopeSpec spec = SimpleSpec(foveaCodec);
	const std::vector<uint8_t> bytes = BuildEnvelope(spec);
	CHECK(!bytes.empty(), label);

	gxh1::ParseLimits limits;
	gxh1::ParsedFrame parsed;
	CHECK(gxh1::ParseFrame(bytes.data(), bytes.size(), limits, parsed) == gxh1::ParseError::None, label);
	CHECK(parsed.foveaPresent && parsed.coverageComplete, label);
	CHECK(parsed.patches.size() == 2, label);

	gxh1::HybridSession session;
	const uint8_t* data = bytes.data();
	size_t size = bytes.size();
	const int result = session.OnAccessUnit(&data, &size);
	CHECK(result == 1, "envelope consumed");
	CHECK(data == bytes.data() + gxh1::kHeaderBytes, "base AU pointer advanced past the header");
	CHECK(size == spec.base.size(), "stripped size equals baseBytes");
	CHECK(std::memcmp(data, spec.base.data(), size) == 0, "base AU bytes are untouched");

	uint64_t frameId = 0;
	CHECK(session.TakeLatest(frameId), "a complete frame is stored");
	CHECK(frameId == spec.frameId, "frame id is the envelope frame id");

	uint8_t rgba[4] = {0, 0, 0, 0};
	uint64_t layerId = 0;
	CHECK(session.CopyLayer(0, rgba, 1, 1, 4, &layerId), "eye 0 layer is available");
	CHECK(layerId == spec.frameId, "layer carries the frame id");
	CHECK(rgba[3] == 255, "layer alpha is opaque");
	CHECK(std::abs(static_cast<int>(rgba[0]) - 200) <= 8 && std::abs(static_cast<int>(rgba[1]) - 30) <= 8 &&
	          std::abs(static_cast<int>(rgba[2]) - 60) <= 8,
	      "eye 0 layer colour");

	uint8_t rgbaEye1[4] = {0, 0, 0, 0};
	CHECK(session.CopyLayer(1, rgbaEye1, 1, 1, 4, nullptr), "eye 1 layer is available");
	CHECK(rgbaEye1[3] == 255, "eye 1 alpha is opaque");

	CHECK(!session.CopyLayer(0, rgba, 2, 2, 8, nullptr), "layer size mismatch is rejected");
	CHECK(!session.CopyLayer(2, rgba, 1, 1, 4, nullptr), "invalid eye index is rejected");
	CHECK(std::strcmp(session.Status(), "decoded_inline") == 0 || std::strcmp(session.Status(), "queued_worker") == 0,
	      "status names the decode path");
}

void CheckMalformed() {
	using gxh1::ParseError;
	const EnvelopeSpec spec = SimpleSpec(gxh1::kFoveaCodecAstc);
	const std::vector<uint8_t> good = BuildEnvelope(spec);

	{
		gxh1::HybridSession session;
		std::vector<uint8_t> bad = good;
		bad[0] = static_cast<uint8_t>(bad[0] ^ 0xFF);
		const uint8_t* data = bad.data();
		size_t size = bad.size();
		CHECK(session.OnAccessUnit(&data, &size) == 0, "bad magic is passed through, not claimed");
		CHECK(data == bad.data() && size == bad.size(), "pass-through leaves the pointers alone");
		gxh1::HybridSession other;
		CHECK(AdmitReason(other, bad) == "bad_magic", "admit names bad_magic");
	}
	// Both a future major and the unplaceable v1 layout are refused before the base span is
	// trusted.
	for (const uint16_t major : {static_cast<uint16_t>(gxh1::kVersionMajor + 1), static_cast<uint16_t>(1)}) {
		gxh1::HybridSession session;
		std::vector<uint8_t> bad = good;
		EnvelopeHeader header{};
		std::memcpy(&header, bad.data(), sizeof(header));
		header.versionMajor = major;
		std::memcpy(bad.data(), &header, sizeof(header));
		FixCrc(bad);
		const uint8_t* data = bad.data();
		size_t size = bad.size();
		CHECK(session.OnAccessUnit(&data, &size) == -1, "bad version is rejected");
		CHECK(data == bad.data() && size == bad.size(), "rejection leaves the pointers alone");
		gxh1::HybridSession other;
		CHECK(AdmitReason(other, bad) == "bad_version", "admit names bad_version");
	}
	{
		gxh1::HybridSession session;
		std::vector<uint8_t> bad = good;
		EnvelopeHeader header{};
		std::memcpy(&header, bad.data(), sizeof(header));
		header.baseBytes = gxh1::kMaxBaseBytes + 1u;
		std::memcpy(bad.data(), &header, sizeof(header));
		// No CRC fix-up here: the length bound is checked before the CRC, and a CRC over
		// the declared (oversized) range would read past the buffer.
		CHECK(AdmitReason(session, bad) == "length_overflow", "oversized base length is rejected");
	}
	{
		gxh1::HybridSession session;
		std::vector<uint8_t> bad = good;
		EnvelopeHeader header{};
		std::memcpy(&header, bad.data(), sizeof(header));
		header.crc32 ^= 0x1u;
		std::memcpy(bad.data(), &header, sizeof(header));
		CHECK(AdmitReason(session, bad) == "bad_crc", "corrupt payload is rejected by CRC");
	}
	{
		gxh1::HybridSession session;
		const std::vector<uint8_t> shortBytes(good.begin(), good.begin() + 40);
		std::vector<uint8_t> partial = shortBytes;
		gxh1::HybridSession other;
		CHECK(AdmitReason(other, partial) == "too_small", "truncated envelope is rejected");
	}
	{
		// A complete 32x32 tiling is accepted; removing one eye-0 tile is a coverage gap
		// and duplicating one tile (an overlap) is rejected by the same tiling check
		// (ParseFrame folds overlap into coverage: it never returns PatchOverlap).
		const std::vector<uint8_t> tile = AstcPayload(SolidRgb(16, 16, 200, 30, 60), 16, 16);
		EnvelopeSpec tiled = spec;
		tiled.canvasW = 32;
		tiled.canvasH = 32;
		tiled.patches.clear();
		for (uint8_t eye = 0; eye < 2; ++eye) {
			for (int row = 0; row < 2; ++row) {
				for (int col = 0; col < 2; ++col) {
					PatchSpec p;
					p.eye = eye;
					p.x = static_cast<uint16_t>(col * 16);
					p.y = static_cast<uint16_t>(row * 16);
					p.width = 16;
					p.height = 16;
					p.payload = tile;
					tiled.patches.push_back(p);
				}
			}
		}
		{
			gxh1::HybridSession session;
			CHECK(AdmitReason(session, BuildEnvelope(tiled)) == "ok", "complete 32x32 tiling is accepted");
		}
		{
			EnvelopeSpec gap = tiled;
			gap.patches.erase(gap.patches.begin());  // eye 0 loses its top-left tile
			gxh1::HybridSession session;
			CHECK(AdmitReason(session, BuildEnvelope(gap)) == "coverage_gap", "coverage gap is rejected");
		}
		{
			EnvelopeSpec overlap = tiled;
			overlap.patches.push_back(tiled.patches.front());  // eye 0, top-left again
			gxh1::HybridSession session;
			CHECK(AdmitReason(session, BuildEnvelope(overlap)) == "coverage_gap",
			      "overlapping patches are rejected by the tiling check");
		}
	}
	{
		// A payload range that runs past the unit must fail before any allocation.
		EnvelopeSpec broken = spec;
		std::vector<uint8_t> bytes = BuildEnvelope(broken);
		EnvelopeHeader header{};
		std::memcpy(&header, bytes.data(), sizeof(header));
		gxh1::PatchEntry entry{};
		std::memcpy(&entry, bytes.data() + header.foveaOffset + gxh1::kFoveaUnitHeaderBytes, sizeof(entry));
		entry.payloadOffset = header.foveaBytes + 64;
		std::memcpy(bytes.data() + header.foveaOffset + gxh1::kFoveaUnitHeaderBytes, &entry, sizeof(entry));
		FixCrc(bytes);
		gxh1::HybridSession session;
		CHECK(AdmitReason(session, bytes) == "bad_patch_table", "out-of-range payload is rejected");
	}
	{
		// Fovea flag cleared without a degraded marker: the parser must not accept it.
		EnvelopeSpec inconsistent = spec;
		inconsistent.flags = 0;
		const std::vector<uint8_t> bytes = BuildEnvelope(inconsistent);
		gxh1::HybridSession session;
		CHECK(AdmitReason(session, bytes) == "bad_flags", "fovea bytes without the present flag are rejected");
	}
}

void CheckEpochs() {
	const EnvelopeSpec spec = SimpleSpec(gxh1::kFoveaCodecAstc);

	gxh1::HybridSession session;
	std::vector<uint8_t> bytes = BuildEnvelope(spec);
	const uint8_t* data = bytes.data();
	size_t size = bytes.size();
	CHECK(session.OnAccessUnit(&data, &size) == 1, "first envelope admits its epoch");

	EnvelopeSpec other = spec;
	other.sessionEpoch = spec.sessionEpoch + 1;
	other.frameId = spec.frameId + 1;
	std::vector<uint8_t> newer = BuildEnvelope(other);
	const uint8_t* data2 = newer.data();
	size_t size2 = newer.size();
	// The fixed header is valid, so the base access unit still reaches the decoder; only the
	// fovea of a foreign epoch is refused.
	CHECK(session.OnAccessUnit(&data2, &size2) == 1, "epoch change without kFlagReset keeps the base");
	CHECK(data2 == newer.data() + gxh1::kHeaderBytes && size2 == spec.base.size(),
	      "epoch rejection hands back the untouched base access unit");
	CHECK(std::strcmp(session.Status(), "epoch_mismatch") == 0, "status names the epoch mismatch");
	uint64_t frameId = 0;
	CHECK(session.TakeLatest(frameId) && frameId == spec.frameId, "old frame is still the newest");

	other.flags = static_cast<uint16_t>(gxh1::kFlagFoveaPresent | gxh1::kFlagReset);
	std::vector<uint8_t> reset = BuildEnvelope(other);
	const uint8_t* data3 = reset.data();
	size_t size3 = reset.size();
	CHECK(session.OnAccessUnit(&data3, &size3) == 1, "kFlagReset lets a new session take over");
	CHECK(session.TakeLatest(frameId) && frameId == other.frameId, "new session frame replaces the old one");

	session.Reset(42);
	CHECK(!session.TakeLatest(frameId), "reset drops the cached frames");
	other.sessionEpoch = 7;
	other.flags = gxh1::kFlagFoveaPresent;
	std::vector<uint8_t> pinned = BuildEnvelope(other);
	const uint8_t* data4 = pinned.data();
	size_t size4 = pinned.size();
	CHECK(session.OnAccessUnit(&data4, &size4) == 1 && size4 == spec.base.size(),
	      "a pinned epoch refuses a different session's fovea but keeps its base");
	CHECK(!session.TakeLatest(frameId), "a refused fovea is never stored");

	session.Reset(0);
	std::vector<uint8_t> adopt = BuildEnvelope(other);
	const uint8_t* data5 = adopt.data();
	size_t size5 = adopt.size();
	CHECK(session.OnAccessUnit(&data5, &size5) == 1, "reset(0) adopts the next envelope epoch");
	CHECK(std::strcmp(gxr_hybrid_status(), "") != 0, "status readable after reset");
}

// A structurally valid envelope whose patches cannot be decoded must still be consumed:
// the base access unit stays decodable and the broken layer is simply not stored.
void CheckDecodeFailureKeepsBase() {
	EnvelopeSpec spec = SimpleSpec(gxh1::kFoveaCodecAstc);
	spec.patches[0].payload.resize(8);  // shorter than one ASTC block
	std::vector<uint8_t> bytes = BuildEnvelope(spec);
	gxh1::HybridSession session;
	const uint8_t* data = bytes.data();
	size_t size = bytes.size();
	CHECK(session.OnAccessUnit(&data, &size) == 1, "undecodable payload still consumes the envelope");
	CHECK(size == spec.base.size(), "base AU survives a failed enhancement decode");
	CHECK(data == bytes.data() + gxh1::kHeaderBytes, "pointers advance on decode failure");
	uint64_t frameId = 0;
	CHECK(!session.TakeLatest(frameId), "a failed decode stores no frame");
	CHECK(std::strcmp(session.Status(), "decode_failed") == 0, "status names decode_failed");
}

void CheckDegradedAndOrdering() {
	EnvelopeSpec degraded;
	degraded.flags = static_cast<uint16_t>(gxh1::kFlagDegraded);
	degraded.foveaCodec = gxh1::kFoveaCodecNone;
	degraded.patches.clear();
	std::vector<uint8_t> bytes = BuildEnvelope(degraded);
	gxh1::HybridSession session;
	const uint8_t* data = bytes.data();
	size_t size = bytes.size();
	CHECK(session.OnAccessUnit(&data, &size) == 1, "degraded frame is stripped, not rejected");
	CHECK(size == degraded.base.size(), "degraded frame keeps the base AU");
	uint64_t frameId = 0;
	CHECK(!session.TakeLatest(frameId), "degraded frame stores no layer");
	CHECK(std::strcmp(session.Status(), "strip_only") == 0, "status names strip_only");

	// Newest frame wins; an older frameId arriving late is dropped.
	EnvelopeSpec first = SimpleSpec(gxh1::kFoveaCodecAstc);
	first.frameId = 5;
	std::vector<uint8_t> firstBytes = BuildEnvelope(first);
	const uint8_t* d1 = firstBytes.data();
	size_t s1 = firstBytes.size();
	CHECK(session.OnAccessUnit(&d1, &s1) == 1, "first frame accepted");

	EnvelopeSpec older = SimpleSpec(gxh1::kFoveaCodecAstc);
	older.frameId = 3;
	older.sessionEpoch = first.sessionEpoch;  // same admitted epoch
	std::vector<uint8_t> olderBytes = BuildEnvelope(older);
	const uint8_t* d2 = olderBytes.data();
	size_t s2 = olderBytes.size();
	CHECK(session.OnAccessUnit(&d2, &s2) == 1, "older frame is structurally accepted");
	CHECK(session.TakeLatest(frameId) && frameId == 5, "older frame never replaces the newer one");
}

void CheckAbi() {
	CHECK(gxr_hybrid_probe() == 1, "probe from the C ABI");

	gxr_hybrid_reset(0);
	const EnvelopeSpec spec = SimpleSpec(gxh1::kFoveaCodecAstc);
	std::vector<uint8_t> bytes = BuildEnvelope(spec);
	const uint8_t* data = bytes.data();
	size_t size = bytes.size();
	CHECK(gxr_hybrid_on_access_unit(&data, &size) == 1, "C ABI consumes the envelope");
	CHECK(size == spec.base.size(), "C ABI strips to the base AU");

	uint8_t rgba[4] = {0, 0, 0, 0};
	uint64_t frameId = 0;
	CHECK(gxr_hybrid_take_layer(0, rgba, 1, 1, 4, &frameId) == 0, "C ABI take_layer succeeds");
	CHECK(frameId == spec.frameId, "C ABI reports the frame id");
	CHECK(gxr_hybrid_take_layer(0, rgba, 1, 1, 2, &frameId) == -1, "C ABI rejects a short stride");
	CHECK(gxr_hybrid_take_layer(0, nullptr, 1, 1, 4, &frameId) == -1, "C ABI rejects a null buffer");
	CHECK(gxr_hybrid_on_access_unit(nullptr, &size) == 0, "C ABI treats null pointers as pass-through");
	CHECK(gxr_hybrid_status() != nullptr, "C ABI status is never null");

	const uint8_t notHybrid[8] = {0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x00};
	const uint8_t* raw = notHybrid;
	size_t rawSize = sizeof(notHybrid);
	CHECK(gxr_hybrid_on_access_unit(&raw, &rawSize) == 0, "non-GXH1 access unit passes through");
	CHECK(raw == notHybrid && rawSize == sizeof(notHybrid), "pass-through keeps the pointers");
}

void CheckSchemaStaticAsserts() {
	static_assert(sizeof(EnvelopeHeader) == 80, "GXH1 header layout drifted");
	static_assert(sizeof(gxh1::PatchEntry) == 48, "GXH1 patch entry layout drifted");
	static_assert(sizeof(gxh1::FoveaUnitHeader) == 96, "GXH1 fovea header layout drifted");
	static_assert(sizeof(gxh1::EyeMapping) == 32, "GXH1 eye mapping layout drifted");
	static_assert(offsetof(gxh1::FoveaUnitHeader, eyes) == 32, "GXH1 eye mapping offset drifted");
	static_assert(gxh1::kVersionMajor == 2, "GXH1 schema version drifted");
	static_assert(alignof(EnvelopeHeader) == 1, "GXH1 header must stay packed");
	static_assert(gxh1::kMagic == 0x31485847u, "GXH1 magic drifted");
	static_assert(gxh1::kMaxPatches == 64, "GXH1 patch ceiling drifted");
	static_assert(offsetof(EnvelopeHeader, crc32) == 68, "GXH1 crc offset drifted");
	static_assert(offsetof(gxh1::PatchEntry, payloadOffset) == 4, "GXH1 patch payload offset drifted");
}

}  // namespace

// A ring below native density arrives as a smaller texel grid; the bench decode stretches it
// back over the rect (the GPU compositor does the same in its sampler).
void CheckResampledRing() {
	EnvelopeSpec spec;
	spec.canvasW = 16;
	spec.canvasH = 16;
	spec.frameId = 77;
	std::vector<uint8_t> texels(8 * 8 * 3);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x) {
			uint8_t* p = texels.data() + (static_cast<size_t>(y) * 8 + x) * 3;
			p[0] = static_cast<uint8_t>(x * 32);
			p[1] = 100;
			p[2] = 50;
		}
	std::vector<uint8_t> blocks;
	CHECK(gxh1::AstcEncodeRgb(texels.data(), 8 * 3, 8, 8, 4, 4, blocks), "resampled ring payload encodes");
	for (uint8_t eye = 0; eye < 2; ++eye) {
		PatchSpec patch;
		patch.width = 16;
		patch.height = 16;
		patch.eye = eye;
		patch.texelW = 8;
		patch.texelH = 8;
		patch.payload = blocks;
		spec.patches.push_back(patch);
	}
	const std::vector<uint8_t> bytes = BuildEnvelope(spec);
	gxh1::HybridSession session;
	const uint8_t* data = bytes.data();
	size_t size = bytes.size();
	CHECK(session.OnAccessUnit(&data, &size) == 1, "resampled envelope consumed");
	std::vector<uint8_t> rgba(16 * 16 * 4, 0);
	CHECK(session.CopyLayer(0, rgba.data(), 16, 16, 16 * 4, nullptr), "resampled layer covers the full rect");
	bool opaque = true, monotonic = true;
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			const uint8_t* p = rgba.data() + (static_cast<size_t>(y) * 16 + x) * 4;
			opaque = opaque && p[3] == 255;
			if (x > 0) monotonic = monotonic && p[0] + 2 >= p[-4];
		}
	CHECK(opaque, "every rect pixel of a resampled ring is covered");
	CHECK(monotonic && rgba[0] <= 12 && rgba[15 * 4] >= 212, "resampled ring is stretched smoothly over its rect");
}

int main() {
	// The C ABI tests exercise the decode path explicitly; production defaults to demux-only.
	_putenv_s("GXR_HYBRID_CPU_DECODE", "1");
	CheckProbe();
	CheckSchemaStaticAsserts();
	CheckAstcBlockDecode();
	CheckPyroWaveDecode();
	CheckGoldenRoundTrip(gxh1::kFoveaCodecAstc, "golden ASTC envelope");
	CheckGoldenRoundTrip(gxh1::kFoveaCodecPyroWave, "golden PyroWave envelope");
	CheckMalformed();
	CheckEpochs();
	CheckResampledRing();
	CheckDecodeFailureKeepsBase();
	CheckDegradedAndOrdering();
	CheckAbi();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
