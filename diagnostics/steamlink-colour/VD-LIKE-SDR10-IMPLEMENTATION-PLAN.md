# Plan: VD-Like SDR10 Color Pipeline

Date: 2026-09-19. Status: detailed planning handoff. SteamLink-side implementation slices A–F completed and verified 2026-09-19 (see Section 10); host side (CustomHeadsetOpenVrGxR) not started; no runtime validation, installation or commit.

## Persistence and Authorization

This is the real, detailed workspace Markdown handoff that a less-capable implementation agent can execute. Keep this plan self-contained because `build/vd-audit` is ignored and may not accompany a checkout. No product implementation is authorized merely by saving this plan. Do not install an APK/driver, restart SteamVR, change live settings, or run device reproductions without explicit authorization. This task is planning only.

The following user decisions were explicitly collected:
- Cover all 7 existing OLED color-supported exact Steam Link bases.
- Leave 2.0.22/5002296 high-resolution-only; DO NOT add color compatibility to it.
- Preserve every existing default and calibration control. New behavior is explicit opt-in and reversible.
- Include a VD-like noise-free baseline plus optional measured APK-side dithering. Dithering must NOT be represented as proven VD behavior.
- Target the actual sibling CustomHeadsetOpenVrGxR repository, not the older CustomHeadsetOpenVR or GalaxyXR-APK projects.

## 1. Goal and Non-Goals

Goal: request genuinely negotiated HEVC Main10 SDR through the existing Valve pipeline; avoid unnecessary host quantization/color modifications; preserve decoder/import precision where supported; perform correct range/transfer interpretation; submit nonlinear RGB in the existing 8-bit sRGB projection targets; separately measure optional dither near that quantization boundary.

This is semantic alignment with VD's confirmed behavior, not reproduction of its proprietary implementation or a promise of identical visual quality. Do not copy VD GLSL bodies into shipped code.

Excluded: HDR/PQ/HLG signaling, tone mapping for ordinary SDR, compositor bypass, kernel/panel/DSC changes, forced RGB10/FP16 presentation, new streaming protocol/GXRP messages, new encoders, fake P010 declarations, controller/tracking changes, resolution/topology refactors, retirement reversals, global default changes, bitrate/preset tuning as a confounder, changes to 5002296.

Do not invent a `VideoOutputPrecision.VD_10BIT_EXPERIMENTAL` value. Input bit depth and output storage are separate. The desired output mode already exists: `srgb8-highp`.

## 2. Verified Facts and Confidence

### Virtual Desktop Reference

Supplied VD version 1.34.18.0 / 10683. The assembly store hash matched the prior audit:
`2b879cfa184921586e5ee6e25c777f3c7c65ce45e51e1fbad3676ca3f5cb3505`.

Local reference: `build/vd-audit/TEN-BIT-FINDINGS.md`.
- MediaCodec -> PRIVATE AImageReader -> AHardwareBuffer/EGLImage -> GL texture -> shader -> ordinary OpenXR color swapchains.
- The traced VR color swapchains use `GL_SRGB8_ALPHA8` (35907). Motion-vector FP16 is NOT color output.
- The video shader treats sampled components as YUV and applies full/limited-range conversion.
- `VideoPlayer.DrawVR` disables `GL_FRAMEBUFFER_SRGB_EXT` (36281/`0x8DB9`) around video draws, then restores it. This is consistent with writing already nonlinear video RGB without double encoding.
- `ShouldUseGammaBoost` returns false unconditionally; the power-1.5 shader branch is dormant.
- HDR-to-SDR processing is separately conditional on `IsHDR`, not HEVC bit depth.
- No custom shader dither was found in the inspected path. `GL_DITHER` defaults true in GLES, but effective driver/runtime dithering was not measured.
- Actual decoded low-bit preservation, sampler precision, runtime processing, and host encoder internals were not proven.

### Steam Link APK

`patches/src/main/kotlin/app/template/patches/steamlink/binary/OledCalibrationPatch.kt`:
- `paddedVideoShader` already uses `highp float` AND `highp samplerExternalOES`.
- Existing recipe: `profile=neutral`, `outputPrecision=srgb8-highp`, `dithering=off`, `use8BitOutputWhenDithering=false`.
- Neutral sets gamma=1 and saturation=1, but RETAINS Valve's `_valve1_d2020d709` matrix. It is not an identity transform end to end. Never silently remove that matrix.
- The sampled texture is treated as RGB by this shader, unlike VD's sampled YUV. Adding VD's YUV-to-RGB conversion risks double conversion.
- The 1087-byte common fragment leaves `main()` OPEN. Valve appends opaque/masked alpha suffixes. Preserve byte length, NUL boundary, inputs/outputs, uniform locations 2..6, and suffix contracts.
- `setProjectionSwapchainFormat` selects exact version/build layouts, checks size, per-site original/known format instructions and surrounding contexts, and writes into a copy. Source SHA-256 constants are present but the helper does NOT simply hash-enforce all accepted patched inputs. Do not claim otherwise.
- Existing dither off/low/standard is in nonlinear code space, before EOTF for linear alternatives. Low is approximately 0.5 8-bit code peak-to-peak, Standard approximately 1 code peak-to-peak. These are full spans, not +/- amplitudes. Existing scales are 0.00196/0.00392; preserve until a numeric test establishes a reason to change them.
- Existing `OledDecodedCompatibilityAudit` currently covers 5001712, 5002322, 5002363, NOT all 7 layouts.
- Existing `VideoOutputPrecisionTest.layouts` covers 6 layouts and omits 5002363.

