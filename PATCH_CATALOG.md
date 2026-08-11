# Patch Catalog — steamlink-patches

Reference for conflict detection when importing external patches.
Each entry lists the exact APK artifact and value(s) a patch writes or modifies.

---

## androidxr group

### XR Core Runtime (`xrCoreRuntimePatch`)
**Default: enabled**
| Artifact | Edit |
|---|---|
| `lib/arm64-v8a/libgxr_xr_bridge.so` | New file (Galaxy XR OpenXR runtime bridge) |
| `res/drawable-anydpi/ic_launcher_background.xml` | Full replace |
| `res/drawable-anydpi/ic_launcher_background_gradient.xml` | New file (resource ID 0x7f010000) |
| `res/values/public.xml` | Full replace (stable IDs: ic_launcher_background_gradient=0x7f010000, ic_launcher=0x7f010001/0x7f030000) |
| `res/values/ids.xml` | Create if missing (empty `<resources/>`) |
| Extension DEX (base APK) | Merges `extension.mpe`: adds `GalaxyXRPermissionActivity`, `GxrOverlayBridge`, `GxrSdlBridge`, `GxrSurfaceCallback`; extends `SDLSurface`, `SDLControllerManager`, `SDLGenericMotionListener_API14` |

Sub-patch only (not exposed): `disablePermissionPromptNativePatch`
| `lib/arm64-v8a/libvrlink_scene.so` @ file offset `0x1422c4` | 8 bytes: replaces `RequestAndroidPermissions()` prologue with `movz w0,#1; ret` (versionCode 5002244 only; skipped for all other builds) |

---

### XR Device Config Baseline (`xrDeviceConfigBaselinePatch`)
**Default: enabled** — depends on `xrCoreRuntimePatch`
| Artifact | Edit |
|---|---|
| `assets/config/hmd_config.json` | Full replace — Galaxy XR HMD identity (sSerialNumber=VRLINKHMDGALAXYXR, sManufacturerName=Samsung, sModelNumber=Galaxy XR, sControllerType=galaxy_xr_hmd, requestedExtensions=[XR_EXT_eye_gaze_interaction]) |
| `assets/config/controller_config.json` | Full replace — /interaction_profiles/oculus/touch_controller static props + pose action offset + input/haptic action bindings |
| `assets/config/default_config.json` | Full replace — `preflight.ignore_microphone_muted = false` |
| `assets/webui/dash/index.html` | Full replace — Steam Link dashboard HTML bootstrap |

---

### XR Manifest Capability Pack (`xrManifestCapabilityPackPatch`)
**Default: enabled** — depends on `xrCoreRuntimePatch`
| Artifact | Edit |
|---|---|
| `AndroidManifest.xml` `uses-sdk@android:minSdkVersion` | Set to `29` |
| `AndroidManifest.xml` `uses-sdk@android:targetSdkVersion` | Set to `36` |
| `AndroidManifest.xml` `uses-sdk@android:maxSdkVersion` | Removed |
| `AndroidManifest.xml` `uses-permission` | Removes all `com.oculus.permission.*` and `com.picovr.permission.*` entries |
| `AndroidManifest.xml` `uses-feature` | Removes all `oculus.software.*` and `com.oculus.feature.*` entries |
| `AndroidManifest.xml` `meta-data` | Removes all `com.oculus.*`, `com.htc.vr.*`, `pvr.*`, `pxr.*`, `picovr.*` entries |
| `AndroidManifest.xml` `uses-native-library` | Removes `libopenxr_forwardloader.oculus.so` |
| `AndroidManifest.xml` `category` | Removes `com.oculus.intent.category.VR` and `com.oculus.intent.category.2D` |
| `AndroidManifest.xml` `uses-permission` | Adds: `org.khronos.openxr.permission.OPENXR`, `OPENXR_SYSTEM`, `android.permission.ACCESS_COARSE_LOCATION`, `ACCESS_FINE_LOCATION`, `HAND_TRACKING`, `EYE_TRACKING_FINE` |
| `AndroidManifest.xml` `uses-feature` | Adds: `android.hardware.vr.headtracking` (v1, required), `android.software.xr.api.openxr` (v0x10001, required), `android.hardware.xr.input.controller/hand_tracking/eye_tracking` (optional) |
| `AndroidManifest.xml` `queries/provider@android:authorities` | Adds `org.khronos.openxr.runtime_broker;org.khronos.openxr.system_runtime_broker` |
| `AndroidManifest.xml` `queries/intent` | Adds `org.khronos.openxr.OpenXRRuntimeService` and `org.khronos.openxr.OpenXRApiLayerService` intents |
| `AndroidManifest.xml` `application/uses-native-library@android:name` | Adds `libopenxr.google.so` (optional) |
| `AndroidManifest.xml` `application/property@android:name` | Adds `android.window.PROPERTY_XR_BOUNDARY_TYPE_RECOMMENDED = XR_BOUNDARY_TYPE_LARGE` |

