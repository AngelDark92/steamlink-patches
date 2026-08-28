# Galaxy XR permission-free resolution experiment

## Current conclusion

The 2026-08-26/27 build-5002322 captures reproduce the headset behavior:

`LOW without Android XR UI → HIGH while UI is visible → LOW after UI disappears`

The transition is reported as immediate. The later typed timestamps are annotation latency: the palm must face the headset to keep the square visible, which prevents normal typing.

Across low, high, and low-again intervals, the captures keep the same decoder, swapchains, image rectangles, three projection layers, session focus, refresh policy, and valid moving gaze pose. The corrected metadata-removal layer transformed 75 sampled frames successfully and still did not change the behavior.

Static analysis of Virtual Desktop proves the architectural difference that now drives this experiment: Virtual Desktop reconstructs its foveated streams inside the application and submits one stereo projection, while Steam Link exposes its base, underside, and alpha-foveated images as three projections. The remaining APK-side path is to reconstruct Steam Link's images before Android XR composition.

## Only active patch

Select **Experimental Single Projection Reconstruction** in Morphe for Steam Link 2.0.22 build 5002322.

Do not select **Appear on top** or any retired resolution experiment.

The patch:

- removes `SYSTEM_ALERT_WINDOW`;
- adds unmanaged Full Space directly to `VRLink`;
- installs mode `single_projection_reconstruction_v1`;
- recognizes only the exact six-swapchain Steam Link streaming topology;
- temporarily retains recognized source images, resolves the multisampled opaque underside and alpha-foveated images, and composites them into two private full-density eye swapchains. The earlier base projection is not sampled because the later same-pose full-FOV underside is opaque and fully covers it under OpenXR ordering;
- replaces the three source projections with one opaque stereo projection;
- forwards the original frame after safely releasing every held image when any topology, EGL, GL, or synchronization prerequisite is missing.

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
- trace build ID `single-projection-reconstruction-v1-20260828`;
- sampled frames proving three input projections and six views became one output projection and two views with successful `xrEndFrame`;
- no new visual, decoder, OpenXR, or SteamVR regression.

If a trace-proven reconstructed single projection remains UI-dependent, stop APK experiments. The remaining fix boundary is Android XR/SpaceFlinger or Valve's renderer rather than an ordinary permission-free APK.

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
.\gradlew.bat :patches:generatePatchesList -PreleaseChannel=all
```
