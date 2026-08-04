# 🥽 Steam Link GalaxyXR Patches

[Morphe](https://morphe.software) patches that make Steam Link work on the Samsung Galaxy XR (SM-I610).

## ❓ About

Steam Link VR (`com.valvesoftware.steamlinkvr`) was not built for Android XR. These patches adapt it to run on the Samsung Galaxy XR headset by injecting the missing OpenXR permissions and features, bundling the Galaxy XR bridge native libraries, fixing broken permission flows, tuning the rendering pipeline, and optionally allowing the patched APK to coexist with the original install.

Target APK: `com.valvesoftware.steamlinkvr` v2.0.22 (versionCode 5002244).

## 🩹 Patches list

<!-- PATCHES_START EXPANDED -->

<!-- Do not modify this section by hand. The patch list is generated when release.yml creates a new release.
     
     If you wish for the patches list to be collapsed, then remove the word 'EXPANDED' from the comment tag above.

     If you wish to manually keep this list updated then remove the PATCHES_START and PATCHES_END 
     comment blocks entirely. -->

| Patch | Default | Description |
|---|---|---|
| **Android XR compatibility** | ✅ | Makes Steam Link fully functional on Samsung Galaxy XR. Adds Android XR / OpenXR permissions and features, HMD and controller identity configs, Galaxy XR bridge native libraries, the permission-bootstrap launcher activity, and the XR spatial-pointer SDL input bridge. |
| **No overlay permission** | | Removes `SYSTEM_ALERT_WINDOW` usage so Steam Link installs and runs without the overlay permission prompt that tears down the XR stream. |
| **Disable permission prompt** | | Replaces VRLink's `RequestAndroidPermissions` with a no-op to prevent stream teardown on Galaxy XR. |
| **Low-latency decoder** | | Forces `findBestDecoder()` to always select the low-latency hardware decoder (`KEY_LOW_LATENCY`), reducing decode jitter from ~11 ms median to ≤8 ms. |
| **Pose prediction offset** | | Adds +16.77 ms to the `XrTime` argument passed to `xrLocateSpace`, compensating for the Android XR runtime's local-display prediction being too early for a wireless stream. |
| **Frame queue latency offset** | | Adds a fixed offset to VRLink's frame-queue latency budget to compensate for wireless pipeline delay. `full` adds +32.768 ms; `half` adds +16.384 ms. |
| **HMD-only pose fix** | | Adds 78 ms to the HMD OpenXR pose-query time and zeroes all six exported HMD velocity fields. Does not affect controller paths. |
| **Legacy two-layer renderer** | | Restores the 5001712-era two-layer XR stream topology by skipping underside swapchain creation and submission added in 5002244. |
| **OLED color calibration** | | Replaces VRLink's embedded GLSL fragment shader with a Galaxy XR OLED-tuned version. Profile `initial`: gamma 1.06 / sat 1.12. Profile `final-balanced`: gamma 1.20 / sat 1.45. |
| **Video dither** | | Enables (or disables) the dormant GLSL dither term in VRLink's video fragment shader. Reduces 8-bit contouring on OLED displays. |
| **Change package name** | | Renames the app package so it can be installed alongside the original Steam Link. Default appends `.gxr`. |

&nbsp;

## 🚀 How to use

Add this repository as a patch source in [Morphe Manager](https://morphe.software):

```
https://github.com/AngelDark92/steamlink-patches
```

Or click: https://morphe.software/add-source?github=AngelDark92/steamlink-patches

<!-- PATCHES_END -->

### 🛠️ Building

```
./gradlew buildAndroid
```

Output: `patches/build/libs/patches-*.mpp`

No Android SDK is required. The extension DEX is assembled from smali sources directly by the build.

## 📜 License

Steam Link GalaxyXR Patches are licensed under the [GNU General Public License v3.0](LICENSE)