---

### XR Launcher Bootstrap (`xrLauncherBootstrapPatch`)
**Default: enabled** — depends on `xrManifestCapabilityPackPatch`
| Artifact | Edit |
|---|---|
| `AndroidManifest.xml` `application/activity@android:name` | Adds `com.valvesoftware.steamlink.GalaxyXRPermissionActivity` (exported=true, MAIN/LAUNCHER, 1280×800px layout) |
| `AndroidManifest.xml` `VRLink activity/property` | Adds `android.window.PROPERTY_XR_ACTIVITY_START_MODE = XR_ACTIVITY_START_MODE_FULL_SPACE_UNMANAGED` |
| `AndroidManifest.xml` `VRLink activity/intent-filter/category` | Adds `org.khronos.openxr.intent.category.IMMERSIVE_HMD` |
| `AndroidManifest.xml` `SteamLink activity/intent-filter` | Removes LAUNCHER intent-filter |
| `AndroidManifest.xml` `SteamLink activity/layout` | Sets `android:defaultWidth=1280.0px`, `android:defaultHeight=800.0px` |

---

### XR Input Routing Config (`xrInputRoutingConfigPatch`)
**Default: enabled** — depends on `xrLauncherBootstrapPatch`
| Artifact | Edit |
|---|---|
| `assets/config/ui_config.json` | Full replace — XR pointer aim/select bindings for touch_controller and hand_interaction_ext; haptic bindings |

---

### Controller Velocity Fix (`controllerVelocityPatch`)
**Default: disabled** (experimental) — depends on `xrCoreRuntimePatch`
| Artifact | Edit |
|---|---|
| `lib/arm64-v8a/libgxr_controller_velocity.so` | New file with embedded config patched at magic `GXRVELCFG0000001` |
| `lib/arm64-v8a/libvrlink_scene.so` `QSVLClient::OnTopOfFrame` | Optional exact-layout AArch64 edits select stock 4×, evenly phased 2×, or display-rate 1× controller pose events while retaining the final type-2 frame-update event; verified layouts: versionCodes 5001712, 5002206, 5002244 |
| config block `+32` (int64 LE) | `maxDeltaMs × 1,000,000` nanoseconds — default 50 ms |
| config block `+40` (float32 LE) | `maxLinearSpeed` m/s — default 20.0 |
| config block `+44` (float32 LE) | `maxAngularSpeed` rad/s — default 50.0 |
| config block `+48` (float32 LE) | `smoothing` EMA weight — default 0.0 |
| `assets/openxr/1/api_layers/implicit.d/XR_APILAYER_local_GalaxyXR_controller_velocity.json` | New file (OpenXR implicit API layer manifest; disable env: `GXR_DISABLE_CONTROLLER_VELOCITY`) |

**Option:** `poseSendCadence` — `stock-4x` (default), `half-2x`, or `display-1x`. Actual sends per second equal the active display rate multiplied by 4, 2, or 1. Non-stock modes fail closed on unrecognized native layouts.

---

### GXR Face Bridge (`gxrFacebridgePatch`)
**Default: enabled** — no dependencies on other XR patches
| Artifact | Edit |
|---|---|
| `lib/arm64-v8a/libgxr_face_bridge.so` | New file (XR_FB_face_tracking2 → XR_ANDROID_face_tracking API layer) |
| `assets/openxr/1/api_layers/implicit.d/XR_APILAYER_local_GalaxyXR_face_bridge.json` | New file (instance extension `XR_FB_face_tracking2`; disable env: `GXR_DISABLE_FACE_BRIDGE`) |
| `AndroidManifest.xml` `uses-permission` | Adds `android.permission.FACE_TRACKING` |

---

### Appear On Top (`appearOnTopPatch`)
**Default: enabled** — depends on `xrLauncherBootstrapPatch`
| Artifact | Edit |
|---|---|
| `AndroidManifest.xml` `uses-permission` | Adds `android.permission.SYSTEM_ALERT_WINDOW` (required for `GxrOverlayBridge` TYPE_APPLICATION_OVERLAY compositor window) |

---

### Unrestricted Battery Usage (`unrestrictedBatteryUsagePatch`)
**Default: enabled** — depends on `xrLauncherBootstrapPatch`
| Artifact | Edit |
|---|---|
| `AndroidManifest.xml` `uses-permission` | Adds `android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` |
| `GalaxyXRPermissionActivity` | Opens the app-specific Battery usage page at startup when not unrestricted; falls back to the direct exemption prompt, then app details |