Native evidence:
- `diagnostics/steamlink-direct-surface/README.md` documents exact 5002322 `QSVLCodecNDK` -> PRIVATE reader -> hardware buffer -> `FlipFrame` -> EGLImage -> `SRGBCorrectionPass`. Java SteamLink `SurfaceTexture` is not the XR decoder owner.
- `diagnostics/steamlink-colour/OLED-COMPATIBILITY-NATIVE.md` and `diagnostics/steamlink-5002363/native-targets.md` establish active shader/swapchain call paths for specific bases. They do not prove sampled color semantics or current headset GL state.
- Historical September 6 captures report 10-bit host selection/P010 output; these are not new-session proof and do not establish all 7 clients.

### GxR Host

Root: `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR`.
- It is a SteamVR driver/postprocessor around Valve vrlink, NOT a separate streaming host/client implementation.
- `GalaxyXrConfig` already contains `vrlinkHeadsetProfile=true`, `profileSupports10bit=true`, `force10bit=false`. The active owner is `GalaxyXR.cpp::ApplyHeadsetProfileSetting`, which writes `supports10bit` in `vrlink_<actual model>`. `GalaxyXR_EarlyApplyVrlinkSettings` applies settings before the headset attaches.
- `force10bit` is retained schema but NOT an active force path: `ApplyHeadsetProfileSetting` removes a legacy `driver_vrlink.force10bit=true` key. Do not reactivate it.
- `FrameProcessor.cpp::MapLayerFormat` supports RGBA8/BGRA8 typed/sRGB variants and `R10G10B10A2`. Hardware sRGB and manual transfer paths differ intentionally. Scratch copies must remain in legal DXGI format families.
- `vrlink_layer_ps.hlsl` includes color controls and host-side `InterleavedGradientNoise` before encoding. This is distinct from APK post-decode dithering.
- `FrameProcessor::ProcessSceneLayer` constructs `NvencTapConfig`/`NvencPostPackConfig`. It currently sets `vuiFullRange=0` from requested `postPack.enable && postPack.limitedRange`.
- `NvencPostPack.cpp::EnsureCache` accepts NV12 via R8/R8G8 and P010 via R16/R16G16 views. `kPsLuma`/`kPsChroma` nevertheless use 8-bit range constants. `Process` currently ignores its `nvencBufferFormat` argument and returns false for several failure/skip paths.
- `NvencTapShims::EncodePicture` calls `Process` before encode and currently ignores its bool result. Thus requested range/VUI state is not proof of successful pixel conversion. Treat this as a risk to isolate in the new baseline, not justification for a broad encoder refactor.
- `NvencTap::Guarded` snapshots/restores caller structures and retries pristine on failed overrides. `RecordLast` is called before attempted overrides complete; requested stats must not be labeled accepted runtime state without accounting for failure/pristine retry.
- No new `encInputFormat` override is needed. NV12 bytes do not become P010 by changing a struct field. No new NVENC API session upgrade is needed just to request Main10; preserve current ABI gates.

## 3. Exact APK Matrix

All sizes, hashes, and existing format sites below come from the current production layout table. They are starting evidence, not a substitute for rechecking each decoded library and active caller.

| Version/build | Size bytes | SHA-256 | Format sites |
|---|---:|---|---|
| 2.0.20/5001712 | 2221072 | `80b62797c7e26d6b67b0cca00693b076a336bdb48ebc1383a16cccb1616ed495` | `0x10a9c4`, `0x10aa34` |
| 2.0.20/5001740 | 2220528 | `5fbb76c06c9fc0e3e5c5825752aa17e040462c8551b69d3492265f620244f443` | `0x10a854`, `0x10a8c4` |
| 2.0.22/5002244 | 2251920 | `4b2fa5e1b5d9d5c938873f692b0e5e18159e1199dee1253dd6eccc8fa43dfa12` | `0x10826c`, `0x1082dc`, `0x10834c` |
| 2.0.22/5002313 | 2276872 | `e4d3575a130dc013e4c8fe4fb965217028229f89b13ba821c01b492e457398bb` | `0x10b2d4`, `0x10b344`, `0x10b3b4` |
| 2.0.22/5002318 | 2277488 | `3c8d1ce13fd61edff5ce65efe6eedcc8565c89b66bab371550986a5c75407e56` | `0x10b430`, `0x10b4a0`, `0x10b510` |
| 2.0.22/5002322 | 2283400 | `e61baf34dfc4749d92561bab5fee47891d271607a0ce44824ff61c3e6a450c3f` | `0x10ba78`, `0x10bae8`, `0x10bb58` |
| 2.0.23/5002363 | 2292008 | `628821feab199d7712be8a51273eb9a21ec440a7c91aa6a768cc7307a4fe22f0` | `0x10c840`, `0x10c8b0`, `0x10c920` |

Stock sRGB instruction: `69 88 91 52`. Known RGB10 alternative: `29 0b 90 52`. Known FP16: `49 03 91 52`. These change projection storage only.

Preserve native topology: 5001712/5001740 have 2 stock create-site families; other listed bases have 3. Preserve the current high-resolution adaptation layered on top; do not overwrite its session/layer handling. The fixed shader prefix is `0x9b4b8` for 5001712, `0x96ba5` for 5002322, `0x970a1` for 5002363; derive the others with `findVideoShader` and native reference tracing, not arithmetic offsets.

5001740 has static-analysis-only provenance until pristine APK is available. 5001712 includes a documented analysis reconstruction. Re-inventory actual fixtures before stating pristine packaging coverage. Missing pristine/decoded inputs are explicit BLOCKED rows, not silently skipped PASS results.

## 4. Recommended Design

### 4.1 Reuse the APK Behavior

Do NOT add a duplicate OLED patch or output-format enum. Ship a documented explicit patch recipe selecting the existing neutral + `srgb8-highp` + off configuration. Preserve the global OLED `default=false`, existing final-balanced profile default, bundle membership and per-version defaults. Selecting neutral explicitly is part of this opt-in workflow.

