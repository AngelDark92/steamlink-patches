#include "hybrid_session.h"

#include "astc_decode.h"
#include "pyrowave_decode.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace gxh1 {
namespace {

// Fixed status literals: the published pointer always refers to static storage, so
// gxr_hybrid_status() can hand it out without a copy or a lock.
const char* MalformedStatus(ParseError error) {
	switch (error) {
	case ParseError::None: return "ok";
	case ParseError::TooSmall: return "malformed:too_small";
	case ParseError::BadMagic: return "malformed:bad_magic";
	case ParseError::BadVersion: return "malformed:bad_version";
	case ParseError::BadHeaderBytes: return "malformed:bad_header_bytes";
	case ParseError::BadFlags: return "malformed:bad_flags";
	case ParseError::LengthOverflow: return "malformed:length_overflow";
	case ParseError::BadOffsets: return "malformed:bad_offsets";
	case ParseError::BadCodec: return "malformed:bad_codec";
	case ParseError::BadPrecision: return "malformed:bad_precision";
	case ParseError::BadCanvas: return "malformed:bad_canvas";
	case ParseError::TooManyPatches: return "malformed:too_many_patches";
	case ParseError::BadPatchTable: return "malformed:bad_patch_table";
	case ParseError::BadPatchGeometry: return "malformed:bad_patch_geometry";
	case ParseError::PatchOutOfCanvas: return "malformed:patch_out_of_canvas";
	case ParseError::PatchOverlap: return "malformed:patch_overlap";
	case ParseError::CoverageGap: return "malformed:coverage_gap";
	case ParseError::BadEpochs: return "malformed:bad_epochs";
	case ParseError::BadCrc: return "malformed:bad_crc";
	case ParseError::UnsupportedColorLevel: return "malformed:unsupported_color_level";
	case ParseError::BadMapping: return "malformed:bad_mapping";
	}
	return "malformed:unknown";
}

size_t AstcBlocksFor(int width, int height, uint8_t blockW, uint8_t blockH) {
	const size_t bx = (static_cast<size_t>(width) + blockW - 1) / blockW;
	const size_t by = (static_cast<size_t>(height) + blockH - 1) / blockH;
	return bx * by * kAstcBlockBytes;
}

// Hard ceiling for one decoded patch: the producer refuses plans above this, and the
// compressed payload cannot legitimately describe a larger image.
constexpr uint32_t kMaxDecodedPixels = kMaxPatchPixels;  // 2 Mi px

}  // namespace

HybridSession::HybridSession() = default;

HybridSession::~HybridSession() {
	{
		std::lock_guard<std::mutex> lock(workerMutex_);
		workerStop_ = true;
		job_.pending = false;
	}
	workerCv_.notify_one();
	if (worker_.joinable()) worker_.join();
}

void HybridSession::SetStatus(const char* text) {
	status_.store(text ? text : "unknown", std::memory_order_relaxed);
}

void HybridSession::Reset(uint64_t sessionEpoch) {
	std::lock_guard<std::mutex> lock(mutex_);
	++generation_;
	DropFramesLocked();
	hasFovea_ = false;
	sessionEpoch_ = static_cast<uint32_t>(sessionEpoch);
	epochPinned_ = sessionEpoch != 0;
}

void HybridSession::DropFramesLocked() {
	for (size_t i = 0; i < kFrameSlots; ++i) {
		frames_[i] = StoredFrame{};
	}
	newestSlot_ = kFrameSlots - 1;
	frameCount_ = 0;
	totalStoredBytes_ = 0;
	SetStatus("reset");
}

ParseError HybridSession::AdmitLocked(const uint8_t* data, size_t bytes, const ParseLimits& limits,
                                      ParsedFrame& parsed, const char** reasonName) {
	ParseLimits bounded = limits;
	// The session stores a fixed patch table and two canvas slots; the caller may lower
	// these limits but never raise them past what this implementation can hold.
	bounded.maxPatches = std::min<uint16_t>(bounded.maxPatches, kMaxPatches);
	if (bounded.maxPatches == 0) bounded.maxPatches = kMaxPatches;

	const ParseError error = ParseFrame(data, bytes, bounded, parsed);
	if (error != ParseError::None) {
		SetStatus(MalformedStatus(error));
		if (reasonName) *reasonName = ParseErrorName(error);
		return error;
	}

	if (!epochPinned_) {
		sessionEpoch_ = parsed.header.sessionEpoch;
		epochPinned_ = true;
	} else if (parsed.header.sessionEpoch != sessionEpoch_) {
		if ((parsed.header.flags & kFlagReset) == 0) {
			SetStatus("epoch_mismatch");
			if (reasonName) *reasonName = "epoch_mismatch";
			return ParseError::BadEpochs;
		}
		// The driver declares a new session: drop everything cached for the old one and
		// retire in-flight work, exactly like an explicit Reset.
		sessionEpoch_ = parsed.header.sessionEpoch;
		DropFramesLocked();
		++generation_;
	}
	limits_ = bounded;
	return ParseError::None;
}