---

### TEST — Baseline Overlay Flow (`overlayBaselineTestPatch`)
**Default: disabled** (experimental A/B baseline)
Same edits as `appearOnTopPatch`. Mutually exclusive with `noOverlayNoPermissionTestPatch`.

---

### TEST — No Overlay / No Permission (`noOverlayNoPermissionTestPatch`)
**Default: disabled** (experimental A/B variant)
| Artifact | Edit |
|---|---|
| `smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali` | Replaces with no-op variant (ensureOverlay() returns early without creating TYPE_APPLICATION_OVERLAY window) |
| `smali/com/valvesoftware/steamlink/GalaxyXRPermissionActivity.smali` | Replaces with stub that skips SYSTEM_ALERT_WINDOW request and calls launchVrLink() directly |

---

### TEST — Old Scene requestExit Bridge (`oldSceneRequestExitBridgePatch`)
**Default: disabled** (experimental adapter; standalone)
| Artifact | Edit |
|---|---|
| `smali/com/valvesoftware/steamlink/VRLink.smali` | Replaces `.method private native requestExit()V` with Java `finishAndRemoveTask()` bridge implementation |

---

## binary group

### HMD-Only Pose Fix (`hmdOnlyPatch`)
**Default: enabled**
| Artifact | Edit |
|---|---|
| `lib/arm64-v8a/libvrlink_scene.so` @ hook vaddr (version-specific) | 4 bytes: `ldr x2,[sp,#8]` → `B <PLT_cave_va>` (AArch64 unconditional branch to trampoline) |
| `lib/arm64-v8a/libvrlink_scene.so` @ PLT cave (last 32 B of first PT_LOAD) | 32 bytes: trampoline — original hook insn + MOVZ x16,low16(offsetNs) + MOVK x16,hi16(offsetNs),lsl#16 + ADD x2,x2,x16 + B hook+4 + NOP×3 |
| Velocity fields `[x19+28]` … `[x19+48]` (6× float/double) | Replaced with `STUR XZR` or `STR WZR` (zeroes PackedPose_t linear/angular velocity) |

**Option:** `offsetMs` — encodes as nanoseconds split across MOVZ/MOVK immediates; default 60, range 0–4000

**Version layouts (matched by `libvrlink_scene.so` file size):**
| versionCode | File size | Hook vaddr | PLT cave vaddr |
|---|---|---|---|
| 5001712 | 2,221,072 | `0x1014E8` | `0x20F2D0` |
| 5002172 | 2,238,792 | `0xFD860` | `0x213370` |
| 5002206 | 2,239,920 | `0xFDD68` | `0x213820` |
| 5002244 | 2,251,920 | `0xFEAD8` | `0x2166B0` |

---

### OLED Color Calibration / Output Precision (`oledCalibrationPatch`)
**Default: enabled**
> ⚠️ Shares the GLSL shader block in `libvrlink_scene.so` with `videoDitherPatch`. Dependency ordering runs OLED calibration first so dither selection cannot be overwritten. Swapchain-format editing is guarded to ARM64 versionCode 5002244.

| Artifact | Edit |
|---|---|
| `lib/arm64-v8a/libvrlink_scene.so` GLSL block (1087 bytes at `#version 300 es` before `GL_OES_EGL_image_external_essl3`) | Full replace with calibrated `highp` shader |
| GLSL `pow(clamp(c,0,1), vec3(GAMMA))` | `gamma` option value (float) |
| GLSL `mix(vec3(luma), c, SATURATION)` | `saturation` option value (float) |
| GLSL D2020-approximating 3×3 color matrix | Fixed: `_valve1_d2020d709` (not user-configurable) |
| GLSL dither | Zero-centred per-channel noise using `UniDitherOffsets.rgb`; scale `0.00292` for sRGB8 or `0.00073` for RGB10_A2 |
| GLSL `DITHER_ENABLE` | `1.` when enabled; toggled to `0.` by `videoDitherPatch` without losing the selected scale |
| Three 5002244 instructions at `0x10826c`, `0x1082dc`, `0x10834c` | `GL_SRGB8_ALPHA8` (`69 88 91 52`) or experimental `GL_RGB10_A2` (`29 0B 90 52`) |
| RGB10_A2 shader output | Explicit sRGB EOTF converts calibrated code values to the linear OpenXR swapchain |

