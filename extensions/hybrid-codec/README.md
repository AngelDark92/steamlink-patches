# hybrid-codec — GXH1 hybrid-frame demux and CPU decode

Client half of the hybrid fovea. The system overview, verification record and open gates
are in `GalaxyXR-Driver/Docs/HybridFovea.md`; this file covers the helper itself.

`libgxr_hybrid_codec.so` is a native helper for the Steam Link Galaxy XR client. It
demultiplexes the GXH1 hybrid-frame envelope written by the SteamVR driver, strips it so the
untouched base access unit reaches the single hardware decoder, and optionally decodes the
ASTC or PyroWave fovea patches on the CPU.

The **production default is demux-only**: the helper validates the fixed header, returns the
base AU, and does not run the CPU decoders. Display compositing and GPU ASTC sampling - the
plan's gate 3 - are **not implemented**, so CPU-decoding a layer nobody displays would only
cost frame time. `HybridSession::Status()` reports `demux_only`.

CPU decode remains available for qualification with `GXR_HYBRID_CPU_DECODE=1`. Host
measurements (`tests/perf_tests.cpp`) on a 1.5 MB base + 34 patches:

| Mode | Input thread | Worker |
| --- | --- | --- |
| ordinary AU, helper idle | 0.8 ns (helper) + one cached branch | none |
| selected, demux-only | 0.001-0.002 ms | none |
| selected, CPU decode | 0.19 ms ASTC / 0.52 ms PyroWave | 13.4 ms / 72.4 ms per frame |

The CPU decoders are therefore a bench tool, not a production path. An unpacked or
disabled helper changes nothing about how the client renders.

## Files