Only edit shader/GL behavior if Phase 1 proves a concrete mismatch. If the sampled external texture is already nonlinear RGB, retain the existing highp RGB shader path. If sRGB framebuffer conversion is already bypassed, do not add another state change. If a mismatch is proven, implement the smallest exact-build-gated fix at the owning render operation, with state restoration and regression tests. Never globally disable `GL_FRAMEBUFFER_SRGB` for UI/other rendering. Do not blindly remove Valve's matrix.

### 4.2 Add 1 Reversible Host Baseline Policy

Proposed persistent field: `GalaxyXrConfig::sdr10Baseline`, default false. GUI name: SDR 10-bit baseline. It requests a baseline; runtime status must separately say requested/observed/unknown/fallback, never imply proven panel depth.

The policy is an effective override, NOT a destructive preset migration. Stored user controls are left intact and resume when disabled. Do not bump existing `nvencSettingsVersion`/`streamFrameSchema` to overwrite preferences.

While enabled:
- Effective headset profile enabled; `supports10bit` requested=true using existing model-aware settings writer and settings journal. `force10bit` remains retired.
- Host video color controls effective-neutral: saturation 50, vibrance 0, contrast 50, gamma 2.2, RGB multipliers 1, brightness 1, optional color matrix disabled, black-floor `rangeMode`/`shadowLift`/`blackPoint` disabled.
- Host `streamFrame.dither=false`. APK dither remains independent, selected at patch time.
- Effective `postPack.enable=false` for this baseline, so no host YUV range remap or post-pack sharpening. This deliberately avoids relying on unverified P010 normalization and requested-only VUI coupling.
- Effective `nvencVuiFullRange`/`nvencVuiMatrix`/`nvencVuiPrimaries`/`nvencVuiTransfer` all -1: retain Valve's ORIGINAL pixel/metadata pair and matching client interpretation. This is a conservative baseline, NOT proof stock metadata is standards-correct. Gate promotion on measured round-trip colors. Do not compensate by relabeling pixels.
- Existing bitrate, rate-control, preset, split-frame, QP safety, transport, render resolution, atlas/foveation, tracking, distortion calibration, synchronization and timeout policies remain unchanged.
- Blackout and stationary-dimming safety controls remain functional. Validation captures must mark frames affected by them invalid, not disable safety globally.
- For neutral numeric comparison, FXAA and pre-encode CAS must be disabled in the controlled test fixture/recipe; they may remain user controls in normal mode. Preserve lens-distortion calibration; use flat test regions or a separate shader harness to avoid mixing geometry into color assertions.
- Do not auto-enable a disabled `streamFrame` master, install a hook, or restart SteamVR via GUI merely by drawing the settings page. Show baseline inactive if required host processing is inactive. The headset profile request still follows explicit baseline opt-in through the existing startup writer.
- Do not silently disable arbitrary per-device custom shaders; detect conflicts and show a not-baseline status. Test recipe requires those shaders off.

Reuse a compact pure resolver so GalaxyXR startup and FrameProcessor per-frame policy cannot disagree. Proposed new header, only because 2 owners plus tests share it: `CustomHeadsetOpenVR/src/Config/SdrColorPolicy.h`. Return a small value object/flags, not a deep copy of distortion vectors every frame. If the existing owner offers an equivalent pure helper, use that instead. Resolve from the current immutable settings snapshot. Do not let one frame mix pre-toggle NVENC policy with post-toggle color constants.

Add a status requirement: changes requiring startup/codec negotiation are PENDING_RESTART/RECONNECT, not hot-applied 10-bit. Switching profile during streaming does not prove Main10 took effect. No automated restart.

### 4.3 Optional Post-Decode Dither

Reuse APK `VideoDitherMode` LOW/STANDARD. Compare only after noise-free baseline passes. Host dither stays off during this experiment. No new dither library or shader is needed initially.

Validate that `UniDitherOffsets` updates for every supported native base and both opaque/masked shader programs, that the uniform remains at location 6, and that endpoints/alpha/fade remain intact. Existing native phase wraps at 1024 on traced bases; recheck all 7.

Do not describe app-side dither as final panel/compositor dithering. No guarantee of improvement: if the compositor/filtering suppresses it or visible grain/flicker worsens, leave it off and report that result.

### 4.4 P010 Post-Pack Follow-Up Is Gated, Not Mandatory Baseline Code

Because baseline bypasses post-pack, repairing its legacy processing is NOT a prerequisite to shipping the baseline controls. Test the suspected normalization issue and record it. Only include a format-aware post-pack fix as a separately opt-in follow-up if the user later needs limited-range conversion or post-pack sharpening in the baseline. Do not expand scope to a global range-policy refactor automatically.

Required future numeric contract:
- NV12 view normalization is byte/255.
- P010 nominal 10-bit code `k` occupies the high 10 bits of a 16-bit word: `word=k*64`, lower 6 bits zero, `R16_UNORM sample=word/65535`.
- Recover nominal `q=sample*65535/(64*1023)`, not just sample interpreted as code/1023. Quantize/repack to valid 10-bit codes only on the intended output write.
- 8-bit limited Y endpoints 16,235; C 16,240 with neutral 128.
- 10-bit limited Y 64,940; C 64,960 with neutral 512. Full-range maximum 1023. Use explicit code-domain endpoint/center mappings; do not rely on 0.5 as an exact code midpoint.
- Test lower 6 bits after GPU writes, adjacent source codes, neutral chroma, clipping and tile boundaries.
- Match `nvencBufferFormat`, actual DXGI desc, picture format, and accepted encoder depth; reject mismatches without reinterpreting bytes.
- Pixels and accepted VUI are a session contract. On conversion failure, no mismatched frame may be silently encoded. In-place metadata retry is not safe if pixels were already converted. An implementation must prepare/validate resources before committing to conversion and define a tested rollback or clean-reconnect path; unsupported VUI reconfiguration must fail clearly. This requires a separate design gate if implemented.

## 5. Execution Phases and Dependencies

### Phase 0: Persist and Freeze Evidence

