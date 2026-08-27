# Galaxy XR permission-free resolution experiment

## Current conclusion

The 2026-08-26/27 build-5002322 captures reproduce the headset behavior:

`LOW without Android XR UI → HIGH while UI is visible → LOW after UI disappears`

The transition is reported as immediate. The later typed timestamps are annotation latency: the palm must face the headset to keep the square visible, which prevents normal typing.

Across low, high, and low-again intervals, the captures keep the same decoder, swapchains, image rectangles, three projection layers, session focus, and valid moving gaze pose. This rules out the centered-gaze fallback and makes an Android XR compositor-path incompatibility the leading boundary.

Steam Link also submits one zero-flag `XrCompositionLayerSettingsFB` node on each of its three projection layers without enabling `XR_FB_composition_layer_settings`. The OpenXR extension requires the extension to be enabled and its flags to be nonzero. The prior stripped runs did not test removal: both loaded the quality layer instead.

## Only active patch

Select **Experimental Android XR projection compatibility** in Morphe for Steam Link 2.0.22 build 5002322.

Do not select **Appear on top** or any retired resolution experiment.

The patch:

- removes `SYSTEM_ALERT_WINDOW`;
- adds unmanaged Full Space directly to `VRLink`;
- installs mode `projection_metadata_compat_v2`;
- removes only safely recognized leading `XrCompositionLayerSettingsFB` nodes;
- preserves all other chain nodes, layer order, flags, dimensions, rectangles, FOV, gaze, decoder, and transport behavior;
- forwards the original frame unchanged when a chain cannot be transformed safely.

## Capture commands

```powershell
$tool = 'C:\Users\Angelo\Desktop\SteamLink-GalaxyXR-Windows-Toolkit-FULL\GalaxyXR-APK\diagnostics\steamlink-resolution-ab'
$common = @{
    Label = 'overlay-off'
    Mode = 'projection_metadata_compat_v2'
    ExperimentId = 'projection-metadata-compat-v2'
    SceneId = 'SteamVR Home - fixed viewpoint'
    NetworkProfile = 'same PC, access point, band, and headset position'
}

& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Repeat 1
& "$tool\Capture-SteamLinkResolutionRun.ps1" @common -Repeat 2
```

The script discovers the installed APK hashes. No pre-known Morphe APK SHA-256 is required.

## Number choices in VR

Repeat 1:

- Resolution: `1` low, `2` high.
- Did Android XR UI appear: `1` no, `2` yes.

Repeat 2:

- Before showing the square: `1` low, `2` high.
- Show the palm square, observe it, let it disappear, then press Enter.
- While visible: `1` low, `2` high.
- After disappearance: `1` low, `2` high.

The script records the visible/after answers as post-event annotations rather than pretending their input times are transition times.

## Acceptance

Success requires two matching cold runs with:

- high resolution without SystemUI in repeat 1;
- high resolution before, during, and after SystemUI in repeat 2;
- no overlay permission and no Steam Link-owned type-2038 window;
- trace build ID `projection-metadata-compat-v2-20260828`;
- sampled frames with three settings nodes seen, three removed, no unsafe layer, and successful `xrEndFrame`;
- no new visual, decoder, OpenXR, or SteamVR regression.

If this remains low, do not restore the retired manifest, gaze, window, dimension, transport, quad, or metadata-quality guesses. The only remaining APK-side implementation is a source-owned GLES/OpenXR pass that reconstructs the base, underside, and foveated textures into one projection layer. If a correctly reconstructed single projection remains UI-dependent, the fix boundary is Android XR/SpaceFlinger or Valve's renderer rather than an ordinary permission-free APK.

## Retired history

See `SteamLink-GalaxyXR-Python-Patches-Already-Tried-for_Resolution_issue/README.md`. Saved capture archives are evidence and must not be deleted.

## Developer rebuild

```powershell
cmake -S extensions\resolution-trace-layer -B extensions\resolution-trace-layer\build-android-new -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/android-ndk-r27d/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 `
  -DOPENXR_SDK_SOURCE_DIR="C:/Users/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/GalaxyXR-APK/tools-galaxyxr-native/OpenXR-SDK-1.1.61"
cmake --build extensions\resolution-trace-layer\build-android-new
Copy-Item extensions\resolution-trace-layer\build-android-new\libgxr_projection_metadata_compat_v2.so patches\src\main\resources\steamlink\androidxr\
.\gradlew.bat :patches:generatePatchesList -PreleaseChannel=all
```