| Path | Role |
| --- | --- |
| `protocol/gxh1.h` | Canonical wire schema v2. The `GXH1-SCHEMA` block is byte-for-byte identical to `GalaxyXR-Driver/GalaxyXRDriver/src/Hybrid/Gxh1Protocol.h`; the driver's `tools/tests/hybrid-protocol-parity.test.cjs` checks it and the copied codec sources. |
| `src/gxh1_parse.{h,cpp}` | Bounded envelope parser and coverage validation (client copy of the driver's `Gxh1Codec`). |
| `src/astc_decode.{h,cpp}` | ASTC LDR single-partition subset (GXAST-v1). Decoder (spec order: weight infill, then UNORM8 endpoint interpolation; identical to astcenc and GPU sampling), `AstcInspectBlock`, and the matching encoder used to produce test payloads. |
| `src/pyrowave_decode.{h,cpp}` | PyroWave v1 CPU codec (GXPW-CPU-v1). The driver copy's stderr debug hooks are absent here. |
| `src/hybrid_session.{h,cpp}` | `HybridSession`: admission, strip, bounded CPU decode, newest-frame store, inline/worker budget. |
| `src/hybrid_entry.cpp` | The C ABI below. No C++ types cross the boundary and no exception escapes. |
| `tests/pure_tests.cpp`, `tests/Run-Tests.ps1` | Host tests built from the copied encoders. |

## Wire contract

One envelope per access unit: `header (80 B) | base access unit | fovea unit`.

```
EnvelopeHeader (packed, little-endian, 80 bytes)
  0  u32 magic            0x31485847 ('GXH1')
  4  u16 versionMajor     2       6  u16 versionMinor  0
  8  u16 headerBytes      80     10  u16 flags
 12  u32 sessionEpoch            16  u32 sourceEpoch    20  u32 mappingEpoch
 24  u64 frameId                 32  u64 ptsNs
 40  u32 baseBytes               44  u32 foveaBytes
 48  u32 baseOffset              52  u32 foveaOffset
 56  u8  baseCodec (1 HEVC, 2 AV1)  57  u8 baseFormat (0 NV12, 1 P010)
 58  u8  foveaCodec (0 none, 1 ASTC, 2 PyroWave)  59 u8 precision (8, 10)
 60  u16 canvasW   62  u16 canvasH   64  u16 patchCount   66  u16 featherPx
 68  u32 crc32   72  u32 reserved0   76  u32 reserved1
 80  base access unit          (unchanged NVENC bytes, baseBytes long)
     fovea unit   (= foveaOffset, foveaBytes long)
        FoveaUnitHeader (96 B) | PatchEntry[64 max] (48 B each) | payloads

FoveaUnitHeader (96 bytes)
  0  u32 unitBytes   4 u16 canvasW   6 u16 canvasH   8 u8 foveaCodec  9 u8 precision
 10  u8  patchCount 11 u8 flags     12 u32 mappingEpoch 16 u16 featherPx
 18  u16 reserved0  20 u32 reserved1..3
 32  EyeMapping eyes[2] (32 B each):
       u16 sourceW, sourceH    eye tile the canvas was cut from (source pixels)
       u16 originX, originY    canvas top-left in that tile
       f32 tanLeft, tanRight, tanTop, tanBottom   signed frustum, y-down
       f32 gazeX, gazeY        gaze in tile pixels (pixel i spans [i, i+1))
```

A patch entry's `texelW/texelH` (0 = native) give the encoded texture size when the driver
resampled that ring below native density; the payload is coded at that size and stretched
over the patch rect (the bench CPU decode does this bilinearly; the parser rejects a texel
grid larger than its rect or declared on one axis only).

The CRC is CRC-32/IEEE over the header with `crc32` zeroed, then the base access unit,
then the fovea unit. The fovea unit header repeats the canvas size, codec, precision,
patch count and mapping epoch; every patch entry repeats the source epoch. Patch payload
offsets are relative to the start of the fovea unit. `frameId` is the scene frame of the
base access unit: the driver attaches a fovea only to its own frame's base. ASTC frames
carry one patch per ring rectangle; PyroWave frames carry one whole-canvas patch per eye
(ring-weighted quantization inside the transform). v1 had no canvas placement and never
shipped; it is refused as `bad_version`.

Flags: `foveaPresent` (1), `reset` (2, epochs changed — drop all cached state),
`degraded` (4, no fovea this frame), `exactCore` (8), `snapshot` (16, mapping changed).

## Behaviour

`gxr_hybrid_on_access_unit(&data, &size)`:

* `0` — not a GXH1 envelope (the first four bytes are not the magic). Pointers are
  untouched; the caller keeps its normal path. This is the only cost when the feature is
  not in use.
* `1` — envelope consumed. `*data`/`*size` now describe the untouched base access unit,
  which the caller must still queue and decode. A structurally valid envelope whose
  patches fail to decode is also `1`: the base video survives, the enhancement layer is
  dropped.
* `-1` — malformed fixed header or unsupported schema version. Pointers untouched; the
  bytes are not video. The codecs-support layer returns the input buffer empty (the
  decoder sees a dropped frame, like network loss) and stays armed for the next envelope.

An integrity, coverage, mapping or epoch failure found after the fixed header (CRC, patch
table, `bad_mapping`, `epoch_mismatch`) returns `1`: the base access unit is still decoded
and only the fovea is dropped.

`gxr_hybrid_take_layer(eye, rgba, width, height, stride, frameId)` copies the newest
complete frame's canvas for one eye (`width * height * 4`, alpha 255 where a patch
covered the pixel, 0 elsewhere). It is non-destructive: repeated calls return the same
frame until a newer one completes. It fails when no frame is stored or when the requested
size does not match the canvas.

`gxr_hybrid_reset(epoch)` drops every cached frame. A non-zero epoch pins the session;
`0` means "adopt the epoch of the next admitted envelope".

`gxr_hybrid_probe()` returns 1 when the compiled wire layout matches the schema. It is a
compile-time constant, idempotent, allocation-free and side-effect-free — safe to call
from a loader check or from `gxr_hybrid_status()` consumers.

## Fail-closed rules

Rejected (the status reads `malformed:<reason>` and `HybridSession::Admit` reports the bare
reason; `epoch_mismatch` is the one non-`malformed` rejection):

* `too_small`, `bad_magic`, `bad_version`, `bad_header_bytes`, `bad_flags`;
* `length_overflow` — base/fovea length above 8 MiB, fovea unit smaller than its header,
  unit or payload ranges past the buffer;
* `bad_offsets` — base/fovea offsets not exactly adjacent, or inside the header;
* `bad_codec`, `bad_precision`, `bad_canvas` — unknown codec/format/precision, canvas
  outside the limit, zero canvas, gaze centre outside the canvas, feather above the limit;
* `too_many_patches`, `bad_patch_table` — patch table or payload ranges inconsistent,
  overlapping payload ranges;
* `bad_patch_geometry`, `patch_out_of_canvas` — block footprint outside 4..12, zero patch
  extent, patch crossing the canvas edge, inverted radii;
