# Resolution helpers (2026-09-03)

## Experimental Surface-backed underside: 2.0.22/5002322 only

In the existing **Galaxy XR high-resolution 3-projection fix** patch, set
**Android Surface placement** to **Underside projection (experimental, 5002322)**.
Keep the other patch selections/options the same for comparison. The default remains
**Terminal quad (tested)**. Every other exact build ignores the experimental selection
and keeps its current helper. In particular, 2.0.20/5001712 remains 2 projections plus
its existing quad, and its packaged native helper is byte-for-byte unchanged.

`libgxr_ast_underside.so`, build `android-surface-underside-5002322-v1.0-20260903`,
replaces only the submitted underside at index 0 with a Surface-backed projection.
Both eye subimages reference 1 static 2x2 opaque-black buffer. It preserves the
underside's space, flags, poses and FOVs and the original base/foveal layer pointers
at indices 1/2. Each eligible frame remains **3 projections / 6 views / 0 quads**.
There is no video copy, conversion or extra draw. Valve still creates and cycles its
original underside swapchains: this experiment does not remove that allocation/work.

Native analysis identifies index 0 as `m_undersideLayerSwapchains`, index 1 as
`m_baseLayerSwapchains` and index 2 as `m_foveatedLayerSwapchains`. The added underside
has no video draw in the examined frame path. Its original pixel content and Valve's
reason for adding it are unknown; opaque black is an explicit experimental substitute.
Older experiment notes used inverted descriptive names for the first 2 layers.

Selection requires exact 2.0.22/5002322, the pinned 2,283,400-byte ARM64 layout and
SHA-256 checks of native `Init`, `GetProjectionLayers` and `FlipFrame`. These do not
reject the existing recommended OLED/audio/pose edits elsewhere in the library.
At runtime, the underside's native single `XrCompositionLayerSettingsFB` record
(null next, zero flags) is preserved. Unexpected layer counts/types/flags, other
underside chains or nonnull view chains pass through unchanged. An `xrEndFrame`
rejection disables replacement for that session;
it does not retry the same frame or silently switch to the terminal quad.

The experiment has its own manifest/mode identity and cannot be stacked with the
tested helper. Repatch a clean base to change modes. Roll back by selecting
**Terminal quad (tested)** on the same clean base. No overlay permission is added.

Local validation: Android NDK build, 4 host CTest targets, Gradle tests/build/catalogs,
and decoded-fixture recommendation audits for selected 5002322 and ignored 5001712.
These are static/mock results; no pristine-source install or headset/GPU result exists.

For a headset comparison, keep the base, remaining patch options, host quality,
refresh rate and scene identical. Check startup, stream restart, palm/SystemUI
show/hide and focus loss/resume. Verify cold logs show this exact build/mode,
`surface_buffer_queued` with `[0,0,0,255]`, and `surface_underside_submission` with
`outputLayerCount=3`, `triggerQuadCount=0`, `replacedLayerIndex=0`,
`baseFoveaPointersPreserved=true` and `result=0`. Only the first 3 accepted frames
are logged; steady-state frames do not format logs or repost the buffer. Successful
submission alone does not prove high resolution or faster GPU composition.

Build just this payload with the same configured Android build directory:

```powershell
cmake --build extensions/resolution-trace-layer/build-android-cpu --target gxr_android_surface_underside_5002322_v1
```

Copy only `libgxr_ast_underside.so` into `patches/src/main/resources/steamlink/androidxr/`
and update its hash in `SurfaceUndersideTest.kt` after rebuilding. The 2 tested
helper artifacts and their existing hashes stay unchanged. Integration audits:

```powershell
.\gradlew.bat :patches:auditDecodedSteamLinkPatches -PdecodedAuditKind=underside -PdecodedAuditIndex=0 -PreleaseChannel=experimental
.\gradlew.bat :patches:auditDecodedSteamLinkPatches -PdecodedAuditKind=underside -PdecodedAuditIndex=1 -PreleaseChannel=experimental
```

## Tested terminal-quad helper CPU update

The existing high-resolution patch installs this shared API layer. Recommendation
bundles already depend on that patch; they do not contain separate copies of its
implementation and do not need membership changes. Repatch the APK using the updated
`.mpp` to receive the new native library; existing installed APKs do not update themselves.

## Behavior and scope

- `libgxr_ast.so`: helper v1.4 for exact Steam Link 2.0.22 builds 5002244, 5002296,
  5002313, 5002318 and 5002322 (3 native projections plus 1 quad).
- `libgxr_ast_5001712.so`: helper v1.2 for exact Steam Link 2.0.20/5001712
  (2 native projections plus 1 quad), installed under the same `libgxr_ast.so` name.
