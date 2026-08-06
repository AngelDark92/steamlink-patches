# Test Variant APK Building Guide

## Overview
Test variants isolate different components (overlay window, permission request, window type) to determine which ones are necessary for full-resolution rendering in Steam Link VR.

## Variant Selection & Building

### Baseline (Phase 1)
**Files**: Original build
- ✅ Full-space unmanaged XR activity mode
- ✅ SDL metrics override → managed-panel 1280×800
- ✅ Overlay window: TYPE_APPLICATION_OVERLAY (0x7f6)
- ✅ Permission request: Yes (Settings.ACTION_MANAGE_OVERLAY_PERMISSION)
- ✅ Manifest permission: SYSTEM_ALERT_WINDOW

**Build**: Standard `gradlew build`
**Expected**: High resolution rendering with overlay window visible.

---

### Variant 1: No Overlay Window (Phase 2)
**Files**:
- Replace: `patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali`
- With: `patches/src/main/resources/steamlink/androidxr/smali/test_variants/GxrOverlayBridge_NoOverlay.smali`

**Behavior**:
- ✅ Full-space unmanaged XR activity mode
- ✅ SDL metrics override → managed-panel 1280×800
- ❌ Overlay window: NOT created (returns true without creating)
- ✅ Permission check: Still performed
- ✅ Manifest permission: SYSTEM_ALERT_WINDOW (declared but not used)

**Build**:
```powershell
# 1. Copy variant into place
cp patches/src/main/resources/steamlink/androidxr/smali/test_variants/GxrOverlayBridge_NoOverlay.smali `
   patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali

# 2. Clean and rebuild
.\gradlew.bat clean build
```

**Test Goal**: Determine if overlay window creation (not just permission) is required for resolution.

**Expected Outcomes**:
- **If resolution == baseline**: Overlay is not required; resolution controlled exclusively by activity mode + SDL metrics.
- **If resolution < baseline**: Overlay window creation triggers undocumented compositor policy required for full resolution.

---

### Variant 2: No Overlay + No Permission Request (Phase 3)
**Files**:
- Replace: `patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GalaxyXRPermissionActivity.smali`
- With: `patches/src/main/resources/steamlink/androidxr/smali/test_variants/GalaxyXRPermissionActivity_NoOverlay_NoPermission.smali`
- AND use: `GxrOverlayBridge_NoOverlay.smali` from Variant 1

**Behavior**:
- ✅ Full-space unmanaged XR activity mode
- ✅ SDL metrics override → managed-panel 1280×800
- ❌ Overlay window: NOT created
- ❌ Permission request: SKIPPED (no Settings dialog on first launch)
- ⚠️ Manifest permission: SYSTEM_ALERT_WINDOW (declared but ignored by WindowManager)

**Build**:
```powershell
# 1. Copy both variants into place
cp patches/src/main/resources/steamlink/androidxr/smali/test_variants/GxrOverlayBridge_NoOverlay.smali `
   patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali

cp patches/src/main/resources/steamlink/androidxr/smali/test_variants/GalaxyXRPermissionActivity_NoOverlay_NoPermission.smali `
   patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GalaxyXRPermissionActivity.smali

# 2. Clean and rebuild
.\gradlew.bat clean build
```

**Test Goal**: Eliminate permission flow entirely; test if resolution is achievable without any overlay or permission mechanism.

**Expected Outcomes**:
- **If app launches and resolution == baseline**: Overlay and permission are both optional.
- **If app crashes/fails**: Permission may be required even if overlay is skipped (unlikely based on documentation).

---

### Variant 3: Alternate Window Type (TYPE_APPLICATION) (Phase 4)
**Files**:
- Replace: `patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali`
- With: `patches/src/main/resources/steamlink/androidxr/smali/test_variants/GxrOverlayBridge_TypeApplication.smali`

**Behavior**:
- ✅ Full-space unmanaged XR activity mode
- ✅ SDL metrics override → managed-panel 1280×800
- ✅ Window created: TYPE_APPLICATION (0x2) — does NOT require SYSTEM_ALERT_WINDOW
- ✅ Permission request: Still performed (for testing purposes)
- ⚠️ Manifest permission: SYSTEM_ALERT_WINDOW (declared but not enforced for TYPE_APPLICATION)

**Build**:
```powershell
# 1. Copy variant into place
cp patches/src/main/resources/steamlink/androidxr/smali/test_variants/GxrOverlayBridge_TypeApplication.smali `
   patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali

# 2. Clean and rebuild
.\gradlew.bat clean build
```

**Test Goal**: Determine if TYPE_APPLICATION_OVERLAY (permission-requiring overlay type) is specifically needed, or if any window type works for compositor signaling.

**Expected Outcomes**:
- **If resolution == baseline**: TYPE_APPLICATION sufficient; can eliminate permission requirement.
- **If resolution < baseline**: TYPE_APPLICATION_OVERLAY is specifically required by Samsung compositor.

---

## Measurement & Logging

### Capture Logs
```powershell
# Clear previous logs
adb logcat -c

# Capture for 30 seconds during active gameplay
adb logcat > variant_run.log &
Start-Sleep -Seconds 30
adb logcat -c

# Alternatively, capture with filters
adb logcat "SteamLinkGXR|GxrVelocity" > variant_run_filtered.log
```

### Key Log Tags
- **SteamLinkGXR**: Overlay lifecycle, permission flow, surface metrics
- **GxrVelocity**: OpenXR system properties (recommended swapchain size)

### Log Examples (Baseline)
```
[SteamLinkGXR] Installed compositor overlay (TYPE_APPLICATION_OVERLAY 0x7f6)
[SteamLinkGXR] SurfaceMetrics: renderW=1280 renderH=800
[GxrVelocity] OpenXR system max rec swapchain: 2560x1440
```

### Expected Log Examples (Variant 1: No Overlay)
```
[SteamLinkGXR] ensureOverlay (VARIANT): Skip overlay creation, permission granted
[SteamLinkGXR] SurfaceMetrics: renderW=1280 renderH=800
[GxrVelocity] OpenXR system max rec swapchain: 2560x1440
```

---

## Revert to Baseline
```powershell
# Restore original files from git
git checkout patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali
git checkout patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GalaxyXRPermissionActivity.smali

# Clean and rebuild
.\gradlew.bat clean build
```

---

## Decision Matrix

| Variant | Overlay | Window Type | Resolution | Conclusion |
|---------|---------|------------|-----------|-----------|
| Baseline | ✅ TYPE_APPLICATION_OVERLAY | High | Baseline for comparison |
| V1 | ❌ None | High | Overlay not required |
| V1 | ❌ None | Low | Overlay required for resolution |
| V2 | ❌ None | High | Permission not required |
| V2 | ❌ None | Low | Permission required (unlikely) |
| V3 | ✅ TYPE_APPLICATION | High | TYPE_APPLICATION sufficient |
| V3 | ✅ TYPE_APPLICATION | Low | TYPE_APPLICATION_OVERLAY required |

---

## Next Steps After Testing
1. **If V1 high resolution**: Skip overlay, commit to `feature/permission-free-resolution`
2. **If V3 high resolution**: Use TYPE_APPLICATION, update manifest to remove SYSTEM_ALERT_WINDOW
3. **If all low resolution**: Document overlay as mandatory, commit to `docs/overlay-required-for-resolution`
4. **If V1 low but V3 high**: Investigate why TYPE_APPLICATION works but no overlay fails (edge case)