1. Read parent/root `AGENTS.md`, specific repository instructions, `WORKSPACE_CLEANUP.md`. Required `galaxyxr-workspace` skill is present but currently a TODO scaffold; parent rules still govern. `morphe-patches` skill contains older compatibility examples; current `AGENTS.md` and Constants exact-pair rules win.
2. Record each repo's HEAD, dirty status, exact file hashes for touched/inspected source and installed artifacts IF installation is later authorized. Do not reset or commit existing work.
3. Inventory 7 source bases and pristine APK availability; write provenance matrix. Freeze the current host GPU/driver/SteamVR/vrlink versions only when actual capture is authorized; do not infer them from old logs.
4. Record VD facts above in the tracked plan so ignored extracted files are optional supporting evidence. Do not commit proprietary extracted binaries or shader code.

Exit: roots unambiguous, source/provenance matrix complete with blocked rows clearly labeled.

### Phase 1: Color Contract and Regression Tests

This phase blocks any conditional shader or metadata mutation. Host and APK evidence work can run in parallel.

APK tasks:
1. Trace `QSVLCodecNDK::Init`, image callback, FrameServer, `QSVLRendererXR::FlipFrame`, EGL import, `SRGBCorrectionPass::RenderSpecificPrep`/`RenderSpecific`, and `SetupSwapchains` for each exact base. Reuse existing native audits/symbol-resolution tooling. Preserve PT_LOAD file/virtual mapping; no neighbor offsets.
2. Record active texture target/sampler, buffer format request, EGL import attributes, actual color interpretation inferred from code, `glEnable`/`glDisable` state around draw, selected output attachment, uniform phase and both alpha suffixes.
3. Establish whether external sampling already returns RGB and whether automatic sRGB output conversion is bypassed. If unavailable statically, mark runtime gate; do not guess from texture target alone.
4. Add 5002363 to `VideoOutputPrecisionTest.layouts`. Extend existing `OledDecodedCompatibilityAudit` to all 7 with exact fixture provenance. If 5001740 fixture unavailable, leave a blocking report, never create a fake neighbor-derived fixture.
5. Characterize baseline shader bytes before changes and test existing options/defaults/dependency closures.

Host tasks:
1. Unit-test pure policy default-off passthrough and opt-in effective values before wiring GUI. Verify stored config not modified and disabling restores exact prior preferences.
2. Verify model-aware `supports10bit` startup route, including actual identity strings used by each APK configuration. No unconditional `xrvst2ue`-only assumption; the early fallback must be checked against the actual connected model and captured vrlink section.
3. Add bounded observation of requested/accepted encoder settings and resource/picture DXGI/NVENC format pairing; do not add pixel-format overrides.
4. Numerically characterize NV12/P010 post-pack range math separately. This is evidence for the bypass decision; do not fix unrelated legacy behavior in this phase.
5. Derive a color contract table: stage, pixel domain, storage type, range, matrix, transfer, owner, proof level, test artifact. For unknown fields use UNKNOWN, not guessed BT.709/sRGB labels.

Exit: default/off regression tests pass; evidence distinguishes requested settings, accepted settings, actual samples, and output storage; any transfer/matrix mismatch has an exact owner and reproducer.

### Phase 2: Host Opt-In Baseline

Depends on Phase 1 policy tests. Can proceed alongside APK audit expansion, but not runtime rollout.

1. Add `sdr10Baseline=false` to `GalaxyXrConfig` in `Config.h`. Parse and serialize it in `ConfigLoader.cpp`; publish it wherever GUI default/config info expects Galaxy XR fields. Missing field means false; reject/ignore invalid types using existing parser convention. Do not alter existing migrations.
2. Implement pure policy resolver in `SdrColorPolicy.h` or an existing suitable helper. Test every effective control listed in Section 4.2, including blackout/dimming preservation and untouched geometry/network settings.
3. Wire effective profile booleans in `GalaxyXR.cpp::ApplyHeadsetProfileSetting`. Keep `GalaxyXR_EarlyApplyVrlinkSettings` and journaled writes; preserve cleanup of retired `force10bit`. Add requested-not-confirmed logging and pending-reconnect semantics.
4. Wire effective color constants and `NvencTap`/`NvencPostPack` configuration in `FrameProcessor.cpp::ProcessSceneLayer`/`ProcessEye`. Baseline bypasses post-pack and manual VUI overrides, including their failure path. Host dither disabled only effectively. Use the same frame policy snapshot for both eyes and encode config.
5. Verify frame processing fallback shader cannot silently claim all baseline controls executed. Neutral fallback may qualify only if it preserves the contract; log which shader was loaded and hash its source.
6. Add 1 toggle using existing Angular Material patterns in `driver-settings.component.html`, logic in `GalaxySettingsBase` if needed. Preserve imported forms/default/save behavior. Disable or mark overridden color controls in baseline without overwriting their stored values. State requested/needs reconnect/observed/unknown distinctly. APK recipe remains a documented separate step, not an invented host-to-APK settings channel.
7. Add field to `JsonFileDefines.ts`. Regenerate `driver-defaults.ts` via `Generate-DriverDefaults.ps1`; never hand-maintain duplicate defaults.
8. Update host `Docs/StreamFrame.md` with exact APK recipe, baseline override list, rollback and limits.

Exit: policy tests, config/default generation checks, GUI build and native staged build pass. Off-path behavior and saved preferences unchanged. No deployment.

### Phase 3: APK Baseline and All-Base Verification

Depends on Phase 1. Independent of host UI work until integration.