int HybridSession::Admit(const uint8_t* data, size_t bytes, const ParseLimits& limits, std::string* reason) {
	ParsedFrame parsed;
	const char* name = "ok";
	std::lock_guard<std::mutex> lock(mutex_);
	const ParseError error = AdmitLocked(data, bytes, limits, parsed, &name);
	if (error != ParseError::None) {
		if (reason) *reason = name;
		return -1;
	}
	hasFovea_ = parsed.foveaPresent;
	SetStatus(parsed.foveaPresent ? "admitted" : "admitted_degraded");
	if (reason) *reason = "ok";
	return 0;
}

bool HybridSession::TakeLatest(uint64_t& frameId) const {
	std::lock_guard<std::mutex> lock(mutex_);
	if (frameCount_ == 0) return false;
	frameId = frames_[newestSlot_].frameId;
	return true;
}

bool HybridSession::CopyLayer(int eye, uint8_t* rgba, int width, int height, int stride, uint64_t* frameId) const {
	if (eye < 0 || eye > 1 || !rgba || width <= 0 || height <= 0) return false;
	if (width > 16384 || height > 16384) return false;
	if (stride < width * 4) return false;
	std::lock_guard<std::mutex> lock(mutex_);
	if (frameCount_ == 0) return false;
	const StoredFrame& frame = frames_[newestSlot_];
	if (!frame.valid || frame.width != width || frame.height != height) return false;
	const size_t rowBytes = static_cast<size_t>(width) * 4u;
	const std::vector<uint8_t>& src = frame.eye[eye];
	if (src.size() < rowBytes * static_cast<size_t>(height)) return false;
	for (int y = 0; y < height; ++y) {
		std::memcpy(rgba + static_cast<size_t>(y) * stride, src.data() + static_cast<size_t>(y) * rowBytes,
		            rowBytes);
	}
	if (frameId) *frameId = frame.frameId;
	return true;
}

const char* HybridSession::Status() const {
	return status_.load(std::memory_order_relaxed);
}

void HybridSession::SetDecodeEnabled(bool enabled) {
	decodeEnabled_.store(enabled, std::memory_order_relaxed);
}

bool HybridSession::InlineBudgetExceeded() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return inlineDisabled_;
}

bool HybridSession::HasFovea() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return hasFovea_;
}

