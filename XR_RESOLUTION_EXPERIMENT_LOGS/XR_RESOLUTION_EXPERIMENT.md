# Galaxy XR permission-free resolution experiment

## Evidence boundary

No Galaxy XR, Android XR, Android, or OpenXR documentation requires Steam Link to draw a `2x2` invisible Android overlay, and no documented API says that granting Appear on top raises OpenXR resolution or chooses the correct projection layer.

OpenXR defines deterministic composition-layer ordering. Steam Link's 3 projection layers are legal, and the accepted captures show successful `xrEndFrame` calls. Devices using other runtimes can composite the same topology at adequate quality because OpenXR standardizes submission semantics, not identical sampling, filtering, foveation, or private compositor policy.

On Galaxy XR, the observed quality transition correlates with a display-ready Android/SystemUI surface: permission alone did not improve quality, while a visible type-2038 surface and the SystemUI recording indicator did. The captured public OpenXR view dimensions, 3-projection/6-view topology, host render target, and exposed policy values did not explain that transition. Decoder/transport proof in the retired 5-phase matrix remained incomplete, so vendor-private compositor policy is strongly indicated but not formally proven.

The earlier claim that 3152x3682 single-projection reconstruction matched the sharp overlay control is superseded by the user's headset observation. Reconstruction lost visible density. Quad-view variants and the older reconstruction variants remained soft and are retired. The CPU-optimized native single-projection + 10-bit probe was reinstated because it had not yet received its focused headset diagnosis.

## 2026-09-01 Android-surface v1.0 result

The Android-Surface Trigger capture is a decisive negative result for the tested runtime:

- `XR_KHR_android_surface_swapchain` was not advertised or enabled;
- the layer correctly reported `surface_trigger_unavailable` and failed open;
- no 2x2 Android Surface, buffer queue, terminal quad, or 4-layer submission existed;
- Valve remained at 3 projections, 6 views, and 3 submitted layers with six 1536x1536 sRGB8 sources;
- public view limits remained 1856x2160 recommended and 3152x3682 maximum;
- the host retained the known 3552x3840 render target;
- the palm result was `SOFTER -> MATCHES REFERENCE -> SOFTER`.

That run proved the extension was hidden from enumeration, but the v1.0 helper only appended advertised extensions and therefore never made the decisive enable request. The palm/SystemUI surface still changed quality while the observed OpenXR topology and source format remained fixed. That is direct evidence for a vendor-private SystemUI/display-compositor policy input, although the application cannot name or select that policy through documented OpenXR.

## Android-surface v1.1 forced-capability probe

Current Android XR documentation lists `XR_KHR_android_surface_swapchain`, while also requiring applications to check feature support on the target device. Helper v1.1 closes the remaining ambiguity by requesting the extension even when enumeration hides it. This is intentionally outside the normal advertised-extension contract. If rejected with `XR_ERROR_EXTENSION_NOT_PRESENT`, it immediately retries Valve's original instance create-info and submits the unchanged 3 projections; other create failures remain invalid evidence. If accepted, it loads `xrCreateSwapchainAndroidSurfaceKHR`, queues the 2x2 buffer, and appends the quad. The trace separates request rejection from request acceptance with a missing function. A rejected first 4-layer `xrEndFrame` can fail that one frame; later frames disable the trigger and return to Valve's submission.

This probe is statically built but not yet headset-tested. A pristine APK must be repatched because the helper build ID and hash changed.

## Active experiments

Two mutually exclusive experiments are selectable for Steam Link `2.0.22` build `5002322`. Patch separate pristine APKs; do not stack them with one another or with `Appear on top`.

### Experimental Native Single-Projection Resolution + 10-bit Probe

This is the restored, still-undetermined CPU-optimized native renderer. It hooks only the exact guarded 5002322 `libvrlink_scene.so`, accepts either sRGB8 or RGB10_A2 Valve sources, reconstructs the six source views into a private stereo output, and submits exactly 1 projection with 2 views. It attempts:

1. density-preserving dimensions derived from the source FOV;
2. Galaxy XR panel-native bounds;
3. the runtime-reported maximum as a fallback.

Its collector records the accepted tier and dimensions, source/scratch/output formats, 10/10/10/2 GL attachments, 3/6-to-1/2 topology, successful `xrEndFrame`, decoder hardware-buffer hints when intercepted, host dimensions, and the palm before/during/after result. This is the experiment to run next.

### Experimental Android-Surface Trigger (3-Projection Passthrough)

This retains Valve's original 3 projections and tries to append a 2x2 Android-surface quad. Version 1.1 force-requests the hidden extension once and fails open if the runtime rejects it.

Run the single entry point and select the installed experiment:

```powershell
GalaxyXR-APK\diagnostics\steamlink-resolution-ab\Capture-SteamLinkResolutionRun.ps1
```

## Interpretation

- Native probe sharp before/during/after with successful 3/6-to-1/2 submission: the native single-projection path matches the overlay reference.
- Native probe `soft -> sharp -> soft`: single projection alone does not escape the vendor-private surface policy.
- RGB10_A2 source plus RGB10_A2 scratch/output and 10/10/10/2 attachments: 10-bit is proven through the app/OpenXR output, not through the private compositor or display panel.
- RGB10_A2 absent: Steam Link did not supply a 10-bit source in that run.
- Android-surface `FORCED_EXTENSION_ACCEPTED`: the hidden capability was accepted and the complete 2x2 trigger reached submission.
- Android-surface `UNSUPPORTED_BY_RUNTIME` or `FORCED_EXTENSION_FUNCTION_UNAVAILABLE`: the direct probe closed this OpenXR route on the tested runtime while preserving Valve's original submission.

## Developer rebuild

```powershell
cmake -S extensions\resolution-trace-layer -B extensions\resolution-trace-layer\build-android-new -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/android-ndk-r27d/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 `
  -DOPENXR_SDK_SOURCE_DIR="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/OpenXR-SDK-1.1.61" `
  -DOPENXR_LOADER_LIBRARY="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/steamlink-patches/decoded-apk-android-steamlinkvr-release-base-2.0.22-5002322/lib/arm64-v8a/libopenxr_loader.so"
cmake --build extensions\resolution-trace-layer\build-android-new --target gxr_single_projection_native_probe_v1 gxr_android_surface_trigger_passthrough_v1
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_nspp.so patches\src\main\resources\steamlink\androidxr\
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_ast.so patches\src\main\resources\steamlink\androidxr\
.\gradlew.bat :patches:test :patches:generatePatchesList -PreleaseChannel=experimental
```

Static build/tests do not replace an exact-build headset capture.