1. Reuse `oledCalibrationPatch` with `profile=neutral`, `outputPrecision=srgb8-highp`, `dithering=off`, `use8BitOutputWhenDithering=false`. Leave custom sliders/legacy defaults as they are.
2. If existing path meets measured contract: no production shader edit. A documented configuration plus coverage/diagnostics is a valid result.
3. If proven double-transfer or matrix mismatch exists: stop before generic patching; record exact native owner, derive every base's preconditions, add only isolated opt-in behavior. Maintain legacy emitted shader bytes when opt-in is off. Do not repurpose reserved uniforms or add binary calls without traced ABI/state restore proof.
4. Build complete opaque/masked shaders from production common prefix plus actual per-base suffixes and compile/link on a GLES-capable harness. A substring test is not shader compilation. Keep `main` open in the common block.
5. Extend real-byte audit: all profiles x all 3 output precisions x all 3 dither modes, all transitions, checkbox true/false; exact-diff allowlist, shader size/NUL/interface, known format guards, idempotence, mismatched tuple/size/site/mixed-state rejection.
6. Include existing recommended patch dependencies in composition tests so OLED updates cannot overwrite native high-resolution fixes. Unsupported build dependencies must return unchanged before file access.
7. Keep generated catalogs synchronized if metadata/options actually change. Do not regenerate catalogs unnecessarily merely because tests change.
8. Package only from pristine APKs using existing Morphe workflow. Compare selected patch options, resulting `.so` bytes and hashes inside final APK; do not trust a UI checkbox alone. Mark reconstructed-only rows unpackageable/unverified, not compatible by fiat.

Exit: all available decoded rows have PASS/FAIL/BLOCKED, no implicit compatibility extension, whole-APK validation distinguished from native helper tests.

### Phase 4: Diagnostics and Optional Dither

Depends on baseline numeric contract and Phase 2/3.

1. Extend `Check-SteamLinkColour.ps1` structured derived report rather than build an unrelated collector. Preserve Capture/Snapshot/Offline/SelfTest meanings and timeout/identity/session filters.
2. Add a goal-specific `Sdr10ToSrgb8` field so 8-bit projection output can be reported as EXPECTED_OUTPUT rather than overall failure. Preserve the old storage-depth fact and never award panel-10bit PASS.
3. Required evidence fields: exact APK pair/hash/options, host version/hash/GPU/driver/vrlink version, session identity/time, requested `supports10bit`, actual host negotiation marker, accepted encoder profile/depth, resource and picture formats, SPS bitdepth/colour metadata where captured, decoder output buffer evidence, sampled shader stages, draw-time sRGB and dither state, actual submitted projection formats.
4. Keep proof levels explicit: capability/request/config/accepted allocation/actual sampled contents/submission/display are different. Missing logs produce UNKNOWN. Failed-override statistics must reflect pristine retry rather than attempted values.
5. Reuse existing resolution trace telemetry for projection identity/format; do not install or resurrect Surface-video pixel-copy experiments. If new draw-time tracing is necessary, make it observation-only, exact-build guarded, rate-limited and separate from production behavior. A runtime layer cannot claim decoder low bits from `xrCreateSwapchain` alone.
6. For bitstream inspection, capture a bounded authorized local test sequence via an existing supported debug/capture path, or add a default-off bounded hook only with an explicit follow-up decision. Use an established HEVC parser such as `ffprobe` for SPS/profile/range; do not hand-roll HEVC parsing. Never record general gameplay by default.
7. Compare APK dither off/low/standard with identical host settings and stream. Do not combine with host dither. Persist measured noise, banding, color mean, endpoint and performance outcomes; keep off default.

Exit: diagnostics report expected 8-bit endpoint separately from unknown sample precision; optional dither is reproducible and never mislabeled VD behavior.

### Phase 5: Authorized Runtime Matrix and Acceptance

Requires separate explicit permission for APK install, driver deployment, SteamVR restart/settings changes and on-device reproduction. No current permission implied.

Use a fixed synthetic scene/gradient fixture, fixed bitrate/preset/tile width/refresh rate/network, same headset brightness, same application and negotiated dimensions. Disable test-confounding FXAA/CAS/host dither/custom color shaders in the test recipe only. Preserve safety controls and record their state. Exact decoded stream/packet sizes can vary; record actual negotiated values.

For each of the 7 clients with pristine installable inputs:
A. Current settings baseline (record, do not overwrite).
B. Host SDR10 baseline + APK neutral/`srgb8-highp`/off.
C. Same as B + APK low dither.
D. Same as B + APK standard dither.
E. Optional 8-bit encoded control only via verified existing profile negotiation; if vrlink ignores the request, mark control unavailable, never pretend it is 8-bit.

Run fresh session captures, 3 repeated 30-second samples per configuration plus a >=10-minute stability run for any candidate to recommend. Test reconnect, resolution change, both eyes, foveal/peripheral transition, opaque and masked projection families, and session teardown. Do not interpret snapshots spanning multiple sessions as current proof.

Acceptance:
- Encoded HEVC SPS/profile shows Main10/10-bit when available; host negotiation alone is lower-grade evidence.
- Actual decoder-buffer/output evidence is P010 or documented equivalent; capability/config strings alone do not pass decoded-content precision.
- Low-bit test gradients survive to high-precision sample observation before final output quantization, to the extent the device exposes readback. If unavailable, mark precision-preservation unproven and do not claim end-to-end preservation.
- All submitted video projection images for the baseline use `GL_SRGB8_ALPHA8`; unrelated UI/static quad allocations are excluded.
- Neutral shader numeric oracle: expected transfer/matrix output within 1 8-bit RGB code for non-clipped controlled inputs (GPU harness), endpoints correct, monotonic grayscale, no NaN/Inf. Compression tests use measured error distributions, not exact-code equality.
- Dither tests distinguish grain from meaningful reduction in contouring: near-zero mean noise in untapered regions, target absolute mean <0.05 8-bit code across fixed spatial/phase sample set, exact black/white preserved by the existing endpoint gate, documented maximum amplitude, alpha unchanged. Evaluate all 1024 phase values in CPU model and representative actual shader draws. Do not claim pure-white output if a separate calibrated matrix intentionally changes it; endpoint tests act on the pre-dither `q` value.
- Baseline color mean/black floor must not shift materially versus the validated reference; a metadata mismatch blocks recommendation rather than being hidden by gamma/saturation changes.
- No new invalid-param/reset/packet-too-big loop. Respect existing ~2 MiB transport hazard; collect max encoded IDR size and observed actual failures without claiming a universal protocol maximum solely from comments.
- Performance comparison: at 90 Hz frame period is about 11.11 ms. No additional sustained missed-frame trend; predeclare a regression gate of >5% p95 added processing/encode time versus paired control or any reproducible new instability. Separate existing transport faults from changes. Reject or keep experimental a failing option.
- No foveal seams, alpha loss, incorrect FOV, resolution reduction or controller/tracking regression attributable to this work.
- Physical panel precision remains UNVERIFIED; visual comparison is not 1024-level panel proof.