bool HybridSession::DecodeFrame(const uint8_t* unit, const PendingPatch* patches, size_t patchCount,
                                uint16_t canvasW, uint16_t canvasH, uint8_t foveaCodec,
                                StoredFrame& out) const {
	if (!unit || !patches || canvasW == 0 || canvasH == 0) return false;
	const uint64_t pixels = static_cast<uint64_t>(canvasW) * canvasH;
	const uint64_t bytesPerEye = pixels * 4u;
	if (bytesPerEye * 2u > kMaxFrameDecodedBytes) return false;

	out.width = canvasW;
	out.height = canvasH;
	out.eye[0].assign(static_cast<size_t>(bytesPerEye), 0);
	out.eye[1].assign(static_cast<size_t>(bytesPerEye), 0);

	std::vector<uint8_t> rgb;
	for (size_t i = 0; i < patchCount; ++i) {
		const PatchEntry& patch = patches[i].entry;
		if (patch.eye > 1) return false;
		if (patch.codec != foveaCodec) return false;
		if (patch.width == 0 || patch.height == 0) return false;
		if (static_cast<uint32_t>(patch.x) + patch.width > canvasW) return false;
		if (static_cast<uint32_t>(patch.y) + patch.height > canvasH) return false;

		const uint8_t* payload = unit + patches[i].offset;
		const int pw = static_cast<int>(patch.width);
		const int ph = static_cast<int>(patch.height);
		// A resampled ring carries fewer texels than its rect; it is decoded at its texel
		// grid and stretched over the rect (the GPU compositor does this in its sampler).
		const int tw = static_cast<int>(PatchTexelW(patch));
		const int th = static_cast<int>(PatchTexelH(patch));

		if (foveaCodec == kFoveaCodecAstc) {
			const size_t needed = AstcBlocksFor(tw, th, patch.blockW, patch.blockH);
			if (needed == 0 || patch.payloadBytes < needed) return false;
			if (!AstcDecodeRgb(payload, patch.payloadBytes, tw, th, patch.blockW, patch.blockH, rgb)) return false;
		} else if (foveaCodec == kFoveaCodecPyroWave) {
			int decodedW = 0;
			int decodedH = 0;
			// Dimensions and the aligned scratch size are validated against the declared
			// patch and the session ceiling before the decoder allocates anything.
			if (!PwDecodeRgbChecked(payload, patch.payloadBytes, tw, th, kMaxDecodedPixels, rgb, decodedW,
			                        decodedH)) {
				return false;
			}
			if (decodedW != tw || decodedH != th) return false;
		} else {
			return false;
		}
		if (rgb.size() < static_cast<size_t>(tw) * static_cast<size_t>(th) * 3u) return false;

		for (int y = 0; y < ph; ++y) {
			uint8_t* dstRow = out.eye[patch.eye].data() +
			                  (static_cast<size_t>(patch.y + y) * canvasW + patch.x) * 4u;
			if (tw == pw && th == ph) {
				const uint8_t* srcRow = rgb.data() + static_cast<size_t>(y) * static_cast<size_t>(pw) * 3u;
				for (int x = 0; x < pw; ++x) {
					dstRow[x * 4 + 0] = srcRow[x * 3 + 0];
					dstRow[x * 4 + 1] = srcRow[x * 3 + 1];
					dstRow[x * 4 + 2] = srcRow[x * 3 + 2];
					dstRow[x * 4 + 3] = 255;
				}
				continue;
			}
			// Bilinear, texel centres aligned with the encoder's box footprints.
			const float v = std::min(static_cast<float>(th - 1),
			                         std::max(0.0f, (y + 0.5f) * th / ph - 0.5f));
			const int y0 = static_cast<int>(v);
			const int y1 = std::min(y0 + 1, th - 1);
			const float fy = v - y0;
			for (int x = 0; x < pw; ++x) {
				const float u = std::min(static_cast<float>(tw - 1),
				                         std::max(0.0f, (x + 0.5f) * tw / pw - 0.5f));
				const int x0 = static_cast<int>(u);
				const int x1 = std::min(x0 + 1, tw - 1);
				const float fx = u - x0;
				for (int c = 0; c < 3; ++c) {
					auto at = [&](int sx, int sy) {
						return static_cast<float>(rgb[(static_cast<size_t>(sy) * tw + sx) * 3u + c]);
					};
					const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
					const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
					dstRow[x * 4 + c] = static_cast<uint8_t>(top + (bottom - top) * fy + 0.5f);
				}
				dstRow[x * 4 + 3] = 255;
			}
		}
	}
	return true;
}

bool HybridSession::CommitFrame(uint64_t generation, StoredFrame&& frame) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (generation != generation_) return false;
	if (frameCount_ > 0 && frame.frameId <= frames_[newestSlot_].frameId) {
		// Out-of-order or duplicate: the newest complete frame always wins.
		return false;
	}
	const uint64_t bytes = static_cast<uint64_t>(frame.eye[0].size()) + frame.eye[1].size();
	if (bytes > kMaxTotalDecodedBytes) {
		SetStatus("canvas_budget");
		return false;
	}
	const size_t slot = frameCount_ == 0 ? 0 : (newestSlot_ + 1) % kFrameSlots;
	totalStoredBytes_ -= frames_[slot].eye[0].size() + frames_[slot].eye[1].size();
	frames_[slot] = std::move(frame);
	frames_[slot].valid = true;
	totalStoredBytes_ += bytes;
	newestSlot_ = slot;
	if (frameCount_ < kFrameSlots) ++frameCount_;
	if (totalStoredBytes_ > kMaxTotalDecodedBytes) {
		// Two canvases this large cannot both be held; keep the newest only.
		for (size_t i = 0; i < kFrameSlots; ++i) {
			if (i == newestSlot_) continue;
			totalStoredBytes_ -= frames_[i].eye[0].size() + frames_[i].eye[1].size();
			frames_[i] = StoredFrame{};
		}
		frameCount_ = 1;
	}
	return true;
}

void HybridSession::EnsureWorker() {
	std::lock_guard<std::mutex> lock(workerMutex_);
	if (worker_.joinable() || workerStop_) return;
	try {
		worker_ = std::thread(&HybridSession::WorkerMain, this);
	} catch (...) {
		worker_ = std::thread();
	}
}

