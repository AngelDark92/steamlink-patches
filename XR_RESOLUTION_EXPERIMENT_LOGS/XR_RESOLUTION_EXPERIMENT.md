# Galaxy XR permission-free resolution experiment

## Current conclusion

The valid 2026-08-29 build-5002322 captures now isolate the projection-topology boundary.

`HIGH without Android XR UI → HIGH while UI is visible → HIGH after UI disappears`

Single-projection reconstruction v1.2 produced 5772 successful 3-to-1 transformations and stayed `HIGH0 -> HIGH -> HIGH1` without `SYSTEM_ALERT_WINDOW`. The earlier same-day judgment that it was equal to the granted-overlay control is superseded: the user now reports that the 3152x3682 reconstruction is plainly lower resolution on-headset. The exact-build native hook showed the same lower resolution and pixel-crawl appearance, ruling out implicit API-layer registration as the cause. The six 1536x1536 MSAA2 sources require about 3745x4048 to preserve foveal density, but the runtime advertised only 3152x3682; the foveal inset therefore mapped to about 1293x1397 instead of 1536x1536. This is real precomposition density loss. The physical Galaxy XR panels are 3552x3840 per eye, distinct from both the OpenXR maximum and the density-preserving request. The surviving Probe attempts density-preserving output, then panel-native output, then the advertised maximum, with atomic allocation fallback and permanent next-frame downgrade after a recoverable submission rejection. It also re-enumerates after `XR_ANDROID_recommended_resolution` events. No headset result exists yet for the new tiers.

Two-projection drop-base v1.1 produced 3967 successful 3-to-2 transformations and stayed `LOW0 -> HIGH -> LOW1`. It forwarded the original opaque underside and alpha-foveal projections unchanged, with no private swapchain, resampling, disable, or failed `xrEndFrame`. The redundant base and an exact count of three projections are therefore ruled out. The remaining classifier is more than one projection, or the broader alpha-foveated multilayer topology.

The valid three-projection sampler-proxy v1.2 run independently produced 4811 successful unchanged 3-projection/6-view submissions and stayed `LOW0 -> HIGH -> LOW1`. Replacing all six original MSAA2 swapchains with 1:1 single-sample proxies did not escape the low path, ruling out original swapchain identity and MSAA as sufficient triggers.

Single-projection fovea-quads v1.0 produced 5042 successful 3-projection/6-view to 1-projection/2-quad transformations and stayed `LOW0 -> LOW -> LOW1`. It was visibly worse than the original two- and three-projection low path, which is sufficient to retire it. The same capture recorded 284 compositor buffer-acquisition failures and 14 latch failures, but it also reached Android XR thermal status 3, activated `thermalSevere` and `compositorAnyJank`, and used the old high-frequency `dumpsys` sampler. Those errors are confounded scheduling evidence and are not attributed to the quad topology without an untraced control.

Both later native 4-view projection candidates also remained visibly low-resolution, matching the unpatched/no-overlay path. They are retired: preserving Valve's source images and removing the reconstruction draw did not make Galaxy XR select the high-resolution stereo compositor path. Their selectable patches, helper binaries, and shared source have been removed; their mode and library identities remain reserved only for rejecting stale decoded-APK contents.

DynamicPolicyManager logs exposed `PanelSuperSampling=1`, `RecommendedResolution=1`, `openxr.currentApp.FRS=1`, and `openxr.sysUi.FRS=1` throughout the palm cycle. SystemUI show/hide events coincide with the visual switch, but those app-visible values do not change. The switch is therefore below the exposed policy surface, in the Galaxy XR compositor/runtime implementation.

The appear-on-top patch does not directly set a Steam Link resolution or OpenXR quality option. When `Settings.canDrawOverlays` succeeds, its Java bridge attaches a real 2x2, alpha-1/255 `TYPE_APPLICATION_OVERLAY` window named `SteamLinkOverlay` before VR launch and on resume. The permission check exists because Android rejects an application-owned type-2038 window without `SYSTEM_ALERT_WINDOW`; it is not a resolution condition. The 2x2 size and near-transparency are choices made by this project to create a small empirical test surface; they have not been proven minimal or necessary and are not requirements from Valve, Samsung, Android XR, Android, or OpenXR. The earlier experiments also conflated the AppOp grant with an attached surface, so the corrected collector records AppOp, Steam Link-owned type-2038 window existence, and WindowManager visible/display-ready surface state separately. SurfaceFlinger presentation still requires trace evidence.

