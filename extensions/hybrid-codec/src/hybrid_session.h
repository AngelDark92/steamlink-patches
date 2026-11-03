// GXH1 hybrid-frame demultiplexing session.
//
// The SteamVR driver can wrap the selected NVENC access unit in a GXH1 envelope that
// carries ASTC or PyroWave fovea patches for both eyes. This class is the client half:
// it validates the envelope (bounded, fail-closed), strips it so the caller keeps decoding
// the untouched base access unit, decodes the patches on the CPU into one RGBA canvas per
// eye, and keeps the newest complete frame.
//
// Bounds and guarantees:
//  - at most kFrameSlots complete frames are retained, at most gxh1::kMaxPatches patches;
//  - every payload length is validated by the parser before any allocation;
//  - the caller is never blocked for long: a frame decodes inline only while the measured
//    cost stays under kInlineBudgetUs, otherwise one worker thread does it;
//  - a frame whose session epoch differs from the admitted envelope is rejected unless the
//    envelope carries gxh1::kFlagReset (the driver's "epochs changed" signal).
//
// Display compositing is NOT part of this class: it produces canvases, it never blends
// them into the client's eye textures.
#ifndef GXR_HYBRID_SESSION_H
#define GXR_HYBRID_SESSION_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../protocol/gxh1.h"
#include "gxh1_parse.h"

namespace gxh1 {

// One eye of one decoded frame. `rgba` is canvasW * canvasH * 4 bytes, top-down rows,
// alpha 255 where a patch covered the pixel and 0 elsewhere.
struct HybridLayerView {
	const uint8_t* rgba = nullptr;
	int width = 0;
	int height = 0;
	uint64_t frameId = 0;
};

class HybridSession {
public:
	HybridSession();
	~HybridSession();

	HybridSession(const HybridSession&) = delete;
	HybridSession& operator=(const HybridSession&) = delete;

	// Drop every cached frame. A non-zero epoch pins the session epoch: later envelopes
	// with a different sessionEpoch are rejected unless they carry kFlagReset. Zero means
	// "adopt the epoch of the next admitted envelope" (a fresh client session).
	void Reset(uint64_t sessionEpoch);

	// Validate one complete envelope against `limits` without decoding it, and pin the
	// epochs it declares. Returns 0 on success and -1 on rejection; `reason` receives the
	// parser reason name when it is non-null. Never allocates canvases.
	int Admit(const uint8_t* data, size_t bytes, const ParseLimits& limits, std::string* reason);

	// Feed one access unit from the decoder's input queue.
	//   0  not a GXH1 envelope: the caller keeps its own pointers, no work was done;
	//   1  envelope consumed: *data / *size now describe the untouched base access unit;
	//  -1  malformed fixed header (or an unsupported schema version): the pointers are
	//      untouched and the caller must not queue the bytes as video; it drops this access
	//      unit only and keeps the helper armed.
	// Large frames are validated (CRC, patch table, coverage) and decoded on the worker;
	// an integrity failure there sets Status() to "malformed:<reason>" and drops only the
	// fovea, because the base access unit has already been returned untouched.
	int OnAccessUnit(const uint8_t** data, size_t* size);

	// Newest complete frame. Non-destructive: repeated calls return the same frame until a
	// newer one completes. Returns false when no frame is stored.
	bool TakeLatest(uint64_t& frameId) const;

	// Copy one eye of the newest complete frame into `rgba`. Requires
	// width/height equal to the canvas size and stride >= width * 4. Non-destructive.
	bool CopyLayer(int eye, uint8_t* rgba, int width, int height, int stride, uint64_t* frameId) const;

	// Persistent diagnostic string ("ok", "strip_only", "demux_only", "malformed:bad_crc", ...).
	const char* Status() const;

	// Production default is demux-only: strip the envelope and validate the fixed header,
	// but do not run the CPU patch decoders (the plan's production path is GPU sampling,
	// which is not implemented yet). Bench/qualification tools enable CPU decode explicitly.
	void SetDecodeEnabled(bool enabled);

