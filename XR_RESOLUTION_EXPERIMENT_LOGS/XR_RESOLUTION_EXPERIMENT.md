# Galaxy XR permission-free resolution experiment

## Current conclusion

The valid 2026-08-29 build-5002322 captures now isolate the projection-topology boundary.

`HIGH without Android XR UI → HIGH while UI is visible → HIGH after UI disappears`

Single-projection reconstruction v1.2 produced 5772 successful 3-to-1 transformations and stayed `HIGH0 -> HIGH -> HIGH1` without `SYSTEM_ALERT_WINDOW`. On 2026-08-30, the user corrected the earlier subjective softness report after further comparison: no remaining softness is visible, and reconstruction now appears equal to the normal APK with the granted appear-on-top control. Its six 1536x1536 MSAA2 sources produced a calculated 3745x4048 detail-preserving request that the runtime capped at 3152x3682. That size cap remains measured telemetry, but it must not be cited as proof of a visible quality loss. The captured runtime reported `qualityExtensionAdvertised=false`, so `XR_FB_composition_layer_settings` could not legally be attached.

Two-projection drop-base v1.1 produced 3967 successful 3-to-2 transformations and stayed `LOW0 -> HIGH -> LOW1`. It forwarded the original opaque underside and alpha-foveal projections unchanged, with no private swapchain, resampling, disable, or failed `xrEndFrame`. The redundant base and an exact count of three projections are therefore ruled out. The remaining classifier is more than one projection, or the broader alpha-foveated multilayer topology.

The valid three-projection sampler-proxy v1.2 run independently produced 4811 successful unchanged 3-projection/6-view submissions and stayed `LOW0 -> HIGH -> LOW1`. Replacing all six original MSAA2 swapchains with 1:1 single-sample proxies did not escape the low path, ruling out original swapchain identity and MSAA as sufficient triggers.

Single-projection fovea-quads v1.0 produced 5042 successful 3-projection/6-view to 1-projection/2-quad transformations and stayed `LOW0 -> LOW -> LOW1`. It was visibly worse than the original two- and three-projection low path, which is sufficient to retire it. The same capture recorded 284 compositor buffer-acquisition failures and 14 latch failures, but it also reached Android XR thermal status 3, activated `thermalSevere` and `compositorAnyJank`, and used the old high-frequency `dumpsys` sampler. Those errors are confounded scheduling evidence and are not attributed to the quad topology without an untraced control.

DynamicPolicyManager logs exposed `PanelSuperSampling=1`, `RecommendedResolution=1`, `openxr.currentApp.FRS=1`, and `openxr.sysUi.FRS=1` throughout the palm cycle. SystemUI show/hide events coincide with the visual switch, but those app-visible values do not change. The switch is therefore below the exposed policy surface, in the Galaxy XR compositor/runtime implementation.

The appear-on-top patch does not directly set a Steam Link resolution or OpenXR quality option. When `Settings.canDrawOverlays` succeeds, its Java bridge attaches a real 2x2, nearly transparent `TYPE_APPLICATION_OVERLAY` window named `SteamLinkOverlay` before VR launch and on resume. The available evidence therefore supports a compositor surface trigger rather than a Steam Link renderer branch, but AppOp grant and attached-surface state were previously conflated. The corrected collector now records AppOp, Steam Link-owned type-2038 window existence, and WindowManager visible/display-ready surface state separately. SurfaceFlinger presentation still requires trace evidence.

Public OpenXR/Android XR APIs provide no control that forces the SystemUI-selected internal quality state. `XR_ANDROID_recommended_resolution` is a change notification followed by re-enumeration, not an application setter, and composition-layer supersampling is an optional compositor hint that is unavailable when the extension is not advertised. Multiple projections remain legal and every captured `xrEndFrame` succeeded, so this is not a core OpenXR projection limit.

## Plain-language explanation

Steam Link submits 3 stereo OpenXR projection layers: full-view imagery plus a higher-detail foveal inset. On Galaxy XR, those separate layers enter a low-resolution compositor path unless an Android XR/SystemUI or granted appear-on-top overlay is present. The experimental OpenXR API layer intercepts `xrEndFrame`, composites the same 3 submitted layers into 1 ordinary stereo projection, and submits that equivalent final image to Android XR. Galaxy XR then selects its high-resolution path without the overlay permission. "Single-projection reconstruction" means reconstructing the final OpenXR layer composition; it does not reconstruct video frames, change Steam Link streaming, or invent image detail. The corrected user observation is that its visible quality now matches the normal APK with appear-on-top granted.

