# 🥽 Steam Link GalaxyXR Patches

[Morphe](https://morphe.software) patches that make Steam Link work on the Samsung Galaxy XR (SM-I610).

## ❓ About

Steam Link VR (`com.valvesoftware.steamlinkvr`) was not built for Android XR. These patches adapt it to run on the Samsung Galaxy XR headset by injecting the missing OpenXR permissions and features, bundling the Galaxy XR XR-bridge native library, providing an optional standalone face-bridge layer for face-tracking, fixing broken permission flows, tuning the rendering pipeline, and optionally allowing the patched APK to coexist with the original install.


Target APK: `com.valvesoftware.steamlinkvr` v2.0.22 (versionCode 5002244).
To download it:
1. Open steam console `steam://open/console`
2. In the steam console tab run `download_depot 250820 250824 634053834998054244`
3. After steam reports download complete retrieve the apk from `C:\Program Files (x86)\Steam\steamapps\content\app_250820\depot_250824\drivers\vrlink\resources\android-steamlinkvr-release.apk` and copy it onto your headset
4. Select it with Morphe and either run the default patches or enable expert mode to select the ones you want to enable.

## 🩹 Patches list

<!-- PATCHES_START EXPANDED -->
> **[v1.4.3-dev.1](https://github.com/AngelDark92/steamlink-patches/releases/tag/v1.4.3-dev.1)**&nbsp;&nbsp;•&nbsp;&nbsp;`dev`&nbsp;&nbsp;•&nbsp;&nbsp;17 patches total
<details open>
<summary>📦 Steam Link&nbsp;&nbsp;•&nbsp;&nbsp;10 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.22 |
| :---: |

| 💊&nbsp;Patch | 📜&nbsp;Description | ⚙️&nbsp;Options |
|----------|----------------|-----------|
| [Android XR compatibility](#android-xr-compatibility) | Makes Steam Link fully functional on Samsung Galaxy XR. Adds Android XR / OpenXR permissions and features, HMD and controller identity configs, Galaxy XR bridge native libraries, safe permission handling, the launcher bootstrap, and managed-panel aspect correction with XR spatial-pointer button input. This patch is required for Galaxy XR operation. |  |
| [Appear on top](#appear-on-top) | Requests appear-on-top permission before launch and installs the 2x2 transparent compositor overlay used by the full-resolution Galaxy XR path. |  |
| [Device identity](#device-identity) | Overrides the HMD manufacturer/model identity reported to SteamVR (hmd_config.json only; controller identity is unaffected). 'samsung-default' leaves the file untouched. | • HMD identity |
| [GXR face bridge](#gxr-face-bridge) | Installs libgxr_face_bridge.so (XR_FB_face_tracking2 → XR_ANDROID_face_tracking API layer) and adds android.permission.FACE_TRACKING to the manifest. |  |
| [HMD-only pose fix](#hmd-only-pose-fix) | Adds a configurable offset to the HMD OpenXR pose-query time and zeroes all six exported HMD velocity fields. Does not affect controller paths. | • Pose offset (ms) |
| [Managed-panel aspect fix](#managed-panel-aspect-fix) | Matches the Steam Link managed panel to Galaxy XR's combined stereo-display aspect ratio, preventing SDL physical-display metrics from stretching the launcher UI. |  |
| [OLED color calibration](#oled-color-calibration) | Replaces VRLink's embedded GLSL fragment shader with configurable Galaxy XR OLED gamma and saturation correction. | • Calibration profile<br>• Gamma<br>• Saturation |
| [Video dither](#video-dither) | Enables (or disables) the dormant GLSL dither term in VRLink's video fragment shader. Reduces 8-bit contouring on OLED displays. | • Enable dither |
| [XR direct input & metrics fix](#xr-direct-input-metrics-fix) | Directly patches the real SDLSurface.surfaceChanged()/onTouch() and SDLGenericMotionListener_API14.onGenericMotion() method bodies in the original app classes, since the extension DEX merge cannot override methods that already exist there. Applies the managed-panel aspect fix and Galaxy XR pointer routing to the code paths that actually execute. |  |
| [XR launcher input](#xr-launcher-input) | Routes Galaxy XR spatial-pointer, controller, and XR_EXT_hand_interaction events to Steam Link launcher mouse/select input. |  |

</details>

<details open>
<summary>📦 Steam Link Experimental&nbsp;&nbsp;•&nbsp;&nbsp;6 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.22 |
| :---: |

| 💊&nbsp;Patch | 📜&nbsp;Description | ⚙️&nbsp;Options |
|----------|----------------|-----------|
| [Controller velocity fix](#controller-velocity-fix) | Experimental: derives current controller linear and angular velocity from grip/aim pose history, avoiding delayed runtime velocity during throws. | • Maximum sample gap (ms)<br>• Derived velocity smoothing<br>• Maximum linear speed (m/s)<br>• Maximum angular speed (rad/s) |
| [Frame queue latency offset](#frame-queue-latency-offset) | Adds a fixed offset to VRLink's frame-queue latency budget to compensate for wireless pipeline delay. 'full' adds +32.768 ms; 'half' adds +16.384 ms. | • Offset size |
| [Legacy two-layer renderer](#legacy-two-layer-renderer) | Restores the 5001712-era two-layer XR stream topology by skipping underside swapchain creation and submission added in 5002244. |  |
| [Low-latency decoder](#low-latency-decoder) | Forces findBestDecoder() to always select the low-latency hardware decoder (KEY_LOW_LATENCY), reducing decode jitter from ~11 ms median to ≤8 ms on Galaxy XR. |  |
| [No overlay permission](#no-overlay-permission) | Disables the appear-on-top compositor-overlay path while preserving Android XR input and managed-panel aspect fixes. |  |
| [Pose prediction offset](#pose-prediction-offset) | Adds +16.77 ms to the XrTime argument passed to xrLocateSpace, compensating for the Android XR runtime's local-display prediction being too early for a wireless stream. |  |

</details>

<details open>
<summary>🌐 Universal&nbsp;&nbsp;•&nbsp;&nbsp;1 patch</summary>
<br>

| 💊&nbsp;Patch | 📜&nbsp;Description | ⚙️&nbsp;Options |
|----------|----------------|-----------|
| [Change package name](#change-package-name) | Renames the app package so it can be installed alongside the original Steam Link. Default appends '.gxr'. Changing the package name may break features that rely on the original identity. | • Package name |

</details>

<!-- PATCHES_END -->

### 🛠️ Building

```
./gradlew buildAndroid
```

Output: `patches/build/libs/patches-*.mpp`

No Android SDK is required. The extension DEX is assembled from smali sources directly by the build.

## 📜 License

Steam Link GalaxyXR Patches are licensed under the [GNU General Public License v3.0](LICENSE)
