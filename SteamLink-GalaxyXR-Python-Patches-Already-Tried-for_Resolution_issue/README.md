# Steam Link Galaxy XR resolution experiments already tried

This directory is the historical record for approaches that did not produce permission-free, persistently high resolution. Do not stack these experiments into a new APK.

## Captured on build 5002322

| Experiment | Result |
| --- | --- |
| Projection trace control | Low without Android XR UI; high only while UI was visible; returned low afterward. Diagnostic only. |
| Projection quality settings | Same UI-dependent result. The transform remained inactive because `XR_FB_composition_layer_settings` was not enabled. |
| VRLink unmanaged Full Space | Manifest change was proven installed, but the UI-dependent low/high/low behavior remained. Keep unmanaged Full Space only as required Android XR configuration, not as the resolution fix. |
| Projection settings stripped | Attempt invalid: the captures requested stripped mode but loaded `projection_settings_quality`. It was not a valid test and must not be cited as a failure of settings removal. |
| Projection metadata compatibility v2 | Corrected removal was proven active on 75 sampled frames across two intact runs. Resolution remained low without SystemUI, high only while SystemUI was visible, then low again. The hypothesis is conclusively rejected. |

The active successors are mutually exclusive: **Experimental Single Projection Reconstruction** (`single_projection_reconstruction_v1`), **Experimental Two Projection Drop Base** (`two_projection_drop_base_v1`), and **Experimental Three Projection Sampler Proxy** (`three_projection_sampler_proxy_v1`). The proxy keeps all three projections while replacing only the six submitted MSAA swapchains, allowing the next run to distinguish projection topology from swapchain/resource classification.

## Captured single-projection v1 result

The 2026-08-28 captures reached `HIGH0` without overlay permission and `HIGH0 → HIGH → HIGH1` across the Android XR UI observation. This supersedes the earlier UI-dependent result, but v1 still alternated between reconstructed one-projection and original three-projection submission on repeated-image frames: 283 of 7,557 eligible frames in run 1 and 11 of 370 captured eligible frames in run 2. Every reconstructed frame also correlated one-for-one with a leaked `GL_INVALID_ENUM` from an invalid sampler-state query.

The 2026-08-29 v1.1 rerun was stable and stayed `HIGH0 -> HIGH -> HIGH1`, but text and edges still showed visible pixel shimmer and the result remained below the granted-overlay control. Telemetry proves why full native foveal density is impossible in one uniform projection: the 3745x4048 detail-preserving request was capped by Android XR at 3152x3682. Build `single-projection-reconstruction-v1.2-20260829` adds four-sample subpixel foveal minification and explicitly enables/requests compositor quality supersampling. It is statically built only and must be compared for edge stability, not treated as recovered source resolution.

The first `two-projection-drop-base-v1-20260829` capture is invalid: it submitted one successful two-projection frame, disabled on the next auxiliary frame, and then passed the original three projections for the LOW observation. Build `two-projection-drop-base-v1.1-20260829` preserves readiness across auxiliary spinner/UI frames while continuing to forward the original underside plus foveal projections unchanged. A new run is required; the historical legacy two-layer experiment removed the underside, so it is not evidence against this test.

The first two live `three_projection_sampler_proxy_v1` captures were invalid as quality evidence. v1 disabled on a zero-layer transition. v1.1 primed all six proxies, but one-shot setup pushed Steam Link into its non-target spinner path and it disabled before any proxy transform. The corrected v1.2 rerun is valid: 4811 successful unchanged three-projection submissions stayed active across `LOW0 -> HIGH -> LOW1` during the SystemUI palm cycle. New sample-count-1 swapchains therefore do not escape the low path; the reversible quality switch is downstream in Android XR compositor policy. Projection count versus the alpha-foveated multi-layer topology remains unresolved until the fixed two-projection mode is rerun.

## Retired permission and window probes

- no-window and denied-permission controls;
- granted permission without a window;
- live type-2038 overlay before VR, removed before VR, and added after VR;
- activity-owned `TYPE_APPLICATION`, decor-view, direct-VR, and VRLink-live windows;
- lifecycle/focus suppression variants;
- baseline overlay-flow and no-overlay/no-permission variants.

Permission-free application windows did not reproduce the Android XR SystemUI effect. A real type-2038 overlay requires special user/platform authorization and is only a control.

## Retired renderer, transport, and compositor probes

- forced recommended swapchain dimensions and projection rectangles;
- pre-handshake and transport-ceiling size changes;
- duplicate/multilayer and purple OpenXR quad probes;
- legacy two-layer renderer topology;
- VRLink activity-layer proxy;
- persistent toast and HWC proxy variants;
- read-only resolution trace variants.
- projection metadata compatibility v2 (`projection_metadata_compat_v2`).

The Python files beside this README preserve those historical builders when present locally. Git history preserves retired Morphe/C++ implementations.

## Rejected without another headset test

- forcing Steam Link's centered fallback gaze valid;
- adding `XR_ANDROID_eye_tracking` as a resolution bridge.

The 2026-08-26/27 captures show the existing `XR_EXT_eye_gaze_interaction` action active, successful gaze-space locations, and valid pose flags throughout low, high, and low-again intervals. These patches no longer test a plausible resolution gate.

## Evidence boundary

The saved captures remain under `C:\Users\Angelo\Documents\GalaxyXR-Diagnostics\SteamLink-Resolution-AB`. Typed marker delays reflect the time needed to lower the palm and answer; they do not show a delayed resolution transition.