## Available experimental patches

Select exactly one remaining projection experiment in Morphe for Steam Link 2.0.22 build 5002322. The fovea-quad patch has been removed after its failed headset run. No replacement topology experiment is proposed from this capture.

Do not select **Appear on top** or any retired resolution experiment.

The patch:

- removes `SYSTEM_ALERT_WINDOW`;
- adds unmanaged Full Space directly to `VRLink`;
- installs mode `single_projection_reconstruction_v1`;
- recognizes only the exact six-swapchain Steam Link streaming topology;
- temporarily retains recognized source images, resolves the multisampled opaque underside and alpha-foveated images, and composites them into two private full-density eye swapchains. The earlier base projection is not sampled because the later same-pose full-FOV underside is opaque and fully covers it under OpenXR ordering;
- replaces the three source projections with one opaque stereo projection;
- reuses the last released private output on repeated-image frames only while the source handles, projection space, and full/foveal FOV mapping remain compatible;
- forwards the original frame after safely releasing every held image when any topology, EGL, GL, or synchronization prerequisite is missing.

The completed two-projection discriminator, mode `two_projection_drop_base_v1`:

- recognizes the same exact three-projection, six-swapchain 5002322 topology;
- removes only projection 0 after proving it has the same pose/FOV as the later opaque full-FOV underside;
- forwards the original underside and alpha-foveated projection structs, swapchains, rectangles, poses, FOVs, flags, and order unchanged;
- creates no private swapchains and performs no GL resolve, resampling, alpha flattening, or source-image lifetime interception;
- permanently fails open for the session if the layout changes after activation, preventing mixed 2/3-projection submission.

Mode `three_projection_sampler_proxy_v1` answers whether three projections can remain high quality when their resource path changes:

- preserves the exact 3-projection/6-view layer count, order, poses, FOVs, spaces, flags, rectangles, and projection metadata;
- resolves six distinct 1536x1536 SRGB8_ALPHA8 MSAA2 sources 1:1 with `GL_NEAREST` into six private 1536x1536 sampleCount-1 swapchains;
- applies explicit linear/clamp application texture-object state and records it without claiming it crosses into the compositor process;
- reuses released proxy contents only on zero-update frames and rejects partial updates, source-identity changes, unsafe topology, GL errors, and post-activation passthrough.

Retired mode `single_projection_fovea_quads_v1` established that spatial quads are not a usable replacement for the original foveal projection:

- forwards one original opaque projection unchanged;
- converts the two original per-eye alpha-foveal images to eye-isolated far-plane quads;
- preserves source handles, rectangles, array indices, alpha flags, and FOV-derived geometry;
- performs no allocation, GL operation, resolve, or resampling.
- nevertheless remained low before, during, and after the SystemUI element and triggered repeated compositor buffer/latch failures.

## Capture commands

Do not repeat the retired fovea-quad mode. The collector retains its parser only to validate already archived captures. Use Repeat 2 only for a specifically selected remaining control or a future experiment with a concrete sampling model.

The script discovers the installed APK hashes. No pre-known Morphe APK SHA-256 is required.

For the two-projection discriminator, change the common fields to:

```powershell
Mode = 'two_projection_drop_base_v1'
ExperimentId = 'two-projection-drop-base-v1'
```

For the three-projection resource discriminator, use:

```powershell
Mode = 'three_projection_sampler_proxy_v1'
ExperimentId = 'three-projection-sampler-proxy-v1'
```

During each run it temporarily streams all Android logcat buffers so unknown XR tags are not lost, but archives only size-capped XR/SystemUI/OpenXR/Steam Link/compositor lines. Expensive WindowManager, ActivityManager, and SurfaceFlinger snapshots now run only at three phase boundaries; the high-frequency background `dumpsys` sampler was removed. Passive logs retain all DynamicPolicy capability values, context samples, GNAV transitions, policy activation/deactivation, and timestamped buffer/latch/HWC failures. Cleanup stops the logcat collector and runs `adb kill-server`, including when capture fails.

For deeper Android XR scheduling evidence, run `GalaxyXR-APK\diagnostics\steamlink-resolution-ab\Capture-AndroidXrCompositorTrace.ps1` separately. Its bounded Perfetto capture is diagnostic-only because tracing adds overhead; pair the palm-cycle trace with a no-palm trace and never use either as the visual acceptance run.

## Number choices in VR

Repeat 2:

