# Steam Link 2.0.20 stop diagnostics

This bundle separates APK build success from runtime compatibility. It does not modify a device unless a person separately installs or launches an APK. The collector itself never installs, launches, force-stops, grants permissions, clears logcat, runs `adb bugreport`, takes screenshots, or pulls tombstones.

## Current static findings

The decoded 2.0.20/5001712 `SDL` ABI does not match the helper currently injected by the legacy XR foundation. The static audit reports these 4 unresolved method descriptors:

- `SDL.isControllerManagerReady()Z`
- `SDLControllerManager.nativeAddJoystick(...IIIIIIZZZZ)V`
- `SDLControllerManager.onNativePadDown(III)Z`
- `SDLControllerManager.onNativePadUp(III)Z`

5001712 instead uses the older joystick registration descriptor and 2-argument pad methods. Likely device signatures are `NoSuchMethodError` or a `VerifyError` naming `GxrSdlBridge`.

There is also a separate OpenXR risk. A raw packed-byte scan finds 30 OpenXR 1.0 patterns and 0 OpenXR 1.1 patterns in the decoded 5001712 loader, while `libgxr_ast.so` has 2 OpenXR 1.1 patterns and its manifest declares 1.1. Those byte counts do not prove loader API support or negotiated runtime behavior. This remains a hypothesis until current runtime logs show loader negotiation or the isolation matrix makes the failure appear only when the high-resolution layer is added.

## Offline checks

Run the self-test without ADB:

```powershell
.\diagnostics\steamlink-crash\Invoke-SteamLinkCrashDiagnostics.ps1 -Mode SelfTest
```

Audit the exact decoded 5001712 tree and bundled smali:

```powershell
.\diagnostics\steamlink-crash\Invoke-SteamLinkCrashDiagnostics.ps1 -Mode Static
```

For CI-style failure when unresolved runtime links exist:

```powershell
.\diagnostics\steamlink-crash\Invoke-SteamLinkCrashDiagnostics.ps1 -Mode Static -FailOnFinding
```

This audit is intentionally different from Morphe compilation: it resolves injected calls against the exact target build instead of merely confirming that DEX and resources can be emitted.

## Read-only device capture

First reproduce the stop manually. Then run the collector; it does not start or stop the app:

```powershell
.\diagnostics\steamlink-crash\Invoke-SteamLinkCrashDiagnostics.ps1 `
  -Mode Capture `
  -DeviceSerial '<adb serial>' `
  -Package 'com.valvesoftware.steamlinkvr.gxr' `
  -Since (Get-Date).AddMinutes(-5) `
  -PatchedApkPath 'C:\path\to\patched.apk' `
  -PatchReceiptPath 'C:\path\to\patch-receipt.json'
```

`-PatchReceiptPath` is optional. Without it, `patchSelectionKnown` is `false`; selected patches and options cannot be reconstructed reliably from an installed APK and are never guessed.

The capture requires exact installed `2.0.20/5001712`. It records targeted package provenance, on-device APK hashes, bounded crash context, package-scoped process exit history, and a classification. It intentionally skips DropBox because its portable interface cannot reliably enforce the requested time boundary. It omits the raw device serial and redacts home paths, private IP addresses, MAC addresses, and secret-like values. Only derived targeted text is archived; unfiltered logcat is never written. The default output is a new timestamped directory in the system temporary folder; an explicit `-OutputDirectory` must not already exist.

## Isolation matrix

Build every row from the same pristine 5001712 APK, signing identity, and package-name policy. Disable every unspecified patch/default.

1. Repack only. It must launch and enter the stock flow; failure means rebuild, signing, or install provenance is already broken.
2. `XR Core Runtime` only. A launch or first-pointer failure isolates the DEX/direct-input bridge; an XR-entry failure points to the XR bridge or permission-routine change.
3. `XR Launcher Bootstrap (Home Space)` only. This includes Core. If row 2 passes and row 3 fails, inspect the manifest, `GalaxyXRPermissionActivity`, and `GxrResolutionProbe`.
4. `Galaxy XR high-resolution 3-projection fix` only. This recursively includes Launcher and Core. If row 3 passes and row 4 fails at XR creation or first frame, inspect `libgxr_ast.so`/OpenXR negotiation first.
5. Add `Device identity`.
6. Add native changes individually: permission names, HMD gates, lobby gate, then stream gates. The first regression maps the native phase and exact `libvrlink_scene.so` function.
7. Add OLED plus Video dither, then Visual Delay, then Microphone separately. These isolate first-frame shader/swapchain, first-pose, and audio-start failures.
8. Add GXR face bridge, then Controller velocity. These isolate the implicit face layer and controller layer/cadence.
9. Reproduce the full current defaults as the final anchor. Test Change package name and Appear on top only as separate side branches because they change identity or launcher policy.

For each row record the selected patches/options, output APK SHA-256, the precise stage that stopped, and the first Java fatal exception or native signal. Useful tags include `AndroidRuntime`, `SteamLinkGXR`, `GxrSdlBridge`, `SDLControllerManager`, `OpenXR-Loader`, `GXRXrBridge`, `GXRSurfaceTrigger`, and `GXRFaceBridge`.

Static evidence can identify incompatible descriptors and risky loader contracts, but the actual device-side cause remains unconfirmed until a current capture matches one of those signatures.
