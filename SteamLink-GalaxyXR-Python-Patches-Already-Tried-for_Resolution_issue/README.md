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

The 2026-08-30 `single_projection_fovea_quads_v1` capture proved 5042 successful transformations but stayed `LOW0 -> LOW -> LOW1`, visibly worse than the original two- and three-projection low path. It also recorded repeated compositor buffer-acquisition and latch failures. The patch is retired and removed from Morphe; do not repeat or tune it.

## Captured single-projection v1 result

The 2026-08-28 captures reached `HIGH0` without overlay permission and `HIGH0 → HIGH → HIGH1` across the Android XR UI observation. This supersedes the earlier UI-dependent result, but v1 still alternated between reconstructed one-projection and original three-projection submission on repeated-image frames: 283 of 7,557 eligible frames in run 1 and 11 of 370 captured eligible frames in run 2. Every reconstructed frame also correlated one-for-one with a leaked `GL_INVALID_ENUM` from an invalid sampler-state query.

The 2026-08-29 v1.2 rerun was stable for 5772 successful transformations and stayed `HIGH0 -> HIGH -> HIGH1`, but text and edges remained below the granted-overlay control. Telemetry proves why full native foveal density is impossible in one uniform projection: the 3745x4048 detail-preserving request was capped by Android XR at 3152x3682. The runtime did not advertise `XR_FB_composition_layer_settings`, so no legal compositor supersampling hint could be attached.

The corrected `two-projection-drop-base-v1.1-20260829` Repeat 2 capture is valid: 3967 successful 3-to-2 submissions stayed `LOW0 -> HIGH -> LOW1`. It preserved the original underside and alpha-foveal projections without allocation, GL, resampling, disable, or failed `xrEndFrame`. The redundant base and an exact count of three projections are rejected as causes.

The corrected three-projection v1.2 rerun is also valid: 4811 successful unchanged submissions stayed `LOW0 -> HIGH -> LOW1`. New sample-count-1 swapchains did not escape the low path. Together with the two-projection result, this localizes the reversible switch to Galaxy XR compositor policy for multiple projections and/or the alpha-foveated multilayer topology, not original swapchain identity, MSAA, or the redundant base.

DynamicPolicyManager exposed constant supersampling, recommended-resolution, and current-app/SystemUI FRS values across the palm switch. No public Android XR/OpenXR setter for the hidden state was found. Multiple projections were accepted successfully, so the evidence indicates a runtime implementation policy or bug rather than a core OpenXR layer-count limitation.

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