- Before showing the square: `1` low, `2` high.
- Show the palm square, observe it, let it disappear, then press Enter.
- The script brackets this interval with `XR_OBSERVATION_STARTED` and `XR_OBSERVATION_ENDED`; the following quality answers remain number-only retrospective annotations.
- While visible: `1` low, `2` high.
- After disappearance: `1` low, `2` high.

The script records the visible/after answers as post-event annotations rather than pretending their input times are transition times.

## Acceptance

Success requires one provenance-valid Repeat 2 run with:

- high resolution before, during, and after SystemUI in repeat 2;
- no overlay permission and no Steam Link-owned type-2038 window;
- trace build ID `single-projection-reconstruction-v1.2-20260829`;
- sampled frames proving three input projections and six views became one output projection and two views with successful `xrEndFrame`;
- no post-activation fallback to the original three projections; repeated frames must report `sourceUpdate=cached` and `reusedOutput=true` or a precise fail-open reason;
- no reconstruction-correlated `GL_INVALID_ENUM` and no targeted-log size-cap failure;
- no new visual, decoder, OpenXR, or SteamVR regression.

If a trace-proven reconstructed single projection remains UI-dependent, stop APK experiments. The remaining fix boundary is Android XR/SpaceFlinger or Valve's renderer rather than an ordinary permission-free APK.

For `two_projection_drop_base_v1`, require build ID `two-projection-drop-base-v1.1-20260829`, one OpenXR session, successful trace-proven 3-to-2 transforms before and during the device-clock-bounded observation, matching successful `xrEndFrame` results, and zero disable or failed-end-frame events through observation. Auxiliary spinner/UI frames are archived but do not disable or validate the transform. Interpret the result as follows:

- high and sharper than reconstruction in the same controlled comparison: single projection may be unnecessary and the redundant-base/three-layer topology may select the low-quality path; this hypothetical outcome was not observed in the accepted runs;
- low while single projection is high: dropping the base is insufficient; either multiple projections/alpha-fovea or another reconstruction property is involved;
- high but below the overlay control: topology explains the low-to-high transition, but the Android XR overlay changes an additional quality state;
- any mixed result, disable event, fallback, or `xrEndFrame` error: inconclusive.

The first two live `three_projection_sampler_proxy_v1` captures on 2026-08-29 are not quality evidence. v1 disabled on a zero-layer transition. v1.1 successfully created, configured, and primed all six proxies, but its one-shot setup consumed the first streamed frame budget; seven milliseconds later Steam Link entered its non-target spinner layer and the layer disabled before any transform. Both runs recorded `LOW0` only after original-submission fallback. Build `three-projection-sampler-proxy-v1.2-20260829` learns without allocating, stages one proxy after each of six later target frames, preserves readiness across unrelated spinner/UI layers, and marks activation only after a successful rewritten `xrEndFrame`.

For `three_projection_sampler_proxy_v1`, require build ID `three-projection-sampler-proxy-v1.2-20260829`, one process/session/fingerprint, six unique staged primes, one ready event, six texture-role records, and a fresh successful transformed frame before visual observation begins. The device-clock-bounded observation must contain successful unchanged 3-projection/6-view submissions and no failed `xrEndFrame`, disable, source reset, source-referencing auxiliary, or post-activation target-passthrough event; teardown records are archived but excluded from quality provenance. High and sharp output shows that a one-projection output is not required and implicates original swapchain identity and/or sample-count/resource allocation. Low output that still changes with SystemUI implicates Android XR's global compositor policy for the three-layer topology. Changed host/encoder/decoder/source facts invalidate either inference. Application texture parameters are not compositor-state proof.

## Retired history

See `SteamLink-GalaxyXR-Python-Patches-Already-Tried-for_Resolution_issue/README.md`. Saved capture archives are evidence and must not be deleted.

## Developer rebuild

```powershell
cmake -S extensions\resolution-trace-layer -B extensions\resolution-trace-layer\build-android-new -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/android-ndk-r27d/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 `
  -DOPENXR_SDK_SOURCE_DIR="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/OpenXR-SDK-1.1.61"
cmake --build extensions\resolution-trace-layer\build-android-new
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_single_projection_reconstruction_v1.so patches\src\main\resources\steamlink\androidxr\
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_two_projection_drop_base_v1.so patches\src\main\resources\steamlink\androidxr\
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_three_projection_sampler_proxy_v1.so patches\src\main\resources\steamlink\androidxr\
.\gradlew.bat :patches:generatePatchesList -PreleaseChannel=all
```