bool HybridSession::EnqueueJob(DecodeJob&& job) {
	EnsureWorker();
	{
		std::lock_guard<std::mutex> lock(workerMutex_);
		if (!worker_.joinable()) return false;
		job_ = std::move(job);
		job_.pending = true;
	}
	workerCv_.notify_one();
	return true;
}

void HybridSession::WorkerMain() {
	for (;;) {
		DecodeJob job;
		{
			std::unique_lock<std::mutex> lock(workerMutex_);
			workerCv_.wait(lock, [this] { return workerStop_ || job_.pending; });
			if (workerStop_) return;
			job = std::move(job_);
			job_ = DecodeJob{};
		}
		StoredFrame frame;
		bool ok = false;
		bool admitted = true;
		const uint8_t* unit = nullptr;
		try {
			if (job.parseFromEnvelope) {
				ParsedFrame parsed;
				{
					std::lock_guard<std::mutex> lock(mutex_);
					if (AdmitLocked(job.envelope.data(), job.envelope.size(), limits_, parsed, nullptr) !=
					    ParseError::None) {
						admitted = false;
					} else {
						hasFovea_ = parsed.foveaPresent;
						job.frameId = parsed.header.frameId;
						job.canvasW = parsed.header.canvasW;
						job.canvasH = parsed.header.canvasH;
						job.foveaCodec = parsed.header.foveaCodec;
						job.patchCount = parsed.patches.size();
						if (job.patchCount > kMaxPatches) admitted = false;
						for (size_t i = 0; i < job.patchCount; ++i) {
							job.patches[i].entry = parsed.patches[i].entry;
							job.patches[i].offset = parsed.patches[i].entry.payloadOffset;
						}
						unit = job.envelope.data() + parsed.header.foveaOffset;
					}
				}
				if (admitted) {
					// Decode outside the session lock: only the patch table above is shared.
					ok = DecodeFrame(unit, job.patches, job.patchCount, job.canvasW, job.canvasH, job.foveaCodec,
					                 frame);
				}
			} else {
				ok = DecodeFrame(job.payload.data(), job.patches, job.patchCount, job.canvasW, job.canvasH,
				                 job.foveaCodec, frame);
			}
		} catch (...) {
			// A bad allocation in one job must not terminate the process; drop the job and
			// keep the base stream decoding.
			ok = false;
		}
		if (!ok) {
			SetStatus("decode_failed_worker");
			continue;
		}
		frame.frameId = job.frameId;
		if (CommitFrame(job.generation, std::move(frame))) SetStatus("decoded_worker");
	}
}

int HybridSession::OnAccessUnit(const uint8_t** data, size_t* size) {
	try {
		return OnAccessUnitImpl(data, size);
	} catch (...) {
		SetStatus("exception");
		return -1;
	}
}

namespace {

// Cheap structural check used on the MediaCodec input thread. It validates only the fixed
// header and the declared spans; CRC, patch table and coverage validation happen on the
// worker for large frames. Returns false for anything the caller must not queue.
bool QuickHeader(const uint8_t* data, size_t bytes, gxh1::EnvelopeHeader& header) {
	if (!data || bytes < gxh1::kHeaderBytes) return false;
	std::memcpy(&header, data, sizeof(header));
	if (header.magic != gxh1::kMagic) return false;
	if (header.versionMajor != gxh1::kVersionMajor) return false;
	if (header.versionMinor > gxh1::kVersionMinor) return false;
	if (header.headerBytes != gxh1::kHeaderBytes) return false;
	const uint16_t knownFlags = gxh1::kFlagFoveaPresent | gxh1::kFlagReset | gxh1::kFlagDegraded |
	                            gxh1::kFlagExactCore | gxh1::kFlagSnapshot;
	if ((header.flags & ~knownFlags) != 0) return false;
	if (header.baseBytes == 0 || header.baseBytes > gxh1::kMaxBaseBytes) return false;
	if (header.foveaBytes > gxh1::kMaxFoveaBytes) return false;
	if (header.baseOffset < gxh1::kHeaderBytes) return false;

	const uint64_t baseEnd = static_cast<uint64_t>(header.baseOffset) + header.baseBytes;
	if (baseEnd > bytes) return false;
	if (header.foveaBytes == 0) {
		if (header.patchCount != 0 || header.foveaCodec != gxh1::kFoveaCodecNone) return false;
		return header.foveaOffset == 0;
	}
	if (header.foveaCodec != gxh1::kFoveaCodecAstc && header.foveaCodec != gxh1::kFoveaCodecPyroWave) return false;
	if (header.patchCount == 0) return false;
	if (header.foveaOffset != baseEnd) return false;
	if (header.foveaBytes < gxh1::kFoveaUnitHeaderBytes) return false;
	return baseEnd + header.foveaBytes <= bytes;
}

}  // namespace

