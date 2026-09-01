# Galaxy XR permission-free resolution experiment

## Evidence boundary

No Galaxy XR, Android XR, Android, or OpenXR documentation requires Steam Link to draw a `2x2` invisible Android overlay, and no documented API says that granting Appear on top raises OpenXR resolution or chooses the correct projection layer.

OpenXR defines deterministic composition-layer ordering. Steam Link's 3 projection layers are legal, and the accepted captures show successful `xrEndFrame` calls. Devices using other runtimes can composite the same topology at adequate quality because OpenXR standardizes submission semantics, not identical sampling, filtering, foveation, or private compositor policy.

On Galaxy XR, the observed quality transition correlates with a display-ready Android/SystemUI surface: permission alone did not improve quality, while a visible type-2038 surface and the SystemUI recording indicator did. The captured public OpenXR view dimensions, 3-projection/6-view topology, host render target, and exposed policy values did not explain that transition. Decoder/transport proof in the retired 5-phase matrix remained incomplete, so vendor-private compositor policy is strongly indicated but not formally proven.

The earlier claim that 3152x3682 single-projection reconstruction matched the sharp overlay control is superseded by the user's headset observation. Reconstruction lost visible density. Quad-view/native-reconstruction variants also remained soft. Those patches, their telemetry payloads, and the Appear-on-top permission/surface matrix have been retired.

## Current experiment

The only selectable resolution experiment for Steam Link `2.0.22` build `5002322` is:

**Experimental Android-Surface Trigger (3-Projection Passthrough)**

It deliberately tests a different producer path without reconstructing Valve's image:

- removes `SYSTEM_ALERT_WINDOW` and applies unmanaged Full Space to `VRLink`;
- installs implicit layer `XR_APILAYER_local_GalaxyXR_android_surface_trigger_passthrough_v1` and `libgxr_ast.so`;
- enables `XR_KHR_android_surface_swapchain` only when the runtime advertises it;
- creates a spec-conformant `2x2` sampled Android `Surface` swapchain (`format`, `sampleCount`, `faceCount`, `arraySize`, and `mipCount` are `0`);
- queues RGBA8888 bytes `[R,G,B,A]=[0,0,0,1]` while the session is visible/focused;
- preserves Valve's original 3 projection pointers, 6 views, image rectangles, formats, order, and contents;
- appends 1 terminal `1mm` source-alpha quad in VIEW space;
- performs no GL draw, blit, copy, resampling, decoder interception, or 3-to-1 reconstruction;
- permanently fails open to Valve's untouched frame if any capability, Surface, layer-budget, topology, buffer-post, or submission prerequisite fails.

This is technically distinct from the failed ordinary OpenGL quad because the producer is an Android `Surface` returned by `xrCreateSwapchainAndroidSurfaceKHR`. It is still an OpenXR-owned composition layer, not a WindowManager type-2038 overlay. It may therefore fail to trigger the Galaxy XR/SystemUI path; that negative result would establish that the missing input is outside documented OpenXR layer control.

## Telemetry contract

The layer records bounded metadata only:

- extension advertised/app-enabled/appended/enabled state;
- system `maxLayerCount`;
- view recommended/maximum dimensions;
- ordinary source swapchain dimensions and formats;
- Android-surface creation, VIEW-space creation, and RGBA8888 buffer-post result;
- exact input 3-projection/6-view fingerprint;
- output 4-layer topology with the injected quad last;
- original-pointer preservation, `noCopy=true`, and `noReconstruction=true`;
- matching `xrEndFrame` result and permanent fail-open reason when applicable.

The focused collector also proves that the APK has no overlay permission, allowed AppOp, or Steam Link type-2038 window; captures fresh host-log deltas and SurfaceFlinger/window state; and runs the palm before/covered/after visual test. RGB10_A2 source observation is reported separately. It can prove Valve requested a 10-bit source swapchain, but it cannot prove private-compositor or panel precision.

There is no longer a test selector. Run only:

```powershell
GalaxyXR-APK\diagnostics\steamlink-resolution-ab\Capture-SteamLinkResolutionRun.ps1
```

## Interpretation

- Sharp before/during/after, matching the known 3-projection + overlay reference, with a fully validated surface-trigger frame: the OpenXR Android-surface producer is sufficient and can replace Appear on top for this exact build.
- Soft before/during/after with a fully validated surface-trigger frame: an OpenXR Android-surface layer is insufficient; the remaining cause is likely WindowManager/SystemUI/private compositor policy unavailable through documented OpenXR.
- A palm-triggered change: the trigger did not stabilize the high-quality path.
- Missing extension/surface/queue/topology/submission evidence: patch activation is inconclusive, not a quality result.
- RGB10_A2 absent: 10-bit is not proven, independently of resolution.

## Developer rebuild

```powershell
cmake -S extensions\resolution-trace-layer -B extensions\resolution-trace-layer\build-android-new -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/android-ndk-r27d/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 `
  -DOPENXR_SDK_SOURCE_DIR="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/OpenXR-SDK-1.1.61"
cmake --build extensions\resolution-trace-layer\build-android-new --target gxr_android_surface_trigger_passthrough_v1
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_ast.so patches\src\main\resources\steamlink\androidxr\
.\gradlew.bat :patches:test :patches:generatePatchesList -PreleaseChannel=experimental
```

Static build/tests do not replace an exact-build headset capture.