**Options:**
| Key | Default | Range | Target in binary |
|---|---|---|---|
| `profile` | `initial` | initial / final-balanced / custom | Selects gamma+saturation pair |
| `gamma` | `1.06` | 0.50–2.50 | `vec3(GAMMA)` argument in `pow()` |
| `saturation` | `1.12` | 0.00–3.00 | Second argument in `mix()` |
| `outputPrecision` | `srgb8-highp` | srgb8-highp / rgb10-a2-experimental | Selects shader transfer/dither scale and all three projection swapchain formats |

`rgb10-a2-experimental` is fail-closed: the patch requires the 2,251,920-byte 5002244 library, the unique shader/NUL boundary, all three known instruction contexts, and a uniform current swapchain state. Galaxy XR runtime support remains unverified; an unsupported format can prevent stream swapchain setup.

---

### Video Dither (`videoDitherPatch`)
**Default: enabled**
> ⚠️ Shares the GLSL shader block in `libvrlink_scene.so` with `oledCalibrationPatch`. Handles both stock and calibrated variants automatically.

| Artifact | Edit |
|---|---|
| `lib/arm64-v8a/libvrlink_scene.so` GLSL (stock shader) | 2 bytes at `color.rgb += fract(...)*.00292`: `//` (disabled) ↔ `  ` (enabled) |
| `lib/arm64-v8a/libvrlink_scene.so` GLSL (legacy calibrated) | `*.00292` ↔ `*.00000` in expression `) - .5) * .00292;` |
| `lib/arm64-v8a/libvrlink_scene.so` GLSL (highp output variants) | `DITHER_ENABLE=1.` ↔ `DITHER_ENABLE=0.` while preserving scale `0.00292` or `0.00073` |

**Option:** `enable` (bool, default true)

---

## identity group

### Device Identity (`deviceIdentityPatch`)
**Default: enabled** — depends on `xrDeviceConfigBaselinePatch`
> ⚠️ Overwrites `assets/config/hmd_config.json` written by `xrDeviceConfigBaselinePatch`. Dependency ordering guarantees this runs second.

| Artifact | Edit |
|---|---|
| `assets/config/hmd_config.json` | Full replace (overrides baseline) with profile-specific file |

**Fields overwritten:** `sSerialNumber`, `sManufacturerName`, `sModelNumber`, `sControllerType`, `sDeviceType`, `sInputProfilePath`, `requestedExtensions` (all per-profile values under `staticProps.*`)

**Option `profile`:**
| Value | File used |
|---|---|
| `samsung-default` | No change (baseline retained) |
| `meta-quest-pro` | `steamlink/identity/hmd_config_meta_quest_pro.json` |
| `pico-4-pro` | `steamlink/identity/hmd_config_pico_4_pro.json` |

---

### Change Package Name (`changePackageNamePatch`)
**Default: disabled**
| Artifact | Edit |
|---|---|
| `AndroidManifest.xml` `manifest@package` | Set to new package name |
| `AndroidManifest.xml` `permission@android:name` | Prefix-replaced for custom permissions declared by this package |
| `AndroidManifest.xml` `uses-permission@android:name` | Prefix-replaced for custom permissions used by this package |
| `AndroidManifest.xml` `provider@android:authorities` | String-replaced for content provider authorities |

**Option:** `packageName` — default appends `.gxr` to original; accepts any valid Java package name regex `^[a-z]\w*(\.[a-z]\w*)+$`

---

---

## Shared-file conflict matrix

| APK artifact | Patches that write to it |
|---|---|
| `lib/arm64-v8a/libvrlink_scene.so` | `disablePermissionPromptNativePatch` (8 B @ 0x1422c4), `hmdOnlyPatch` (hook + cave + velocity), `controllerVelocityPatch` (controller cadence instructions in `QSVLClient::OnTopOfFrame`), `oledCalibrationPatch` (1087-byte GLSL block plus three guarded swapchain instructions), `videoDitherPatch` (dither-state marker inside GLSL block) |
| `assets/config/hmd_config.json` | `xrDeviceConfigBaselinePatch` (baseline), `deviceIdentityPatch` (profile override — intentional) |
| `AndroidManifest.xml` | `xrManifestCapabilityPackPatch`, `xrLauncherBootstrapPatch`, `gxrFacebridgePatch`, `appearOnTopPatch`, `changePackageNamePatch` |
| `res/values/ids.xml` | `androidXrLibPatch`, `controllerVelocityPatch`, `gxrFacebridgeLibPatch` (all: idempotent create-if-missing only) |

**Known intentional coupling:** `oledCalibrationPatch` rewrites the full GLSL block first; `videoDitherPatch` depends on it and then toggles the generated highp dither state. Its byte helper still recognises stock and legacy-calibrated states for guarded compatibility tests.
