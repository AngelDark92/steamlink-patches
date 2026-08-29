# Galaxy XR permission-free resolution experiment

## Current conclusion

The 2026-08-28 build-5002322 captures prove that single-projection reconstruction improves the permission-free result:

`HIGH without Android XR UI → HIGH while UI is visible → HIGH after UI disappears`

The APK did not request `SYSTEM_ALERT_WINDOW`, AppOps remained `default`, and repeat 1 recorded `HIGH0`. Repeat 2 recorded `HIGH0`, `HIGH`, and `HIGH1`; its OpenXR trace hit the collector size cap before those markers, so the subjective result is valid but frame-level correlation across the palm interval is incomplete.

The v1 implementation is not production-ready. Run 1 reconstructed 7,274 of 7,557 eligible frames and forwarded the original three projections on 283 frames; run 2 reconstructed 359 of 370 captured eligible frames and forwarded 11. Every forwarded frame had zero new source-image releases, while every reconstructed frame had all six. This is repeated-image reuse, not a recorded OpenXR failure. The 1-to-3-to-1 topology switching is the strongest explanation for the visible instability.

Each reconstructed frame also left one `GL_INVALID_ENUM`, later consumed by Steam Link's `CheckGL`. The layer used an invalid indexed sampler-binding query and therefore restored zero-valued sampler state. The v1.1 source fixes that query, reuses the last released private output only when the full/foveal FOV mapping and space still match, records pose changes separately, reports fallback reasons/readiness masks, and samples release-success telemetry to avoid another log-cap failure. The v1.1 build is statically compiled but has not been installed or tested on the headset.

The reported single-projection softness is plausible without implying that one projection is the high-resolution trigger. Reconstruction resolves the original 1536x1536 MSAA2 images into new sampleCount-1 swapchains, linearly resamples the foveal inset, and was capped by the runtime's 3152x3682 maximum. That extra image path can reduce visible detail even if the resulting topology selects a better compositor policy. The two-projection mode removes all of those reconstruction variables.

This result does not prove that projection count alone is Android XR's trigger. Reconstruction simultaneously changes layer count, swapchain identity and dimensions, MSAA resolve, alpha handling, and sampling. Two complementary discriminators are now implemented: `two_projection_drop_base_v1` drops only the compositionally hidden base while forwarding the original remaining images, and `three_projection_sampler_proxy_v1` retains all three projections while replacing only the six submitted MSAA swapchains with 1:1 single-sample proxies. Both have passed static build/provenance tests but have not been installed or run on the headset.

## Available experimental patches

Select exactly one of **Experimental Single Projection Reconstruction**, **Experimental Two Projection Drop Base**, or **Experimental Three Projection Sampler Proxy** in Morphe for Steam Link 2.0.22 build 5002322. Patch generation fails if modes are combined.

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

For the next test, prefer **Experimental Two Projection Drop Base**. Mode `two_projection_drop_base_v1`:

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

## Capture commands

```powershell
$tool = 'C:\Users\Angelo\Desktop\SteamLink-GalaxyXR-Windows-Toolkit-FULL\GalaxyXR-APK\diagnostics\steamlink-resolution-ab'
$common = @{
    Label = 'overlay-off'
    Mode = 'single_projection_reconstruction_v1'
    ExperimentId = 'single-projection-reconstruction-v1'
    SceneId = 'SteamVR Home - fixed viewpoint'
    NetworkProfile = 'same PC, access point, band, and headset position'
}

& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Repeat 1
& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Repeat 2
```

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

During each run it temporarily streams all Android logcat buffers so unknown XR tags are not lost, but archives only size-capped XR/SystemUI/OpenXR/Steam Link/compositor lines. It also samples filtered WindowManager, ActivityManager, and SurfaceFlinger layer state around the palm observation window. Cleanup stops the background collectors and runs `adb kill-server`, including when capture fails.

## Number choices in VR

Repeat 1:

- Resolution: `1` low, `2` high.
- Did Android XR UI appear: `1` no, `2` yes.

Repeat 2:

- Before showing the square: `1` low, `2` high.
- Show the palm square, observe it, let it disappear, then press Enter.
- The script brackets this interval with `XR_OBSERVATION_STARTED` and `XR_OBSERVATION_ENDED`; the following quality answers remain number-only retrospective annotations.
- While visible: `1` low, `2` high.
- After disappearance: `1` low, `2` high.

The script records the visible/after answers as post-event annotations rather than pretending their input times are transition times.

## Acceptance

Success requires two matching cold runs with:

- high resolution without SystemUI in repeat 1;
- high resolution before, during, and after SystemUI in repeat 2;
- no overlay permission and no Steam Link-owned type-2038 window;
- trace build ID `single-projection-reconstruction-v1.1-20260829`;
- sampled frames proving three input projections and six views became one output projection and two views with successful `xrEndFrame`;
- no post-activation fallback to the original three projections; repeated frames must report `sourceUpdate=cached` and `reusedOutput=true` or a precise fail-open reason;
- no reconstruction-correlated `GL_INVALID_ENUM` and no targeted-log size-cap failure;
- no new visual, decoder, OpenXR, or SteamVR regression.

If a trace-proven reconstructed single projection remains UI-dependent, stop APK experiments. The remaining fix boundary is Android XR/SpaceFlinger or Valve's renderer rather than an ordinary permission-free APK.

For `two_projection_drop_base_v1`, require build ID `two-projection-drop-base-v1-20260829`, trace-proven 3-to-2 transforms, matching successful `xrEndFrame` results, and zero disable events. Interpret the result as follows:

- high and sharper than reconstruction: single projection is unnecessary; the redundant-base/three-layer topology selects the low-quality path, while reconstruction caused the remaining softness;
- low while single projection is high: dropping the base is insufficient; either multiple projections/alpha-fovea or another reconstruction property is involved;
- high but below the overlay control: topology explains the low-to-high transition, but the Android XR overlay changes an additional quality state;
- any mixed result, disable event, fallback, or `xrEndFrame` error: inconclusive.

The first live `three_projection_sampler_proxy_v1` capture on 2026-08-29 is not quality evidence. Build v1 learned the correct six-source topology and created six proxies, but armed during the stream-to-spinner transition and disabled on the next zero-layer frame before submitting any proxy transform. The run recorded `LOW0` only after fallback to the original submission. Build `three-projection-sampler-proxy-v1.1-20260829` distinguishes legal zero-layer idle frames, primes and releases all six private buffers before arming, and marks activation only on the first real proxy transform.

For `three_projection_sampler_proxy_v1`, require build ID `three-projection-sampler-proxy-v1.1-20260829`, one process and one OpenXR session, one successful six-buffer prime, six texture-role records, at least one fresh six-image resolve, trace-proven unchanged 3-projection/6-view metadata, matching successful `xrEndFrame`, and zero disable or post-activation passthrough events. High and sharp output shows that a one-projection output is not required and implicates original swapchain identity and/or sample-count/resource allocation. Low output that still changes with SystemUI implicates Android XR's global compositor policy for the three-layer topology. Changed host/encoder/decoder/source facts invalidate either inference. Application texture parameters are not compositor-state proof.

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
