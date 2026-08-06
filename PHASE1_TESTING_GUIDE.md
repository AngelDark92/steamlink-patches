# Phase 1 Implementation Complete: Ready for Device Testing

## Summary

**What's Done:**
✅ Phase 1 instrumentation implemented and building cleanly
✅ Logging added for OpenXR system properties, overlay lifecycle, and surface metrics
✅ Three test variants prepared (Phases 2–4)
✅ Complete testing documentation provided

**Current APK:** Phase 1 baseline with instrumentation
- Builds with: `.\gradlew.bat build`
- Output: `patches/build/libs/morphe-patches-*.jar` (APK patch bundle)

---

## Device Testing Sequence

### 1. Deploy Phase 1 Baseline
```powershell
# Ensure USB debugging is enabled on device
adb devices

# Clear logs
adb logcat -c

# Deploy the patched APK (follow your normal patching + deployment process)
# Then launch SteamLink and run gameplay scenario for 10+ seconds

# Capture logs while running
adb logcat "SteamLinkGXR|GxrVelocity" > phase1_baseline.log

# Screenshot actual render output (visual resolution check)
adb shell screencap -p /sdcard/phase1_baseline.png
adb pull /sdcard/phase1_baseline.png

# Notes to capture:
# - Record startup time, any permission prompts
# - Note overlay window visibility (2×2 transparent)
# - Check if UI/content appears full resolution
# - Log timestamps for when swapchain is requested
```

### 2. Switch to Phase 2 (No Overlay)
```powershell
# Replace overlay bridge with variant
cp patches/src/main/resources/steamlink/androidxr/smali/test_variants/GxrOverlayBridge_NoOverlay.smali `
   patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali

# Rebuild
.\gradlew.bat clean build

# Deploy variant APK (follow your patching process)
adb logcat -c

# Run same 10+ second gameplay scenario
adb logcat "SteamLinkGXR|GxrVelocity" > phase2_no_overlay.log
adb shell screencap -p /sdcard/phase2_no_overlay.png
adb pull /sdcard/phase2_no_overlay.png
```

### 3. Compare Logs & Screenshots
```powershell
# Analyze log files for key metrics:
Select-String "OpenXR system max rec swapchain" phase1_baseline.log, phase2_no_overlay.log
Select-String "SurfaceMetrics" phase1_baseline.log, phase2_no_overlay.log
```

**Critical Comparison:**
| Metric | Phase 1 (Baseline) | Phase 2 (No Overlay) | Conclusion |
|--------|---|---|---|
| Recommended swapchain size | Log value | Log value | Should match |
| Surface metrics (W×H) | Log value | Log value | Should match |
| Visual resolution (screenshot) | Full resolution? | Same as Phase 1? | If same → overlay not required |

### 4. Decide Next Phase
- **If Phase 2 resolution == Phase 1**: Overlay is NOT required → Skip to Phase 3 (no permission)
- **If Phase 2 resolution < Phase 1**: Overlay IS required → Skip to Phase 4 (TYPE_APPLICATION test)
- **If Phase 2 crashes/fails**: Report error logs; may need investigation

### 5. Phase 3 (Optional): No Overlay + No Permission
```powershell
# If Phase 2 passed, test without permission flow
cp patches/src/main/resources/steamlink/androidxr/smali/test_variants/GxrOverlayBridge_NoOverlay.smali `
   patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali

cp patches/src/main/resources/steamlink/androidxr/smali/test_variants/GalaxyXRPermissionActivity_NoOverlay_NoPermission.smali `
   patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GalaxyXRPermissionActivity.smali

.\gradlew.bat clean build

# Deploy, capture logs, compare
# Expected: No permission dialog, app launches directly
```

### 6. Phase 4 (Optional): TYPE_APPLICATION Window
```powershell
# If Phase 2 overlay was required, test TYPE_APPLICATION window (no permission needed)
cp patches/src/main/resources/steamlink/androidxr/smali/test_variants/GxrOverlayBridge_TypeApplication.smali `
   patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali

.\gradlew.bat clean build

# Deploy, capture logs, compare
# Expected log: "Installed compositor window (TYPE_APPLICATION 0x2 variant...)"
```

