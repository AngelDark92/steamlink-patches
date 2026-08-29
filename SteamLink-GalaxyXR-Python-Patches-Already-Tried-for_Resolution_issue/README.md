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

Build `single-projection-reconstruction-v1.1-20260829` fixes the sampler query, adds guarded last-output reuse for repeated frames, and reduces release-event logging. It is statically built only; headset stability and quality remain unverified.

Build `two-projection-drop-base-v1-20260829` implements the previously untried discriminator: it removes only the redundant first opaque full-FOV projection, leaving the original underside plus foveal projections and image lifetime untouched. It has passed static build/provenance tests but has not been installed or tested on the headset. The historical legacy two-layer experiment removed the underside, so it is not evidence against this test.

The first live `three-projection-sampler-proxy-v1` capture is invalid as quality evidence: v1 learned the exact topology but disabled on the next zero-layer spinner/idle frame before any proxy transform. The recorded `LOW0` came after original-submission fallback. Build `three-projection-sampler-proxy-v1.1-20260829` now primes all six proxy buffers, tolerates legal zero-layer idle frames without mixing original projections, and marks activation only on its first real transform. It is statically validated but still needs a new headset run.

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
