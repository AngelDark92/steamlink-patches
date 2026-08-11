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
> **[v1.9.2-dev.2](https://github.com/AngelDark92/steamlink-patches/releases/tag/v1.9.2-dev.2)**&nbsp;&nbsp;•&nbsp;&nbsp;`dev`&nbsp;&nbsp;•&nbsp;&nbsp;19 patches total
<details open>
<summary>📦 Steam Link&nbsp;&nbsp;•&nbsp;&nbsp;18 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.22 |
| :---: |

| 💊&nbsp;Patch | 📜&nbsp;Description | ⚙️&nbsp;Options |
|----------|----------------|-----------|
| [Android XR native permission names](#android-xr-native-permission-names) | Replaces native Oculus face/eye permission checks with the Android XR permission names used by the legacy Galaxy XR build. |  |
| [Appear on top](#appear-on-top) | Recommended. Adds SYSTEM_ALERT_WINDOW to the manifest so GalaxyXRPermissionActivity can request overlay permission at startup. |  |
| [Change package name](#change-package-name) | Renames the app package so it can be installed alongside the original Steam Link. Default appends '.gxr'. Changing the package name may break features that rely on the original identity. | • Package name |
| [Controller velocity fix](#controller-velocity-fix) | Derives current controller linear and angular velocity from grip/aim pose history and can reduce VRLink's stock four controller pose sends per display frame. | • Maximum sample gap (ms)<br>• Controller pose-send cadence<br>• Derived velocity smoothing<br>• Maximum linear speed (m/s)<br>• Maximum angular speed (rad/s) |
| [Device identity](#device-identity) | Overrides the HMD manufacturer/model identity reported to SteamVR (hmd_config.json only; controller identity is unaffected). 'samsung-default' leaves the file untouched. | • HMD identity |
| [Force HMD initialization gates](#force-hmd-initialization-gates) | Bypasses the two legacy-tested capability gates in QSVLDeviceHmd::Init for Steam Link build 5002244. |  |
| [Force lobby permission-state gate](#force-lobby-permission-state-gate) | Bypasses the legacy-tested permission-state gate in XrSceneLobby for Steam Link build 5002244. |  |
| [Force stream XR gates](#force-stream-xr-gates) | Bypasses the three legacy-tested XR feature/permission gates in XrSceneStream::Init for Steam Link build 5002244. |  |
| [GXR face bridge](#gxr-face-bridge) | Installs libgxr_face_bridge.so (XR_FB_face_tracking2 → XR_ANDROID_face_tracking API layer) and adds android.permission.FACE_TRACKING to the manifest. |  |
| [HMD-only pose fix](#hmd-only-pose-fix) | Adds a configurable offset to the HMD OpenXR pose-query time and zeroes all six exported HMD velocity fields. Does not affect controller paths. | • Pose offset (ms) |
| [OLED color calibration](#oled-color-calibration) | Replaces VRLink's embedded GLSL fragment shader with configurable Galaxy XR OLED gamma and saturation correction. | • Calibration profile<br>• Gamma<br>• Saturation |
| [Unrestricted battery usage](#unrestricted-battery-usage) | Recommended. Opens Android's per-app Battery usage page at startup so Unrestricted can be selected for XR streaming. |  |
| [Video dither](#video-dither) | Enables (or disables) the dormant GLSL dither term in VRLink's video fragment shader. Reduces 8-bit contouring on OLED displays. | • Enable dither |
| [XR Core Runtime](#xr-core-runtime) | Installs the Galaxy XR runtime bridge resources and extension DEX foundation used by other XR patches. |  |
| [XR Device Config Baseline](#xr-device-config-baseline) | Installs baseline Galaxy XR HMD/controller/default config payloads and dashboard bootstrap assets. |  |
| [XR Input Routing Config](#xr-input-routing-config) | Installs ui_config.json mappings for XR pointer/button routing in launcher UI flows. |  |
| [XR Launcher Bootstrap (Home Space)](#xr-launcher-bootstrap-home-space) | Installs GalaxyXRPermissionActivity as launcher and configures Steam Link/VRLink activity XR startup wiring. |  |
| [XR Manifest Capability Pack](#xr-manifest-capability-pack) | Adds Android XR/OpenXR permissions, features, runtime queries, and app-level XR properties. |  |

</details>

<details open>
<summary>📦 Steam Link Experimental&nbsp;&nbsp;•&nbsp;&nbsp;1 patch</summary>
<br>

**🎯 Supported versions:**

| 🧪&nbsp;2.0.22 |
| :---: |

| 💊&nbsp;Patch | 📜&nbsp;Description | ⚙️&nbsp;Options |
|----------|----------------|-----------|
| [XR resolution permission experiment](#xr-resolution-permission-experiment) | Experimental window/permission A/B probe. Turn off Appear on top and do not combine with older resolution test APK modifications. | • Experiment mode |

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