int HybridSession::OnAccessUnitImpl(const uint8_t** data, size_t* size) {
	if (!data || !size) return -1;
	const uint8_t* d = *data;
	const size_t n = *size;
	if (!d || n < sizeof(uint32_t)) return 0;

	uint32_t magic = 0;
	std::memcpy(&magic, d, 4);
	if (magic != kMagic) return 0;  // not ours: the pass-through path costs one compare

	EnvelopeHeader header{};
	if (!QuickHeader(d, n, header)) {
		SetStatus("malformed:header");
		return -1;
	}

	// From here on the envelope is structurally valid and always consumed: every path
	// returns exactly 1 after advancing the caller's pointers to the untouched base access
	// unit. Payload integrity/coverage errors found later on the worker drop the fovea, not
	// the base.
	auto Strip = [&]() {
		*data = d + header.baseOffset;
		*size = header.baseBytes;
		return 1;
	};

	if ((header.flags & kFlagFoveaPresent) == 0) {
		SetStatus("strip_only");
		return Strip();
	}

	if (!decodeEnabled_.load(std::memory_order_relaxed)) {
		// Production path until GPU sampling/composition exists: never spend CPU on a layer
		// no consumer can display. The base access unit is already stripped above.
		SetStatus("demux_only");
		return Strip();
	}

	uint64_t generation = 0;
	bool inlineDisabled = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		generation = generation_;
		inlineDisabled = inlineDisabled_;
	}
	const uint64_t pixels = static_cast<uint64_t>(header.canvasW) * header.canvasH;
	const bool inlineAllowed = !inlineDisabled && header.foveaBytes <= kInlineMaxFoveaBytes &&
	                           header.patchCount <= kInlineMaxPatches && pixels <= kInlineMaxCanvasPixels;

	if (inlineAllowed) {
		ParsedFrame parsed;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			// The fixed header already validated the base span: an integrity, coverage or
			// epoch failure drops the fovea only, exactly like the worker path below.
			if (AdmitLocked(d, n, limits_, parsed, nullptr) != ParseError::None) return Strip();
			hasFovea_ = true;
			generation = generation_;
		}
		const uint8_t* unit = d + header.foveaOffset;
		const size_t patchCount = parsed.patches.size();
		PendingPatch patches[kMaxPatches];
		for (size_t i = 0; i < patchCount; ++i) {
			patches[i].entry = parsed.patches[i].entry;
			patches[i].offset = parsed.patches[i].entry.payloadOffset;
		}
		StoredFrame frame;
		const auto start = std::chrono::steady_clock::now();
		const bool ok = DecodeFrame(unit, patches, patchCount, header.canvasW, header.canvasH,
		                            header.foveaCodec, frame);
		const uint64_t elapsedUs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
		if (!ok) {
			SetStatus("decode_failed");
			return Strip();
		}
		frame.frameId = header.frameId;
		CommitFrame(generation, std::move(frame));
		if (elapsedUs > kInlineBudgetUs) {
			{
				std::lock_guard<std::mutex> lock(mutex_);
				inlineDisabled_ = true;
			}
			SetStatus("inline_over_budget");
		} else {
			SetStatus("decoded_inline");
		}
		return Strip();
	}

	// Large frame: hand the whole envelope to the worker and return the base immediately.
	// The copy is bounded by the header lengths already checked; MediaCodec may reuse its
	// input buffer as soon as we return, so the worker must own the bytes it validates.
	if (n > static_cast<size_t>(kHeaderBytes) + gxh1::kMaxBaseBytes + gxh1::kMaxFoveaBytes) {
		SetStatus("envelope_budget");
		return Strip();
	}
	DecodeJob job;
	try {
		job.envelope.assign(d, d + n);
	} catch (...) {
		SetStatus("enqueue_alloc_failed");
		return Strip();
	}
	job.parseFromEnvelope = true;
	job.frameId = header.frameId;
	job.generation = generation;
	job.canvasW = header.canvasW;
	job.canvasH = header.canvasH;
	job.foveaCodec = header.foveaCodec;
	if (!EnqueueJob(std::move(job))) {
		SetStatus("worker_unavailable");
	} else {
		SetStatus("queued_worker");
	}
	return Strip();
}

}  // namespace gxh1
