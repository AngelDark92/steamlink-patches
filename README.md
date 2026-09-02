# 🥽 Steam Link GalaxyXR Patches

[Morphe](https://morphe.software) patches that make Steam Link work on the Samsung Galaxy XR (SM-I610).

## ❓ About

Steam Link VR (`com.valvesoftware.steamlinkvr`) was not built for Android XR. These patches adapt it to run on the Samsung Galaxy XR headset by injecting the missing OpenXR permissions and features, bundling the Galaxy XR XR-bridge native library, providing an optional standalone face-bridge layer for face-tracking, fixing broken permission flows, tuning the rendering pipeline, and optionally allowing the patched APK to coexist with the original install.

Target APK: `com.valvesoftware.steamlinkvr`. Exact compatibility metadata and guarded adaptations include v2.0.20 builds 5001712 and 5001740 plus v2.0.22 builds 5002172, 5002206, 5002244, 5002313, 5002318, and 5002322. The high-resolution patch additionally recognizes only exact v2.0.22 build 5002296. The available 5001740 source is an analysis reconstruction from a malformed hybrid APK, so pristine-APK Morphe patching, installation, and headset runtime validation remain pending. The permission-free high-resolution fix accepts exact builds 5001712, 5002244, 5002296, 5002313, 5002318, and 5002322; only 5002322 has headset validation, while the other decoded-base adaptations are statically validated. Reconstruction, quad-view, permission-matrix, warm-up/omit, and DFR re-arm experiments are retired.

Use Morphe Manager 1.22 or newer with compatibility checks enabled for build-specific filtering. Manager 1.7 cannot distinguish APKs that share versionName `2.0.22`, and Expert mode may intentionally show incompatible patches. Morphe's `default` flag is global, so 4 exact-build dependency bundles provide version-aware recommendations while every individual patch remains default-off. Both legacy bundles select the same [16-patch set](PATCH_CATALOG.md#recommendation-bundles), including the 3 native force-gate patches, all required XR foundation patches, and Device identity with the Meta Quest Pro spoof. Build 5002322 selects only GXR face bridge, Galaxy XR high-resolution fix, Microphone input preset (Voice Recognition), OLED color calibration (`final-balanced`), Unrestricted battery usage, and Visual Delay Fix (60 ms). Build 5002318 remains a separate native-XR-safe set with Galaxy XR Device identity. Builds 5002296 and 5002313 retain expert-selectable patches but no automatic bundle. **Appear on top (legacy)** and **Change package name** are never recommended.

For legacy bundles through 5002244, including **2.0.22/5002244** and **2.0.20/5001712**, leave **HMD identity** on **Recommended for this build** or explicitly choose **Meta Quest Pro**. The spoof reports `Oculus Quest Pro` while retaining Galaxy XR tracking/controller routing. Existing saved explicit Samsung, Stock, or Pico choices are respected, so change those if necessary. The automatic recommendation still resolves to Galaxy XR on 5002318; 5002322 has no identity patch.

Bundle selection does not broaden native compatibility: high-resolution output remains unavailable on 5001740, 5002172, and 5002206; the 3 force-gate edits also remain unavailable on 5002172 and 5002206. Those edits safely skip instead of guessing native layouts. Both 5001712 and 5002244 have the complete legacy set available.

**Video dither is retired from all catalogs and bundles.** OLED-generated shaders default to dithering off, while preserving the calibration profile and output precision. [Developer opt-in instructions](PATCH_CATALOG.md#video-dither-retired-developer-opt-in) retain the information needed to enable it in a local build; no dither checkbox remains in Morphe.

To download it:
1. Open steam console `steam://open/console`
2. In the steam console tab run `download_depot 250820 250824 634053834998054244` for version 2.0.22-5002244 (less stable but newer) or `download_depot 250820 250824 1108221034296079583` for version 2.0.20-5001712 (reported as more stable by the community)
3. After steam reports download complete retrieve the apk from `C:\Program Files (x86)\Steam\steamapps\content\app_250820\depot_250824\drivers\vrlink\resources\android-steamlinkvr-release.apk` and copy it onto your headset
4. Select it with Morphe. In normal compatibility-filtered mode, select the single recommended bundle for the detected exact build. On 5002322 this includes the permission-free high-resolution fix and standalone OLED `final-balanced`; do not also select **Appear on top (legacy)**. No desktop IP, pairing token, APK hash, or native telemetry enrollment is required. For face/tongue tracking, install VRCFaceTracking plus the matching [Galaxy XR LinkFT module](https://github.com/compdoge/LinkFT), then enable Steam Link OSC, eye sharing, face sharing, and output port 9015.

## 🩹 Patches list

<!-- PATCHES_START EXPANDED -->
> **[v1.11.1-dev.8](https://github.com/AngelDark92/steamlink-patches/releases/tag/v1.11.1-dev.8)**&nbsp;&nbsp;•&nbsp;&nbsp;`dev`&nbsp;&nbsp;•&nbsp;&nbsp;23 patches total
<details open>
<summary>📦 Steam Link&nbsp;&nbsp;•&nbsp;&nbsp;23 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.20 (5001740) | 2.0.20 (5001712) | 2.0.22 (5002172) | 2.0.22 (5002206) | 2.0.22 (5002244) | 2.0.22 (5002313) | 2.0.22 (5002318) | 2.0.22 (5002322) | 2.0.20 (5001712) | 2.0.22 (5002244) | 2.0.22 (5002296) | 2.0.22 (5002313) | 2.0.22 (5002318) | 2.0.22 (5002322) |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Static-analysis adaptation for Steam Link 2.0.20 build 5001740; pristine-APK patching and runtime validation remain pending. | Verified Steam Link 2.0.20 build 5001712. | Verified Steam Link 2.0.22 build 5002172. | Verified Steam Link 2.0.22 build 5002206. | Verified Steam Link 2.0.22 build 5002244. | Verified Steam Link 2.0.22 build 5002313. | Build 5002318 recommends its native-Android-XR-safe set: Galaxy XR high-resolution 3-projection fix, Device identity, Microphone input preset, OLED color calibration, GXR face bridge, Visual Delay Fix, and Unrestricted battery usage. Appear on top remains an optional legacy fallback. | Build 5002322 recommends Galaxy XR high-resolution 3-projection fix, GXR face bridge, Microphone input preset (Voice Recognition), Unrestricted battery usage, Visual Delay Fix (60 ms), and OLED color calibration with the Final balanced tested profile. The retired projection experiments are excluded. | Exact Steam Link 2.0.20/5001712 high-resolution target with its isolated 2-projection to 3-layer payload. The topology correction has prior user-reported startup and delayed-frame runtime evidence; this rebuilt binary remains uninstalled. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.22 build 5002244; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.22 build 5002296; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.22 build 5002313; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.22 build 5002318; headset validation pending. | Headset-validated Galaxy XR high-resolution patch target for exact Steam Link 2.0.22 build 5002322. |

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Android XR native permission names](#android-xr-native-permission-names) | Replaces native Oculus face/eye permission checks with the Android XR permission names used by Galaxy XR, including the verified Steam Link 5001712 layout. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |
| [Appear on top (legacy)](#appear-on-top-legacy) | Legacy overlay-permission fallback retained for older Steam Link builds. Adds SYSTEM_ALERT_WINDOW and the compositor signal window. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313, 5002318 |  |
| [Change package name](#change-package-name) | Renames the manifest package and Steam Link's internal VR-launch component so the patched app can coexist with the original installation. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 | • Package name |
| [Controller velocity fix](#controller-velocity-fix) | Derives current controller linear and angular velocity from grip/aim pose history and can reduce VRLink's stock four controller pose sends per display frame. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 | • Maximum sample gap (ms)<br>• Controller pose-send cadence<br>• Derived velocity smoothing<br>• Maximum linear speed (m/s)<br>• Maximum angular speed (rad/s) |
| [Device identity](#device-identity) | Overrides the HMD identity reported to SteamVR. Recommended selects Meta Quest Pro for exact legacy bundle targets through 5002244, including 2.0.20/5001712; otherwise Galaxy XR. The Galaxy profile installs its complete transport identity while preserving stock controller/hand routing and extensions. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313, 5002318 | • HMD identity |
| [Force HMD initialization gates](#force-hmd-initialization-gates) | Bypasses the two verified capability gates in QSVLDeviceHmd::Init for Steam Link builds 5001712, 5001740, 5002244, and 5002313. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |
| [Force lobby permission-state gate](#force-lobby-permission-state-gate) | Bypasses the verified permission-state gate in XrSceneLobby for Steam Link builds 5001712, 5001740, 5002244, and 5002313. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |
| [Force stream XR gates](#force-stream-xr-gates) | Bypasses the three verified XR gates in builds 5001712, 5001740, and 5002244. Build 5002313 rewrote XrSceneStream::Init and is intentionally left unchanged. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |
| [GXR face bridge](#gxr-face-bridge) | Installs libgxr_face_bridge.so (XR_FB_face_tracking2 → XR_ANDROID_face_tracking API layer) and adds android.permission.FACE_TRACKING to the manifest. See the [GXR Face Bridge source](https://github.com/compdoge/gxr-face-bridge) and matching [Galaxy XR VRCFT module](https://github.com/compdoge/LinkFT). | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313, 5002318, 5002322 |  |
| [Galaxy XR high-resolution 3-projection fix](#galaxy-xr-high-resolution-3-projection-fix) | Permission-free resolution fix for exact builds 5001712, 5002244, 5002296, 5002313, 5002318, and 5002322. Preserves each build's native projection layout (2 layers on 2.0.20/5001712; 3 layers on supported 2.0.22 builds) and source formats, including future RGB10_A2, while appending a static 2x2 Android-surface compositor trigger with no image copy or reconstruction. | 5001712, 5002244, 5002296, 5002313, 5002318, 5002322 |  |
| [Galaxy XR legacy foundation (through 2.0.22/5002244)](#galaxy-xr-legacy-foundation-through-2-0-22-5002244) | Selects the 16-patch Galaxy XR legacy set, including Meta Quest Pro identity, native gates, face bridge, OLED calibration, microphone, battery, Visual Delay, and XR foundation. High-resolution output is guarded to verified layouts; unavailable on 5001740, 5002172, and 5002206. | 5001740, 5002172, 5002206, 5002244 |  |
| [Galaxy XR recommended set (2.0.20/5001712)](#galaxy-xr-recommended-set-2-0-20-5001712) | Applies the 16-patch Galaxy XR legacy set for exact Steam Link 2.0.20 build 5001712, including Meta Quest Pro identity, permission-free high resolution, and the Final balanced tested OLED profile. | 5001712 |  |
| [Galaxy XR recommended set (2.0.22/5002318)](#galaxy-xr-recommended-set-2-0-22-5002318) | Applies the existing native-Android-XR-safe Galaxy XR patch set for exact Steam Link 2.0.22 build 5002318. | 5002318 |  |
| [Galaxy XR recommended set (2.0.22/5002322)](#galaxy-xr-recommended-set-2-0-22-5002322) | Applies the validated permission-free Galaxy XR patch set for exact Steam Link 2.0.22 build 5002322, including the Final balanced tested OLED profile. | 5002322 |  |
| [Microphone input preset](#microphone-input-preset) | Selects the Android AAudio microphone processing mode used by Steam Link. Galaxy XR testing found Voice Recognition clearer and louder than stock Voice Communication. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313, 5002318, 5002322 | • Microphone mode |
| [OLED color calibration](#oled-color-calibration) | Calibrates Galaxy XR OLED color and selects a guarded high-precision video output path for Steam Link builds 5001712, 5001740, 5002244, 5002313, 5002318, and 5002322. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313, 5002318, 5002322 | • Calibration profile<br>• Gamma<br>• Saturation<br>• Video output precision |
| [Unrestricted battery usage](#unrestricted-battery-usage) | Opens Android's per-app Battery usage page at startup so Unrestricted can be selected for XR streaming. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313, 5002318, 5002322 |  |
| [Visual Delay Fix](#visual-delay-fix) | Adds a configurable offset to the HMD OpenXR pose-query time and zeroes all six exported HMD velocity fields. Does not affect controller paths. Its trampoline uses a dedicated executable mapping over non-runtime ELF comment bytes and preserves live PLT entries. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313, 5002318, 5002322 | • Pose offset (ms) |
| [XR Core Runtime](#xr-core-runtime) | Installs the Galaxy XR runtime bridge resources and extension DEX foundation used by other XR patches. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |
| [XR Device Config Baseline](#xr-device-config-baseline) | Installs baseline Galaxy XR HMD/controller/default config payloads and dashboard bootstrap assets. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |
| [XR Input Routing Config](#xr-input-routing-config) | Installs ui_config.json mappings for XR pointer/button routing in launcher UI flows. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |
| [XR Launcher Bootstrap (Home Space)](#xr-launcher-bootstrap-home-space) | Installs GalaxyXRPermissionActivity as launcher and configures the Steam Link VR activity XR startup wiring. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |
| [XR Manifest Capability Pack](#xr-manifest-capability-pack) | Adds Android XR/OpenXR permissions, features, runtime queries, and app-level XR properties. | 5001712, 5001740, 5002172, 5002206, 5002244, 5002313 |  |

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