- Other builds retain their existing guards. This update does not add compatibility.
- Frame lookup uses a generation-validated non-owning thread-local cache. Its hits
  avoid both the map mutex and shared-pointer ownership RMWs. Session creation,
  destruction and instance cleanup invalidate caches. The registry outlives caches;
  OpenXR's external destruction synchronization protects the borrowed state during
  a frame. The generation check is not a concurrent memory reclamation algorithm.
- Event processing retains shared ownership and reads event bytes only on
  `XR_SUCCESS`, never on `XR_EVENT_UNAVAILABLE` or errors.
- The quad remains submitted on every eligible frame. Buffer content, size, alpha,
  projection pointers/order, source formats, and GPU behavior are unchanged.

No speedup or new headset validation is claimed from local tests. In particular, a
CPU change does not establish lower runtime GPU composition cost or improved 10-bit output.

Validation for this revision: both Android builds, all 83 Gradle tests, all 3 CTest
targets (also repeated 20 times each), all 6 decoded-base high-resolution audits,
and all 4 decoded-base recommendation-bundle audits passed. The generated
`patches-1.12.1-dev.1.mpp` contains both rebuilt helpers byte-for-byte. Decoded-base
audits are offline patching evidence, not pristine-source APK or headset validation.

## Local build and tests

Run from the repository root. This checkout currently has NDK r27c and cached OpenXR
1.1.43 headers (Android-surface extension revision 4). The obsolete sibling
`GalaxyXR-APK` tool paths are not required. Supply your own equivalent paths if needed.

```powershell
$gxrRoot = (Get-Location).Path
$gxrSdk = Join-Path $gxrRoot 'extensions/controller-velocity-layer/build-android/_deps/openxr_headers-src'
$gxrNdk = Join-Path $gxrRoot '.android-sdk/ndk/27.2.12479018'

# Windows host: native lifecycle tests and the actual 2-/3-projection layer with
# mock Android/OpenXR calls. CTest makes no device connections.
cmake -S extensions/resolution-trace-layer/tests -B extensions/resolution-trace-layer/build-host-tests "-DOPENXR_SDK_SOURCE_DIR=$gxrSdk"
cmake --build extensions/resolution-trace-layer/build-host-tests --config Release
ctest --test-dir extensions/resolution-trace-layer/build-host-tests -C Release --output-on-failure

# Android helpers: use Ninja from PATH, or pass -DCMAKE_MAKE_PROGRAM=<ninja.exe>.
cmake -S extensions/resolution-trace-layer -B extensions/resolution-trace-layer/build-android-cpu -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$gxrNdk/build/cmake/android.toolchain.cmake" "-DOPENXR_SDK_SOURCE_DIR=$gxrSdk" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
cmake --build extensions/resolution-trace-layer/build-android-cpu
```

After both builds succeed, copy both generated `.so` files to
`patches/src/main/resources/steamlink/androidxr/`. Update the build IDs and pinned
SHA-256 hashes in `OptionalXrPatches.kt` and `AndroidSurfaceTriggerResourceTest.kt`
when publishing a different payload. Gradle does not rebuild these C++ libraries.

```powershell
.\gradlew.bat :patches:test :patches:buildAndroid -PreleaseChannel=experimental
# If the 6 existing decoded-fixture APKs are available:
foreach ($gxrIndex in 0..5) {
    .\gradlew.bat :patches:auditDecodedSteamLinkPatches -PdecodedAuditKind=high-resolution "-PdecodedAuditIndex=$gxrIndex"
    if ($LASTEXITCODE -ne 0) { throw 'High-resolution audit failed' }
}
foreach ($gxrIndex in 0..3) {
    .\gradlew.bat :patches:auditDecodedSteamLinkPatches -PdecodedAuditKind=recommended "-PdecodedAuditIndex=$gxrIndex"
    if ($LASTEXITCODE -ne 0) { throw 'Recommendation-bundle audit failed' }
}
```

The host tests exercise negative-cache invalidation, handle reuse, cleanup, owned
event readers, and persistent render threads taking turns. The integration tests
include the production C++ file for both projection counts, exercise 1000 no-event
polls with stale data and 1000 steady frames, and verify continuous quad submission,
1-time buffer posting, no steady logging, unchanged source pointers, and fail-open
behavior after runtime rejection. They do not simulate the vendor compositor.

## Headset A/B and rollback

Use the same Steam Link base, patch selections, host quality, scene and refresh rate
for the previous and updated payloads. Check cold launch, stop/start streaming,
palm show/hide, DFR-UI attach/detach, and focus loss/resume. Sharpness must remain
stable throughout; record crashes, flicker, or resolution transitions explicitly.

If collecting timing, separate helper CPU from runtime `xrEndFrame`/GPU time and
keep overlay/recording state identical. Smaller timings alone do not prove preserved
resolution. These binaries have not been installed or tested on a headset by this build.

To revert, rebuild from the previous source revision or use the previous `.mpp`
and repatch the same clean APK with the same selections. Do not stack old and new
native helpers or change recommendation bundles to roll back this CPU-only update.