---

## Revert to Baseline
```powershell
git checkout patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali
git checkout patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GalaxyXRPermissionActivity.smali
.\gradlew.bat clean build
```

---

## Key Logs to Extract

**Phase 1 Baseline** (use grep or Select-String in logs):
```
[SteamLinkGXR] Installed compositor overlay (TYPE_APPLICATION_OVERLAY 0x7f6)
[SteamLinkGXR] SurfaceMetrics: renderW=1280 renderH=800
[GxrVelocity] OpenXR system max rec swapchain: <WIDTH>x<HEIGHT>
```

**Phase 2 No Overlay**:
```
[SteamLinkGXR] ensureOverlay (VARIANT): Skip overlay creation, permission granted
[SteamLinkGXR] SurfaceMetrics: renderW=1280 renderH=800
[GxrVelocity] OpenXR system max rec swapchain: <WIDTH>x<HEIGHT>
```

**Phase 4 TYPE_APPLICATION**:
```
[SteamLinkGXR] Installed compositor window (TYPE_APPLICATION 0x2 variant...)
[SteamLinkGXR] SurfaceMetrics: renderW=1280 renderH=800
[GxrVelocity] OpenXR system max rec swapchain: <WIDTH>x<HEIGHT>
```

---

## Decision Tree

**After Phase 2 results:**
- **Resolution maintained** → Overlay not required for rendering
  - **Next**: Try Phase 3 (skip permission entirely)
  - **If Phase 3 works**: DECISION: Permission-free resolution possible
  - **If Phase 3 fails**: Overlay may be required for app stability (not just resolution)
  
- **Resolution degraded** → Overlay or overlay type is required
  - **Next**: Try Phase 4 (TYPE_APPLICATION instead of TYPE_APPLICATION_OVERLAY)
  - **If Phase 4 maintains resolution**: TYPE_APPLICATION sufficient; can eliminate permission
  - **If Phase 4 also degrades**: TYPE_APPLICATION_OVERLAY specifically required

---

## Expected Timelines

- **Phase 1 capture + Phase 2 build**: ~10 minutes
- **Phase 2 deploy + capture**: ~10 minutes
- **Log analysis**: ~5 minutes
- **Total per decision cycle**: ~25 minutes

**Full test suite (all 4 variants)**: ~90 minutes including captures and analysis

---

## What to Do With Results

**If overlay is NOT required:**
1. Branch: `feature/permission-free-resolution`
2. Remove manifest `SYSTEM_ALERT_WINDOW` permission
3. Remove permission request flow
4. Remove overlay creation
5. Document: "Resolution controlled exclusively by VRLink XR activity mode + SDL metrics override"

**If overlay IS required but TYPE_APPLICATION works:**
1. Branch: `feature/permission-free-via-type-application`
2. Update `GxrOverlayBridge.smali` to use TYPE_APPLICATION
3. Remove manifest `SYSTEM_ALERT_WINDOW` permission
4. Document: "Resolution requires window (TYPE_APPLICATION); TYPE_APPLICATION_OVERLAY specific type not required"

**If TYPE_APPLICATION_OVERLAY is required:**
1. Branch: `docs/overlay-permission-required-for-resolution`
2. Document reason: "Samsung Android XR compositor policy requires TYPE_APPLICATION_OVERLAY window for full-resolution rendering"
3. Keep current implementation; consider alternative compositor approaches (future work)

---

## Notes

- All test variants preserve the core resolution control mechanisms:
  - VRLink XR activity start mode: `XR_ACTIVITY_START_MODE_FULL_SPACE_UNMANAGED`
  - SDL metrics override: `GxrSurfaceCallback.applyManagedPanelMetrics()`
  - Controller velocity layer: Unchanged
  
- Only the overlay mechanism is varied; this ensures causation testing is clean

- Screenshot comparison is important: log entries might show same swapchain size, but visual rendering might differ if GPU/display has overlay-specific behavior

- Save all logs and screenshots; they form the evidence basis for final decision