Exit: a dated per-base result table with static/native/APK/device/visual/performance columns, not a single undifferentiated PASS.

### Phase 6: Documentation, Rollback and Cleanup

1. Update this plan checklist with exact completed files, commands, hashes and blocked rows. Link from both project docs. No copied proprietary binaries.
2. Disable `sdr10Baseline` to restore effective host settings; require normal reconnect/restart for encoder profile changes. Restore only owned SteamVR settings through existing journal. Preserve user changes made after activation.
3. Restore prior APK/options from verified backup as needed; never auto-downgrade or install without authorization.
4. Record 2026-09-19 plan and later actual test dates separately; do not backdate runtime results.
5. Classify temporary harness builds/captures/staged artifacts, keep compact results and exact required inputs/tools, then remove only explicit allowlisted disposable outputs. Never remove `build` wholesale. Report cleanup amount and retained exceptions after experiments, not during planning.

## 6. Critical Files: Full Paths

### Steam Link, Expected Updates

- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/diagnostics/steamlink-colour/VD-LIKE-SDR10-IMPLEMENTATION-PLAN.md`: this tracked handoff document.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/patches/src/test/kotlin/app/template/patches/steamlink/binary/VideoOutputPrecisionTest.kt`: 5002363 layout and numeric/structural tests.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/patches/src/main/kotlin/util/OledDecodedCompatibilityAudit.kt`: all 7 hash-pinned actual native fixtures, variant/transition verification.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/patches/build.gradle.kts`: update audit description/arguments if required by expanded fixture handling, not unrelated build changes.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/diagnostics/steamlink-colour/Check-SteamLinkColour.ps1`: goal-specific SDR10-to-sRGB8 derived status and self-tests.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/diagnostics/steamlink-colour/README.md`: exact opt-in recipe, proof levels, authorized capture instructions.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/diagnostics/steamlink-colour/OLED-COMPATIBILITY-NATIVE.md`: per-base evidence expansion and provenance.

### Steam Link, Reuse or Conditional Changes Only

- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/patches/src/main/kotlin/app/template/patches/steamlink/binary/OledCalibrationPatch.kt`: reuse `paddedVideoShader`/`findVideoShader`/`setProjectionSwapchainFormat`/`resolveVideoOutputPrecision`; production edits only for a demonstrated discrepancy.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/patches/src/main/kotlin/app/template/patches/shared/Constants.kt`: exact tuple compatibility, no changes expected.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/patches/src/test/kotlin/app/template/patches/steamlink/PatchCompatibilityMatrixTest.kt`: existing default/dependency regression checks, extend only if options change.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/diagnostics/steamlink-colour/Test-OledDecodedCompatibility.ps1`: fallback helper runner; has a source-extraction boundary to maintain if helper layout changes.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/extensions/resolution-trace-layer/src/android_surface_trigger_passthrough_layer.cpp`: reuse existing projection telemetry; do not alter layer topology for color work.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/WORKSPACE_CLEANUP.md`: final evidence/artifact record when experiments finish.

### Host, Expected Updates

- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/src/Config/Config.h`: `GalaxyXrConfig::sdr10Baseline` false default.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/src/Config/ConfigLoader.cpp`: parse/write/publish field without destructive migrations.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/src/Config/SdrColorPolicy.h`: proposed NEW pure policy resolver, shared by startup/frame processing/tests.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/src/Headsets/GalaxyXR.cpp`: `ApplyHeadsetProfileSetting` and `GalaxyXR_EarlyApplyVrlinkSettings` effective request, no `force10bit` revival.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/src/Driver/FrameProcessor.cpp`: `ProcessSceneLayer`/`ProcessEye` effective baseline and consistent config snapshot; retain `MapLayerFormat`.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/src/Driver/NvencTap.cpp` and `NvencTap.h`: bounded accepted-vs-requested telemetry in `Guarded`/`RegisterResource`/`EncodePicture`/`LockBitstream`; no input-format mutation.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetGUI/src/app/services/JsonFileDefines.ts`: config type.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetGUI/src/app/services/driver-defaults.ts`: GENERATED defaults only.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetGUI/src/app/pages/galaxy-settings/galaxy-settings.base.ts`: existing save/defaults/policy UI logic; no migration clobber.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetGUI/src/app/pages/driver-settings/driver-settings.component.html`: 1 baseline toggle and honest effective state using existing Material UI.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/tests/SdrColorPolicyTest.cpp`: proposed NEW standalone policy test, justified by absence of a color-policy test home.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/tools/Test-SdrColorPolicy.ps1`: proposed NEW runner following existing standalone MSVC test runner; no SteamVR/live config.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/Docs/StreamFrame.md`: paired APK recipe and rollback; link to this authoritative plan instead of duplicating it.

### Host Reuse/Conditional

- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/src/Driver/NvencPostPack.cpp` and `NvencPostPack.h`: bypass in baseline; numeric characterization/future opt-in fix only after follow-up gate.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/DriverFiles/resources/shaders/d3d11/vrlink_layer_ps.hlsl`: actual color/dither shader, not a missing external resource. Avoid production shader edits if neutral constants suffice.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/CustomHeadsetOpenVR/src/Config/SteamVRSettingsJournal.h`: reuse owned-setting rollback.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/tools/Generate-DriverDefaults.ps1`: canonical native->GUI default generator.
- `D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/CustomHeadsetOpenVrGxR/tools/Build-Portable.ps1` and `tools/PORTABLE_BUILD.md`: local staged no-deploy build path.

## 7. Focused Verification Commands

Commands below are for the implementation phase, NOT commands executed while creating this plan. Run each in its stated repository, stop on nonzero exit, retain bounded logs. Tests that write scratch output are expected only after implementation authorization.

Steam Link root:
1. `.\gradlew.bat :patches:test --tests app.template.patches.steamlink.binary.VideoOutputPrecisionTest`
2. `.\gradlew.bat :patches:test --tests app.template.patches.steamlink.PatchCompatibilityMatrixTest`
3. `.\gradlew.bat :patches:auditOledDecodedCompatibility`
4. Fallback only if Gradle/plugin resolution blocks: `.\diagnostics\steamlink-colour\Test-OledDecodedCompatibility.ps1 -JavaHome <verified-JDK21-path>`. This uses a `PatchException` shim and extracted production helpers; it is NOT a real Morphe build or APK proof.
5. `powershell -NoProfile -File .\diagnostics\steamlink-colour\Check-SteamLinkColour.ps1 -Mode SelfTest`
6. `.\gradlew.bat build` required final repository gate; distinguish dependency failure from code failure.
7. Use existing `generatePatchesList` task only if options/catalog metadata changed; inspect configured output channels and preserve tracked catalogs.
8. Real Morphe packaging/signature validation for each available pristine APK follows existing project tooling, then inspect actual `.so` bytes/options. Do not invent nonexistent audit kinds such as `vd-10bit`. Never bypass missing pristine APK evidence with reconstruction.

Host root:
1. `.\tools\Test-SdrColorPolicy.ps1` new runner only after implemented.
2. `.\tools\Test-SteamVRSettingsJournal.ps1` existing standalone tests, no live settings IO.
3. `.\tools\Generate-DriverDefaults.ps1 -Check` after regeneration when field is added.
4. `.\tools\Test-VrlinkCapabilities.ps1` only if touching early profile/capability integration changes its contract; otherwise no reassurance-only ABI gate.
5. In `CustomHeadsetGUI`: `npm run build:ng`; run focused policy/component tests using the repo's Karma/Jasmine configuration rather than inventing React tests.
6. PowerShell 7: `.\tools\Build-Portable.ps1 -DriverOnly -OutputDirectory "$PWD/output/SDR10-Baseline-<unique-run>"`. This stages under output, no deploy. For final paired GUI package omit `-DriverOnly` or use the corresponding GUI build.
7. Alternative with installed Visual Studio: `node build.js --vendor galaxyxr --no-gui` or full command for GUI, after classifying its deletion of existing output staging/default-driver directories. Portable fresh-output command is safer for dirty local work.
8. Do not use the legacy CMake Linux packaging target as the Windows test/build recipe. Do not claim standalone tests are linked into driver; existing test runners compile their own executables.

## 8. Handoff Rules for a Less-Capable Agent

- Work 1 testable slice at a time. First substantive edit must be immediately followed by the narrowest relevant check. No bulk host+APK changes before any validation.
- Start with adding the missing 5002363 test layout and expanding audit input enumeration, or with the pure host policy test. Do not start by altering binary instructions.
- Preserve all existing emitted shader bytes/defaults when opt-in is off. Prove this with golden/current generated comparisons, not prose.
- Stop if any exact native hash/size/caller differs. Never move offsets by a constant delta from a neighboring build.
- Stop if shader prefix cannot fit 1087 bytes or changes suffix/interface assumptions. Do not truncate or relocate arbitrarily.
- Stop if color interpretation cannot be determined. No speculative YUV matrix, sRGB toggling, gamut conversion or VUI relabel.
- No claim that P010 allocation proves meaningful low bits or that highp restores bits already lost.
- No claim that disabled post-pack makes original vrlink metadata correct; that is a baseline to test with the stock decoder/matrix pair.
- No automatic `force10bit` setting, HDR flag, bitrate increase, driver API upgrade, or new protocol to make a failing test pass.
- No updates to live user settings merely for validation. Use temporary fixtures and existing mocks; ask before any device work.
- Never conflate static helper PASS, actual Morphe APK PASS, installed artifact verification, runtime acceptance, visual improvement and physical panel precision.
- If a required fixture/capture permission/tool is unavailable, mark that row BLOCKED with exact prerequisite. Do not silently omit it or promise completion.

## 9. Current Planning Outcome

Research completed and scope aligned; no implementation, build, installation, or live test performed while creating this plan. The recommended design deliberately reuses existing 10-bit request and APK sRGB8 controls, adds a reversible host neutral policy and precise evidence, and avoids unproven VD dithering assumptions. Seek and obey implementation authorization before modifying the host or APK.

## 10. Implementation results — SteamLink side, recorded 2026-09-19

User constraint for this effort: **SteamLink repository only** — no work in `CustomHeadsetOpenVrGxR`, no breaking other patches or the build, testable slices with the narrowest check after each edit. Slice letters follow the implementation handoff; A–D were completed in the prior session, E–F in the 2026-09-19 session. Nothing below is committed; no APK was installed and no device/runtime capture was authorized.

### Completed items