Public OpenXR/Android XR APIs provide no control that forces the SystemUI-selected internal quality state. `XR_ANDROID_recommended_resolution` is a change notification followed by re-enumeration, not an application setter, and composition-layer supersampling is an optional compositor hint that is unavailable when the extension is not advertised. Multiple projections remain legal and every captured `xrEndFrame` succeeded, so this is not a core OpenXR projection limit.

## Documentation boundary and competing explanations

No public Galaxy XR, Android XR, Android, or OpenXR document says that an OpenXR application must draw a 2x2 invisible Android overlay, that granting "appear on top" raises OpenXR resolution, or that an Android/SystemUI element selects the correct OpenXR projection layer.

- Android [`Settings.canDrawOverlays`](https://developer.android.com/reference/android/provider/Settings#canDrawOverlays(android.content.Context)) only reports whether the application may draw these windows. It neither creates a window nor specifies any XR side effect.
- Android [`TYPE_APPLICATION_OVERLAY`](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#TYPE_APPLICATION_OVERLAY) specifies permission, Z-order, process-importance, and system resource-management behavior. It requires `SYSTEM_ALERT_WINDOW`, sits above ordinary activity windows but below critical system windows, and may be repositioned, resized, or hidden by the system. It contains no XR resolution or image-quality contract.
- Android XR documents OpenXR applications as running in [unmanaged Full Space](https://developer.android.com/develop/xr/openxr/get-started). It does not document an Android overlay workaround.
- OpenXR makes the application supply the ordered composition-layer array to `xrEndFrame`. The runtime composites those layers in [submission order using a painter's algorithm](https://registry.khronos.org/OpenXR/specs/1.1-khr/html/xrspec.html#compositing), subject to its advertised [`maxLayerCount`](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFrameEndInfo.html). It does not inspect `canDrawOverlays` to choose a "right" Steam Link projection. Later submitted layers are already the defined layers on top.
- Android XR's [`XR_ANDROID_recommended_resolution`](https://developer.android.com/develop/xr/openxr/extensions/XR_ANDROID_recommended_resolution) can notify an application that the runtime's recommendation changed because of performance, thermal, or other factors. It requires re-enumeration; it is not a setter and cannot force a higher maximum.

The observed overlay/SystemUI correlation is therefore real headset evidence, but the explanation that Galaxy XR is "selecting the right layer" is not supported by the APIs and conflicts with deterministic OpenXR layer ordering. The 2x2 Android surface contains no Steam Link eye image and is outside the OpenXR layer array. The recording indicator producing the same improvement, even when it does not visibly dim Steam Link, is particularly strong evidence that the exact Steam Link-owned 2x2 window is not the requirement. It instead points to a Galaxy XR/Android XR private compositor or window-policy transition caused by the presence of another Android/SystemUI surface.

The remaining explanations, in current likelihood order, are:

1. Galaxy XR switches allocation, upscaling, foveation, filtering, or other compositor quality policy when a second Android/SystemUI surface participates in composition. The exposed `PanelSuperSampling`, `RecommendedResolution`, app FRS, and SystemUI FRS values staying constant means that any such switch is below the public policy surface.
2. The extra surface exposes or avoids a Galaxy XR OpenXR runtime bug specific to multiple projection layers or alpha-foveated composition in an otherwise pure unmanaged-Full-Space frame.
3. A focus/session-state or scheduling transition indirectly changes runtime behavior. The recording-dot result without visible dimming weakens a pure focus-loss explanation but does not eliminate an unreported internal state change. Android also documents that an application-overlay process receives adjusted process importance.
4. A dynamic view recommendation, SteamVR target/encode decision, decoder-buffer allocation, or SurfaceFlinger/HWC path changes at the same time. These alternatives remain possible until the paired logs prove that those inputs stayed identical across the visual transition.

Multiple projection layers working on devices that do not run Android XR is expected, not contradictory. OpenXR explicitly supports multiple ordered composition layers; each runtime owns its implementation, sampling, optical correction, foveation, and resource policy. A Quest, PC, or other runtime can composite Steam Link's 3 projections at adequate quality while Galaxy XR takes a lower-quality private path for the same legal submission. OpenXR guarantees the composition model and ordering, not identical resolution or filtering across runtimes. This device difference is evidence for an Android XR/Galaxy XR implementation policy or bug, not a general rule that Steam Link's renderer topology is invalid.

The decisive A/B is permission versus surface, not merely permission versus denial: compare grant with no window, attached but non-visible/non-display-ready type-2038, visible transparent type-2038, a SystemUI recording indicator with no Steam Link overlay, and the ordinary no-overlay control. For each phase, capture OpenXR session state, `xrWaitFrame.shouldRender`, all recommended-resolution events and re-enumerated recommended/maximum dimensions, submitted layer order and image rectangles, SteamVR render/encode/transport dimensions, decoder buffers, and SurfaceFlinger/HWC participation. If quality changes while every OpenXR-visible value and source/decoder fact remains fixed, the residual cause is vendor-private compositor policy and cannot be selected through documented OpenXR.

## Plain-language explanation

Steam Link submits 3 stereo OpenXR projection layers: full-view imagery plus a higher-detail foveal inset. On Galaxy XR, those separate layers correlate with a low-resolution compositor path unless an Android XR/SystemUI surface or an attached type-2038 overlay enabled by the appear-on-top grant is present. The overlay does not choose among those layers. The surviving native Probe intercepts Valve's streaming `xrEndFrame`, composites the same 3 submitted layers into 1 ordinary stereo projection, and submits that equivalent final image to Android XR. Earlier reconstruction escaped the original low path, but its 3152x3682 reported-maximum output was still visibly below the attached-overlay control. "Single-projection reconstruction" means reconstructing the final OpenXR layer composition; it does not reconstruct video frames, change Steam Link streaming, or invent image detail. Separately, the PC must render and transport a high-detail source: the validated result uses `render/overrideRender=3552x3840`, `encodeWidth=3072`, fixed `streamFormatWidth=1536`, and disabled automatic width selection. Reconstruction cannot restore source detail after VRLink falls back to its `2048x2048` render and automatic `1152` transport defaults.

## Available experimental patch

For Steam Link 2.0.22 build 5002322, the only selectable projection experiment is:

- **Experimental Native Single-Projection Resolution + 10-bit Probe** patches the exact 5002322 AArch64 scene library, routes Valve's streaming `xrEndFrame` through `libgxr_nspp.so`, accepts uniform sRGB8 or RGB10_A2 sources, performs the CPU/GPU-optimized native reconstruction, and records bounded resolution, attachment-precision, decoder-buffer, capability, topology, and `xrEndFrame` telemetry.

The former efficient API-layer, native sRGB-only hook, and native CPU-optimized dual-format patches are removed. Their old mode and library names remain reserved only so the Probe can remove stale decoded-APK resources and reject a scene library already hooked by another helper. The Probe does not force 10-bit: the existing Video output precision path must first make all 6 Valve source swapchains RGB10_A2.

Do not select **Appear on top** or any retired resolution experiment.

The patch:

- removes `SYSTEM_ALERT_WINDOW`;
- adds unmanaged Full Space directly to `VRLink`;
- installs only `single_projection_native_probe_v1` and `libgxr_nspp.so`;
- recognizes only the exact six-swapchain Steam Link streaming topology;
- temporarily retains recognized source images, resolves the multisampled opaque underside and alpha-foveated images, and composites them into two private runtime-limited eye swapchains. The earlier base projection is not sampled because the later same-pose full-FOV underside is opaque and fully covers it under OpenXR ordering;
- uses one centered bilinear foveal sample instead of the retired 4-sample box, disables fixed-function `GL_DITHER` only around its own draw, and requests quality supersampling plus quality sharpening only when `XR_FB_composition_layer_settings` is advertised;
- replaces the three source projections with one opaque stereo projection;
- reuses the last released private output on repeated-image frames only while the source handles, projection space, and full/foveal FOV mapping remain compatible;
- forwards the original frame after safely releasing every held image when any topology, EGL, GL, or synchronization prerequisite is missing.

The Probe accepts only a uniform set of 6 sRGB8 or 6 RGB10_A2 source swapchains and creates matching-format scratch and final swapchains after confirming runtime support. It records the decoder output description and AHardwareBuffer allocation contract, all 6 source swapchain formats, actual GLES attachment component sizes, the final output format and rectangles, and the submitted layer/view count. This can prove or contradict real 10-bit rendering from the decoder through a successful RGB10_A2 `xrEndFrame`. It cannot prove that the private Android XR compositor or panel retained 10-bit; that stage is not directly observable from the application.

For resolution discovery, the Probe records each eye's recommended and maximum dimensions and computes the density-preserving request. It attempts density-preserving, Galaxy XR panel-native 3552x3840, then reported-maximum output. A recoverable oversized `xrEndFrame` rejection discards that frame and permanently advances to the next tier. Only a tier with a successful matching submission is accepted. `XR_FB_foveation` and vendor GL foveation support are logged but not enabled because foveation reduces shading work and does not increase the compositor's declared pixel capacity.

Run only `GalaxyXR-APK\diagnostics\steamlink-resolution-ab\Capture-SteamLinkResolutionRun.ps1`. It asks at startup whether to run the native Probe palm/resolution/10-bit test or the ordinary-3-projection permission-versus-surface matrix. The selector exposes no shared mode, label, repeat, APK, or output arguments.

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

## Current diagnostics

The selector discovers the installed APK hashes and rejects mismatched instrumentation.

Test 1 requires the sole experimental native Probe and no Appear-on-top patch. It performs 1 palm hidden-visible-hidden cycle, compares all 3 visual states with the normal 3-projection + overlay reference, validates the accepted single-projection resolution tier, and proves or contradicts the decoder-to-`xrEndFrame` RGB10_A2 chain.

Test 2 requires build 5002322 with the ordinary Appear-on-top patch and no projection experiment. Its passive `libgxr_pst.so` layer does not enable extensions Valve omitted and forwards Valve's original calls unchanged. In every phase it samples session state, successful `xrWaitFrame`/`xrEndFrame` outcomes, `shouldRender`, current recommended/maximum view dimensions, active swapchain contracts, layer order, and submitted rectangles; it observes recommended-resolution events only if Valve enabled that extension. A shell-protected receiver creates these exact phases in 1 Steam Link/SteamVR session: denied/no window, granted/no window, granted/attached-hidden, granted/visible-transparent, and denied/no Steam overlay with the SystemUI recording indicator visible. Every phase also archives SteamVR render/encode/transport facts timestamp-bounded to the unchanged current vrserver process, a Steam Link PID-bound decoder contract, phase deltas, SurfaceFlinger, and HWC evidence. Missing or ambiguous facts fail closed instead of reusing a previous-session log tail.

The matrix reports vendor-private compositor policy only when visual quality changes while all OpenXR-visible, host, transport, source, and decoder fingerprints remain fixed. A measured public-pipeline change is reported separately; missing state or telemetry is `INCOMPLETE`.

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
  -DOPENXR_SDK_SOURCE_DIR="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/OpenXR-SDK-1.1.61" `
  -DOPENXR_LOADER_LIBRARY="C:/path/to/exact/5002322/arm64-v8a/libopenxr_loader.so"
cmake --build extensions\resolution-trace-layer\build-android-new --target gxr_single_projection_native_probe_v1
cmake --build extensions\resolution-trace-layer\build-android-new --target gxr_permission_surface_trace_v1
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_nspp.so patches\src\main\resources\steamlink\androidxr\
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_pst.so patches\src\main\resources\steamlink\androidxr\
.\gradlew.bat :patches:generatePatchesList -PreleaseChannel=experimental
```
