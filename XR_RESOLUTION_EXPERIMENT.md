# XR Resolution Permission Experiment

This experimental patch tests whether Steam Link can reproduce the high-resolution handshake without Android's **Appear on top** grant.

## Patch Selection

Enable **XR resolution permission experiment** in Morphe expert mode and select one mode. Disable the existing **Appear on top** patch. Use an APK without any older forced-resolution, rect-fix, persistent-toast, or no-overlay Python modifications.

| Order | Mode | Purpose |
|---:|---|---|
| 1 | No window control | Establish the permission-free low-resolution baseline. |
| 2 | Granted overlay control | Establish the known high-resolution result. Grant Appear on top when Android opens Settings. |
| 3 | Denied overlay enforcement probe | Confirm whether Android rejects `TYPE_APPLICATION_OVERLAY` while the app-op is denied. |
| 4 | Activity-owned application window | Test a permission-free 2x2 `TYPE_APPLICATION` window bound to SteamLink's live activity token. |
| 5 | SteamLink decor view | Test whether an extra view/surface is sufficient without another top-level window. |
| 6 | Application window immediately before VR | Test whether window timing immediately before the VR transition controls the handshake. |

Run modes 5 and 6 only after mode 4 successfully logs `probe addView success type=2` but does not match the granted control.

## Clean Run

Before each run, uninstall the previous package or clear its data. Appear on top must be off for every mode except **Granted overlay control**.

```powershell
adb shell am force-stop com.valvesoftware.steamlinkvr
adb shell pm clear com.valvesoftware.steamlinkvr
adb shell appops set com.valvesoftware.steamlinkvr SYSTEM_ALERT_WINDOW deny
adb logcat -c
adb shell appops get com.valvesoftware.steamlinkvr SYSTEM_ALERT_WINDOW
```

Clearing package data may not clear every OEM special-access setting. Confirm the setting in Android before launching the test.

## Android Evidence

Start this before launching Steam Link and leave it running through connection and VR entry:

```powershell
adb logcat -v epoch -v threadtime > android-<mode>-run1.log
```

Capture these snapshots before launch, at the computer picker, and after entering VR:

```powershell
adb shell dumpsys package com.valvesoftware.steamlinkvr > package-<mode>-run1.txt
adb shell appops get com.valvesoftware.steamlinkvr SYSTEM_ALERT_WINDOW > appops-<mode>-run1.txt
adb shell dumpsys window windows > windows-<mode>-run1.txt
```

The log must contain `SteamLinkGXR` lines reporting mode, phase, `canDraw`, token availability, and the `addView` result. In the denied probe, preserve the complete `probe addView failed` exception.

## PC Evidence

Before launching the app, note the current time and SteamVR render scale. After disconnecting, copy the SteamVR logs from:

```text
%LOCALAPPDATA%\openvr\logs
```

Also retain any Steam Link or Remote Play host logs created during the same interval. Search the paired logs for negotiated stream dimensions, encoded frame dimensions, recommended OpenXR view dimensions, and created swapchain dimensions.

Run every mode twice from a cold app start. Visual sharpness is supporting evidence; a permission-free result counts as a fix only when its PC stream dimensions and Android OpenXR/swapchain dimensions match **Granted overlay control**.

## Expected Classification

- `TYPE_APPLICATION_OVERLAY` fails while denied: Android's documented security boundary is confirmed.
- `TYPE_APPLICATION` succeeds and dimensions match the granted control: the permission-free mode can replace Appear on top.
- `TYPE_APPLICATION` succeeds but dimensions remain low: the privileged window classification or Android XR compositor policy is decisive.
- PC stream dimensions are high while Android output remains low: investigate Android XR composition/projection rather than transport negotiation.