**Slices A–D — test coverage, diagnostics field, build wiring (prior session):**
- `patches/src/test/kotlin/app/template/patches/steamlink/binary/VideoOutputPrecisionTest.kt` — added the missing `2.0.23/5002363` layout (Phase 1 item 4) and a new test proving OLED-patched bytes pass the high-resolution retired-hook guard in every option combination and both mutation orders (Phase 3 item 6).
- `patches/src/main/kotlin/util/OledDecodedCompatibilityAudit.kt` — extended from 3 bases (5001712, 5002322, 5002363) to all 7, with explicit BLOCKED rows for missing decoded inputs (Phase 1 item 4).
- `diagnostics/steamlink-colour/Check-SteamLinkColour.ps1` — goal-specific `Sdr10ToSrgb8` derived field (`EXPECTED_OUTPUT` / `NOT_BASELINE_ENDPOINT` / `UNKNOWN`) plus self-test coverage (Phase 4 item 2).
- `patches/build.gradle.kts` — `auditOledDecodedCompatibility` description updated to all 7 bases; new `auditSdr10ShaderAssemble` JavaExec task (fresh output directory because the audit requires it absent/empty).

**Slice E — complete shader assembly (Phase 3 item 4), 2026-09-19:**
- New `patches/src/main/kotlin/util/Sdr10ShaderAssembleAudit.kt` — assembles the complete opaque/masked programs from the production `paddedVideoShader` prefix (neutral/sRGB8-highp, off/low/standard) plus each base's real native suffixes, located by unique content anchors and C-string boundaries; fail-closed checks on size, NUL boundary, interface, balance and alpha assignment.
- New `diagnostics/steamlink-colour/Test-Sdr10ShaderAssemble.ps1` — cached-Kotlin runner (normal Gradle is blocked resolving `app.morphe.patches:1.3.3`).
- New `diagnostics/steamlink-colour/glsl_validate.py` — fail-closed structural/semantic ESSL 3.00 checker, stdlib-only Python 3; 6 real bugs from the prior untested draft were found and fixed (incl. `pow` legal-form rules, constructor kind returns, `parse_block` consuming its `{`, read-only `in` variables).
- Result: **6 PASS, 1 BLOCKED of 7 bases** (5001740: decoded input missing); 36 assembled `.glsl` files — 1,116 B opaque / 1,383 B masked on every base and dither mode — plus `report.txt` under `build/sdr10-shader-assemble-5337d033135547da8ec01f3ee0d0eac4/`; `glsl_validate.py`: **36 PASS, 0 FAIL**.

**Slice F — documentation (Phase 6 item 1), 2026-09-19:** this section; `diagnostics/steamlink-colour/README.md` and `diagnostics/steamlink-colour/OLED-COMPATIBILITY-NATIVE.md` now link here and record the exact recipe, proof levels, all-base provenance and blocked rows; `WORKSPACE_CLEANUP.md` records the new artifacts and the `build/sdr10-shader-assemble-*` retention policy. No proprietary binaries were copied into any doc.

### Verification evidence (all re-run 2026-09-19)

| Gate | Command (Git Bash) | Result |
|---|---|---|
| Full Kotlin compile + JUnit | `powershell -NoProfile -ExecutionPolicy Bypass -File diagnostics/steamlink-5002363/Compile-CachedAudit.ps1` | 126/126 tests passed |
| OLED decoded audit | `powershell -NoProfile -ExecutionPolicy Bypass -File diagnostics/steamlink-colour/Test-OledDecodedCompatibility.ps1` | 6 PASS + BLOCKED 5001740 |
| Shader assembly | `powershell -NoProfile -ExecutionPolicy Bypass -File diagnostics/steamlink-colour/Test-Sdr10ShaderAssemble.ps1` | 6 PASS, 1 BLOCKED of 7 bases; 36 `.glsl` + report |
| GLSL structural check | `python diagnostics/steamlink-colour/glsl_validate.py <assemble-output-dir>` | 36 PASS, 0 FAIL of 36 files |
| Colour diagnostic self-test | `powershell -NoProfile -ExecutionPolicy Bypass -File diagnostics/steamlink-colour/Check-SteamLinkColour.ps1 -Mode SelfTest` | PASS: 21 offline checks |

These are fallback routes around the blocked Morphe plugin resolution; they are not a real Morphe build, APK packaging or install proof.

### Hashes (re-verified 2026-09-19)

- Stock 1,087-byte fragment, byte-identical on all 6 available bases, NUL-terminated: SHA-256 `cbf2d90eb70b9769dd64e57da5d76dbc38ab7213dcf7b940c956813a1ddaa99a`.
- Opaque suffix, 29 B, identical on all 6: SHA-256 `93158a53e85fde1af61ce449f16c91b3b4213c93101cb98da42e5cc5bdca3f4c`.
- Mask suffix, 296 B, identical on all 6: SHA-256 `2bad22b297f2016866482551483c0ecd44f629ce4d9df1848eb55d6a03008623`.
- Per-base `.so` sizes/hashes as in the Section 3 matrix; all 6 decoded inputs re-verified by the expanded audit in this run.

### Blocked rows (explicit, not skipped)

| Row | Exact prerequisite |
|---|---|
| 2.0.20/5001740 decoded audit + shader assembly | a decoded `lib/arm64-v8a/libvrlink_scene.so` of that exact base (expected 2,220,528 B, SHA-256 `5fbb76c06c9fc0e3e5c5825752aa17e040462c8551b69d3492265f620244f443`); static-analysis-only provenance until then; never a neighbor-derived fixture. |
| GLSL driver compilation | glslangValidator/Vulkan SDK (absent on this machine); the structural checker is not a substitute. |
| Phase 5 runtime matrix | explicit authorization for APK install, driver deployment, SteamVR restart and on-device capture. |

### Not done / not authorized

- Host-side (CustomHeadsetOpenVrGxR) baseline policy, GUI, tests: out of scope per user constraint.
- Committing the uncommitted slices; APK install/deploy; SteamVR restart; device captures.

Project docs: [README.md](README.md), [OLED-COMPATIBILITY-NATIVE.md](OLED-COMPATIBILITY-NATIVE.md).