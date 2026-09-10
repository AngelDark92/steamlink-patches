# Actual video through Android Surface — experiment v1

Created 2026-09-10 for **2.0.20/5001712** and **2.0.22/5002322** only.

Select **Android Surface actual video (experimental)**. It is default-off and belongs to the experimental catalog. The local importable bundle is `build/surface-video-package/steamlink-patches-surface-video-experimental.mpp`.

This carries the **actual rendered streamed video**, including every foveated projection and its alpha, through `XR_KHR_android_surface_swapchain`. It copies Valve's completed OpenGL images into EGL-backed Android Surfaces before releasing the source images. Valve's decoder, AImageReader callbacks, metadata, frame acknowledgements and foveation shaders continue to run. This is a GPU presentation bridge, not a direct MediaCodec-to-Surface decoder bypass.

**Status: native build, host tests, production-helper audits and Morphe decoded-fixture patching pass. Headset playback and performance are unverified.** The earlier fovea-only Surface experiment [failed a user test and was retired](../steamlink-surface-fovea/README.md). This new all-projection experiment has a separate identifier; its implementation does not establish that the earlier failure has been solved.

## Select patches

Start from the original matching APK with compatibility checks enabled. Deselect the automatic **Galaxy XR recommended set** and **Galaxy XR high-resolution 3-projection fix**. The latter appends a 2×2 trigger; this experiment replaces the submitted video swapchains. The 2 modes reject each other in either patch order. Do not select **Appear on top (legacy)** for this experiment.

### Both versions

| Patch | Selection |
|---|---|
| Android Surface actual video (experimental) | Required; start with **8-bit sRGB Surface (first test)** |
| OLED color calibration | Keep your existing calibrated profile; use **8-bit sRGB highp** and dithering **Off** for the initial path comparison |
| Microphone input preset | Optional; **Voice Recognition** matches the normal recommendation |
| Unrestricted battery usage | Optional; opens battery settings as usual |
| Visual Delay Fix | Structurally compatible, including the existing **60 ms** setting. Leave off for a minimal first Surface test, then compare separately; additional Surface latency is unmeasured |
| Other renderer/compositor experiments or controller cadence experiments | Leave off initially; no combined runtime validation |

### 2.0.22 / 5002322

Add **GXR tongue bridge (version 5002322 and above)** if you use it. These individual patches reproduce the normal bundle's components except the replaced high-resolution trigger. Keep Valve's stock startup and identity; no legacy XR foundation or startup patches are needed for this exact build.

### 2.0.20 / 5001712

Retain the older build's individual XR foundation patches from its normal bundle:

- **Android XR native permission names**
- **Force HMD initialization gates**
- **Force lobby permission-state gate**
- **Force stream XR gates**
- **XR Core Runtime**
- **XR Device Config Baseline**
- **XR Input Routing Config**
- **XR Manifest Capability Pack**
- **Startup permission requests (before 5002322)**
- **Startup splash and XR launch mode (before 5002322)**
- **Device identity** — **Meta Quest Pro**, or **Recommended for this build**

Add **GXR face bridge (version 5002318 and below)** if used, plus the common optional patches above. The startup patches intentionally retain their existing permission/splash behavior. This new Surface patch does not add them as hidden dependencies.

## Precision experiment

After 8-bit Surface playback works, compare these settings on a fresh build:

| Setting | FP16 comparison |
|---|---|
| Actual video Surface precision | **FP16 linear scRGB Surface** |
| OLED → Video output precision | **FP16 linear output** |
| OLED → Use 8-bit output when dithering | **Unchecked** |
| OLED → Comparison dithering | **Off** initially |

The 2 selectors control different stages: OLED controls Valve's source projection images; the new selector controls the Android Surface buffers. An FP16 Surface cannot recover precision already lost in an 8-bit source. Both stages require runtime support. Unsupported FP16 source swapchains may prevent streaming before the helper can intervene; unsupported helper EGL/Surface setup keeps the original video path.

RGB10_A2 source images are accepted structurally, but their 2-bit alpha is undesirable for the foveated masks. FP16 preserves substantially finer alpha. The Surface options deliberately use RGBA8888 or RGBA16F rather than RGB10_A2.

**This patch does not enable HDR, 10-bit DSC, physical 10-bit panel scanout, or final display-engine dithering.** FP16 uses linear scRGB metadata for the existing SDR content; it does not relabel SDR as PQ/HLG. Source decoding may still be 8-bit. The proprietary Android XR compositor and the display configuration determine the later stages. No kernel change is required for this experiment and none was made.

## Runtime behavior and limits

