# Galaxy XR permission-free projection experiment

## Why this experiment changed

The latest captures show a reversible per-frame effect: Steam Link looks low resolution while no Android XR system element is visible, becomes high while the palm home square, a notification, or other Android XR UI is visible, then returns to low when that element disappears.

The measurable host render target, encoder and decoder sizes, OpenXR swapchains, projection image rectangles, focus, and bandwidth do not change at the transition. The previous AppOp, type-2038, activity-window, delayed-window, focus-suppression, and purple-quad probes therefore no longer isolate the remaining likely boundary. Their saved logs are retained as evidence, but their Morphe modes are retired.

The current experiment tests three projection-metadata paths plus one Android XR activity-classification path. It does not force dimensions, change SteamVR settings, or create an Android overlay.

Android's current OpenXR setup guidance requires OpenXR activities to use [`XR_ACTIVITY_START_MODE_FULL_SPACE_UNMANAGED`](https://developer.android.com/develop/xr/openxr/get-started). The legacy 5002244 launcher patch already placed that property directly on `VRLink`; that historical configuration did not by itself establish a fix. Stock native-XR build 5002322 is different: its decoded `VRLink` activity has no start-mode property. The new 5002322-only arm therefore tests the missing declaration on the current architecture without mixing in a projection transform. It is pending headset testing and is not recorded as a proven fix.

## Morphe modes

Build and install exactly one of these patches at a time:

| Morphe patch | Mode recorded in telemetry | Behavior |
|---|---|---|
| **Experimental XR projection trace control** | `projection_trace_control` | Byte-for-byte control: forwards the caller's `xrEndFrame` pointer unchanged and records projection, gaze/action, session, and extension telemetry. |
| **Experimental XR projection quality settings** | `projection_settings_quality` | Requests quality supersampling and sharpening only when Steam Link already enabled `XR_FB_composition_layer_settings`; otherwise fails open. |
| **Experimental XR projection settings stripped** | `projection_settings_stripped` | Removes only safely identifiable leading `XrCompositionLayerSettingsFB` nodes; unknown/unsafe chains are forwarded unchanged. |
| **Experimental VRLink unmanaged full space** | `vrlink_unmanaged_full_space` | 5002322 only. Forwards `xrEndFrame` unchanged and adds exactly one direct unmanaged Full Space property to `VRLink`. |

For every APK:

1. Enable exactly one mode above in Morphe.
2. Disable **Appear on top** and every older resolution, overlay, window, quad, rect, transport, multilayer, toast, and proxy experiment.
3. Patch and install on the device.
4. Run repeat 1 and repeat 2 without rebuilding or reinstalling between them.
5. Install the next mode only after both repeats are complete.

No pre-known APK SHA-256 is needed. The collector hashes the APK actually installed on the device and validates the embedded `GXR_RESOLUTION_MODE` metadata.

## Exact headset sequence

Keep the same SteamVR scene, viewpoint, network, and session conditions.

- **Repeat 1 — no-SystemUI control:** keep all Android XR UI out of view. Mark `LOW0` if the stable image is low or `HIGH0` if the tested patch is already high, then `DONE`.
- **Repeat 2 — reversible system-UI control:** mark the initial state as `LOW0` or `HIGH0`; look at the right palm until the Android XR home square is visible; mark `XR_ELEMENT_APPEARED`; while it is visible mark `HIGH`; hide the square and wait for the image to settle; mark `XR_ELEMENT_DISAPPEARED`; finally mark `LOW1` if it returned low or `HIGH1` if it remained high; then `DONE`.

Do not disconnect or leave VR between the repeat-2 markers. The collector rejects missing, duplicate, or out-of-order markers.

## Capture commands

```powershell
$tool = 'C:\Users\Angelo\Desktop\SteamLink-GalaxyXR-Windows-Toolkit-FULL\GalaxyXR-APK\diagnostics\steamlink-resolution-ab'
$common = @{
  Label = 'overlay-off'
  ExperimentId = 'projection-metadata-01'
  SceneId = 'SteamVR Home - fixed viewpoint'
  NetworkProfile = 'same PC, AP, band, headset position'
}

& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Mode projection_trace_control -Repeat 1
& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Mode projection_trace_control -Repeat 2

& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Mode projection_settings_quality -Repeat 1
& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Mode projection_settings_quality -Repeat 2

& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Mode projection_settings_stripped -Repeat 1
& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Mode projection_settings_stripped -Repeat 2

& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Mode vrlink_unmanaged_full_space -Repeat 1
& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Mode vrlink_unmanaged_full_space -Repeat 2
```

The collector is observational. It does not install or launch the APK, change AppOps, clear app data/logcat, restart SteamVR, or edit SteamVR settings. It copies the live Steam logs immediately from `C:\Program Files (x86)\Steam\logs`, plus package metadata, installed-APK hashes, OpenXR telemetry, `properties.json`, and `vrlink_debug.json` when available. It temporarily pulls the installed `base.apk`, archives only an `aapt2` text dump of its manifest, then deletes the temporary APK. No pre-known SHA-256 is required.

After all eight 5002322 captures:

```powershell
& "$tool\Compare-XrResolutionMatrix.ps1" -Capture @(
  'control-run1.zip', 'control-run2.zip',
  'quality-run1.zip', 'quality-run2.zip',
  'stripped-run1.zip', 'stripped-run2.zip',
  'fullspace-run1.zip', 'fullspace-run2.zip'
)
```

Possible results:

- `USE_PROJECTION_QUALITY_SETTINGS`: the public quality metadata is high without SystemUI in both runs and remains high after SystemUI disappears.
- `USE_PROJECTION_SETTINGS_STRIPPED`: removing Steam Link's existing settings metadata is high without SystemUI in both runs and remains high after disappearance.
- `USE_VRLINK_UNMANAGED_FULL_SPACE`: the standards-correct direct `VRLink` declaration is high without SystemUI in both runs and remains high after disappearance.
- `MULTIPLE_CANDIDATES`: more than one isolated arm succeeds; do not select a production patch until another discriminator separates them.
- `NO_METADATA_FIX`: no isolated candidate reproduces the Android XR system-UI path. Do not ship one; next boundary is a one-layer native reconstruction/flattening feasibility study or an Android XR compositor report.
- `INVALID_CONTROL`: the control did not show the required low → high → low reversal.
- `INCOMPLETE`: trace, provenance, AppOp, mode, or marker validation failed.

A candidate is not a production fix until it remains high after `XR_ELEMENT_DISAPPEARED`, remains permission-free, produces no OpenXR errors, and repeats twice under the same conditions.

## Developer-only rebuild

The Morphe package already contains the native libraries. Rebuild only after changing the C++ layer:

```powershell
cmake -S extensions\resolution-trace-layer -B extensions\resolution-trace-layer\build-android-new -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/android-ndk-r27d/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 `
  -DOPENXR_SDK_SOURCE_DIR="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/OpenXR-SDK-1.1.61"
cmake --build extensions\resolution-trace-layer\build-android-new

Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_projection_trace_control.so patches\src\main\resources\steamlink\androidxr\
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_projection_settings_quality.so patches\src\main\resources\steamlink\androidxr\
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_projection_settings_stripped.so patches\src\main\resources\steamlink\androidxr\
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_vrlink_unmanaged_full_space.so patches\src\main\resources\steamlink\androidxr\

.\gradlew.bat :patches:generatePatchesList -PreleaseChannel=all
```

Keep all prior capture archives. They establish why the retired modes were removed and must not be treated as disposable build output.