	// True once an inline decode overran kInlineBudgetUs; later frames use the worker.
	bool InlineBudgetExceeded() const;

	// True when the last admitted envelope had a fovea unit.
	bool HasFovea() const;

private:
	static constexpr size_t kFrameSlots = 2;
	// 64 MiB per frame (both eyes) and 128 MiB for all retained frames. A 2880x2880
	// per-eye canvas (66.4 MB per frame) fits; anything larger is rejected before
	// allocation instead of exhausting the client process.
	static constexpr uint64_t kMaxFrameDecodedBytes = 64ull * 1024ull * 1024ull;
	static constexpr uint64_t kMaxTotalDecodedBytes = 128ull * 1024ull * 1024ull;
	// Hard latency budget for an inline decode. Beyond it the frame goes to the worker.
	static constexpr uint64_t kInlineBudgetUs = 8000;
	// Conservative pre-gate: past these sizes an inline decode cannot fit the budget on
	// a mobile core, so the frame is dispatched without measuring first.
	static constexpr uint32_t kInlineMaxFoveaBytes = 256u * 1024u;
	static constexpr size_t kInlineMaxPatches = 8;
	static constexpr uint64_t kInlineMaxCanvasPixels = 1ull << 20;

	struct StoredFrame {
		std::vector<uint8_t> eye[2];
		uint64_t frameId = 0;
		uint16_t width = 0;
		uint16_t height = 0;
		bool valid = false;
	};

	// A patch as the decoder needs it: geometry plus the payload offset inside the unit
	// buffer it was built from (the caller's fovea unit inline, the job copy on the worker).
	struct PendingPatch {
		PatchEntry entry{};
		uint32_t offset = 0;
	};

	struct DecodeJob {
		PendingPatch patches[kMaxPatches];
		size_t patchCount = 0;
		std::vector<uint8_t> payload;
		// Set when the job carries a complete envelope copy and must be parsed on the
		// worker (large frames never run ParseFrame/CRC on the MediaCodec input thread).
		std::vector<uint8_t> envelope;
		bool parseFromEnvelope = false;
		uint64_t frameId = 0;
		uint64_t generation = 0;
		uint16_t canvasW = 0;
		uint16_t canvasH = 0;
		uint8_t foveaCodec = kFoveaCodecNone;
		bool pending = false;
	};

	// Parse + epoch admission. Caller holds mutex_ (SetStatus itself is lock-free).
	ParseError AdmitLocked(const uint8_t* data, size_t bytes, const ParseLimits& limits, ParsedFrame& parsed,
	                       const char** reasonName);
	// Publishes one of the fixed status literals; lock-free so a reader never races a
	// worker-thread update.
	void SetStatus(const char* text);
	int OnAccessUnitImpl(const uint8_t** data, size_t* size);

	bool DecodeFrame(const uint8_t* unit, const PendingPatch* patches, size_t patchCount,
	                 uint16_t canvasW, uint16_t canvasH, uint8_t foveaCodec, StoredFrame& out) const;
	bool CommitFrame(uint64_t generation, StoredFrame&& frame);
	void DropFramesLocked();

	void EnsureWorker();
	bool EnqueueJob(DecodeJob&& job);
	void WorkerMain();

	mutable std::mutex mutex_;
	StoredFrame frames_[kFrameSlots];
	size_t newestSlot_ = kFrameSlots - 1;  // no valid frame yet
	size_t frameCount_ = 0;
	uint64_t totalStoredBytes_ = 0;

	bool epochPinned_ = false;
	uint32_t sessionEpoch_ = 0;
	// Bumped on every reset/admission generation; a job from an older generation never commits.
	uint64_t generation_ = 1;
	ParseLimits limits_{};
	bool hasFovea_ = false;
	bool inlineDisabled_ = false;
	std::atomic<bool> decodeEnabled_{true};
	std::atomic<const char*> status_{"ok"};

	std::mutex workerMutex_;
	std::condition_variable workerCv_;
	std::thread worker_;
	DecodeJob job_;
	bool workerStop_ = false;
};

}  // namespace gxh1

#endif  // GXR_HYBRID_SESSION_H
