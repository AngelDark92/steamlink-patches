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
> **[v1.8.0-dev.1](https://github.com/AngelDark92/steamlink-patches/releases/tag/v1.8.0-dev.1)**&nbsp;&nbsp;•&nbsp;&nbsp;`dev`&nbsp;&nbsp;•&nbsp;&nbsp;4 patches total
<details open>
<summary>📦 Steam Link Experimental&nbsp;&nbsp;•&nbsp;&nbsp;3 patches</summary>
<br>

**🎯 Supported versions:**

| 🧪&nbsp;2.0.22 |
| :---: |

| 💊&nbsp;Patch | 📜&nbsp;Description | ⚙️&nbsp;Options |
|----------|----------------|-----------|
| [Controller velocity fix](#controller-velocity-fix) | Experimental: derives current controller linear and angular velocity from grip/aim pose history, avoiding delayed runtime velocity during throws. | • Maximum sample gap (ms)<br>• Derived velocity smoothing<br>• Maximum linear speed (m/s)<br>• Maximum angular speed (rad/s) |
| [TEST EXPERIMENTAL - Baseline Overlay Flow](#test-experimental-baseline-overlay-flow) | A/B test baseline. Keeps launcher bootstrap plus overlay permission flow (Appear on top behavior). Enable this OR the No-Overlay test patch, not both. |  |
| [TEST EXPERIMENTAL - No Overlay / No Permission](#test-experimental-no-overlay-no-permission) | A/B test variant. Replaces GalaxyXRPermissionActivity and GxrOverlayBridge with no-overlay/no-permission-request smali for crash reproduction and comparison. |  |

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