* `coverage_gap` — both eyes must tile the declared canvas exactly on a 16-pixel grid.
  The tiling check is also what rejects overlapping patches (the parser folds overlap into
  coverage and never returns `patch_overlap`);
* `bad_mapping` — an eye canvas outside its source tile, an empty tile, an inverted or
  non-finite frustum, or gaze outside the tile;
* `bad_epochs` — patch/unit epochs disagree with the envelope, or the frame's
  `sessionEpoch` differs from the admitted one without the `reset` flag (the driver sets
  `reset` on the first envelope of every epoch);
* `bad_crc`.

Runtime bounds, all enforced before allocation:

* at most 2 complete frames are retained, newest `frameId` wins and an older frame that
  arrives late is dropped;
* at most 64 patches per frame (`ParseLimits::maxPatches` may lower this, never raise it);
* canvas sides up to `kMaxCanvasPixels` (4096) and decoded patches up to `kMaxPatchPixels`
  (2 Mi px); the driver refuses larger plans before capturing;
* 64 MiB of decoded canvas per frame (both eyes) and 128 MiB for all retained frames — a
  2880x2880 per-eye canvas fits, anything larger is refused rather than exhausting the
  client process;
* every payload length is checked against the declared geometry before a decoder runs.

Latency: a frame decodes inline only while it is small enough to plausibly fit
(`foveaBytes <= 256 KiB`, `<= 8` patches, canvas `<= 1 Mi cells`) **and** the measured
decode stays under 8 ms. One measured overrun disables inline decode for the rest of the
session; later frames go to a single worker thread that keeps at most one pending job
(newest wins). The caller is never asked to wait for a decode.

## Integration (zero cost when unused)

`extensions/codecs-support-layer/src/codecs_support_layer.cpp` resolves this helper once,
when the first decoder of a session arms (`HybridEnabled()`), and caches the result in
`g_hybridArmed`. `WrapQueueInput` then has exactly one guarded branch: armed → call
`gxr_hybrid_on_access_unit`, and on `1` move the base access unit back to the queued
offset before `queueInputBuffer`. When the helper is not present, not packaged, fails its
probe, or `GXR_DISABLE_HYBRID_CODEC` is set to anything but `0`, `g_hybridArmed` stays
false and the wrapper executes exactly the code it executed before this feature existed:
one bool test, no allocation, no extra lock, no call into the helper. The output path is
not touched.

Kill switch: `GXR_DISABLE_HYBRID_CODEC=1` (any non-empty value other than `0`).

## Build and test

```powershell
# Android helper -> ../builds/steamlink-patches/build/hybrid-codec-work/lib (add -Stage to
# write the pinned resource; then update HYBRID_CODEC_LIBRARY_SIZE/SHA256 and the test pin)
powershell -NoProfile -ExecutionPolicy Bypass -File extensions/hybrid-codec/Build-Native.ps1

# Host tests (portable MSVC; artifacts under ../builds/steamlink-patches/build/hybrid-codec-work)
powershell -NoProfile -ExecutionPolicy Bypass -File extensions/hybrid-codec/tests/Run-Tests.ps1
```

NDK 27.2.12479018, `aarch64-linux-android29-clang++`, C++17, `-Wall -Wextra -Werror`,
`-fvisibility=hidden` (only the five ABI symbols are exported), `-static-libstdc++`,
16 KiB page alignment. No third-party dependencies.

The host suite prints `N checks, M failures` and exits non-zero on any failure. It covers
the golden envelope round trip for both codecs, ASTC block decode and PyroWave frame
decode from known encoded inputs, malformed magic/version/length/CRC/coverage/overlap
rejection, epoch admission, degraded frames, frame ordering, and that
`gxr_hybrid_probe()` performs no allocation (counted through a global `operator new`).

## Limits

* No display compositing, no eye-texture upload, no feathering of patch edges: the RGBA
  canvases are produced and cached only. Gate 3 of the ASTC streaming plan (colour-space
  and capacity proof) must land before anything consumes them.
* ASTC is the 8-bit sRGB subset only. A `precision == 10` envelope is accepted by the
  schema but the ASTC path never claims RGB10; PyroWave output is 8-bit RGBA after
  YCbCr reconstruction.
* The decoder is not hardware accelerated; the budget above is a latency guard, not a
  quality trade-off.
