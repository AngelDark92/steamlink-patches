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

The only active successor is **Experimental Single Projection Reconstruction**, mode `single_projection_reconstruction_v1`. It does not adjust metadata: it reconstructs Steam Link's underside and foveated images into one stereo projection before Android XR composition.

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