- Native helper variants expect exactly 2 projections/4 views on 5001712 and 3 projections/6 views on 5002322. Whole RGBA images are copied; crop rectangles, poses, FOVs, spaces, layer order and alpha flags are preserved.
- The 1st valid complete video frame discovers the source swapchains. It submits the original frame. Surface submission starts only after every replacement has a queued image; no partial eye/layer remapping occurs.
- Sampling occurs after successful `xrWaitSwapchainImage`, before downstream `xrReleaseSwapchainImage`. A wait timeout does not permit reading. A separate shared GLES context preserves Valve's GL state; explicit synchronization completes reads before ownership returns to the runtime.
- The helper writes Surfaces only while the session is visible/focused. Pausing clears readiness. Session/swapchain destruction releases helper EGL, Surface and XR resources.
- Unsupported shapes, unknown extension chains or incomplete frames retain the original submission. A transfer failure disables the experiment for that session. If the runtime rejects a Surface frame, the error is returned and subsequent frames use originals; `xrEndFrame` is never called twice to retry one frame. An application may stop streaming upon that first runtime error.
- The Surface swapchains are producer queues: they are never enumerated, acquired, waited or released with ordinary XR image functions. Their `format`, `sampleCount`, `faceCount`, `arraySize`, and `mipCount` creation fields are zero as required by [Khronos](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/khr/khr_android_surface_swapchain.adoc).
- Full-size extra buffers and blocking GPU synchronization can substantially increase memory use, latency and frame time, especially with FP16. Separate Android queues do not establish atomic stereo/frame latching. Runtime frame alignment, orientation, foveal seams, black levels, reconnect behavior and sustained performance need headset checks.

Log tag: `GXRSurfaceVideo`. `surface_created` reports the native window format; `video_buffer_queued` reports the first 3 successful transfers; `surface_video_frame` reports the first 3 accepted Surface frame submissions. These logs establish submission only, not panel precision. `transfer_failed_passthrough` or `surface_frame_rejected` indicates fallback. The implicit-layer disable variable is `GXR_DISABLE_SURFACE_VIDEO`.

## Static evidence and reproduction

Both source libraries are pinned by file size and SHA-256 in `audit_native.py`. The production installer independently hashes 5 complete native function bodies per base: swapchain creation, streamed-frame rendering, GL draw, acquire/wait, and release. These anchors remain unchanged under the tested OLED modes and Visual Delay Fix. No Valve native bytes are edited by this new patch.

| Exact base | Native size | Creation wrapper | FlipFrame | RenderSpecific | Acquire/wait | Release |
|---|---:|---|---|---|---|---|
| 2.0.20/5001712 | 2,221,072 | `0x13562c` | `0x10ad18` | `0xf1af4` | `0x1385f8` | `0x1386ac` |
| 2.0.22/5002322 | 2,283,400 | `0x13a1c8` | `0x10bf38` | `0xf0d70` | `0x13dafc` | `0x13dbb0` |

Validation:

- 4 arm64 native variants built with NDK `27.2.12479018`, Android API 29, cached OpenXR SDK headers.
- 104 Kotlin/JUnit tests pass, including default-off/exact compatibility, resource validation, conflicting modes in either order and manifest preservation on conflict.
- 2 new C++ host suites pass: image ownership/timeout/release ordering and frame preparation for both projection counts, including all-or-original submission and original metadata preservation. These do not execute an Android EGL driver.
- Production installer: 27 format/transition cases per base, each reapplied; all 9 OLED precision/dither combinations, plus Visual Delay compatibility; original source bytes unchanged.
- Actual Morphe DSL applied to 4 unsigned **reconstructed decoded-fixture APKs**: both formats on both bases. Output contains the expected helper and implicit-layer manifest while original native libraries, DEX and configuration assets remain byte-identical. These fixtures are not pristine release APKs; no signed installable APK or headset result is claimed.
- Final `.mpp` includes JVM classes, Android `classes.dex`, all 4 helper variants and the new manifest. Normal Gradle remains blocked resolving `app.morphe.patches:1.3.3`; the fallback uses the cached Kotlin compiler, Morphe Desktop `1.13.1` dependencies, smali assembler and D8 `9.4.17`.

Run `python diagnostics/steamlink-surface-video/audit_native.py` for the read-only caller trace. It writes hashes, manifest/config comparison data and resolved disassembly below `build/surface-video-audit/`.

Native build (from repository root; adjust tool paths if needed):

```powershell
& build/tooling/cmake/data/bin/cmake.exe -S extensions/resolution-trace-layer -B build/surface-video-native -G Ninja -DCMAKE_MAKE_PROGRAM="$pwd/build/tooling/bin/ninja.exe" -DCMAKE_TOOLCHAIN_FILE="$pwd/.android-sdk/ndk/27.2.12479018/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DOPENXR_SDK_SOURCE_DIR="$pwd/extensions/controller-velocity-layer/build-android/_deps/openxr_headers-src"
& build/tooling/cmake/data/bin/cmake.exe --build build/surface-video-native --target gxr_surface_video_5001712_0 gxr_surface_video_5001712_1 gxr_surface_video_5002322_0 gxr_surface_video_5002322_1
Copy-Item build/surface-video-native/libgxr_surface_video_*.so patches/src/main/resources/steamlink/androidxr/
& diagnostics/steamlink-surface-video/Build-TestPackage.ps1 -JavaHome F:/Runtimes/Java21
```

The test-package script needs the cached dependencies and decoded fixtures under `build/`; its output is a local experiment, not a published release. Host tests are registered in `extensions/resolution-trace-layer/tests/CMakeLists.txt`; they can also be compiled directly with a C++17 host compiler (frame tests need the OpenXR include directory).
