# Steam Link colour-depth check

Double-click **Run-Colour-Check.cmd**. After **Ready** appears, fully close and reopen Steam Link, connect it to SteamVR, stream for 20-30 seconds, then press Enter while still connected. A stream disconnect/reconnect alone may miss startup telemetry: the projection trace logs only the first 3 successful frames of each XR session. Read **READ-ME.txt** in the new **SteamLink-Colour-...** folder on your Desktop.

For headset evidence, ADB must already be connected and authorized. The checker automatically finds the toolkit's bundled `../../../Tools/install/platform-tools/adb.exe` relative to this diagnostic folder, as well as ADB on PATH or in `%LOCALAPPDATA%\Android\Sdk\platform-tools`. Use `-AdbPath` to select an executable explicitly. Missing ADB still allows host analysis. It reads logs only: no APK installation, settings changes, log clearing, recording, or automatic restarts. It saves derived findings, not full logs.

To inspect available buffered logs immediately, without waiting or restarting anything:

```powershell
.\Check-SteamLinkColour.ps1 -Mode Snapshot
```

Snapshot is useful when startup messages are still buffered. Its findings are historical context and never prove the current connection; use a fresh capture to tie evidence to a new connection.

| Result | Meaning |
|---|---|
| PC: 10-BIT MODE OBSERVED | `driver_vrlink` selected 10-bit mode during this capture. This is negotiation evidence, not bitstream inspection. |
| Decoder: UNKNOWN | Actual decoded buffers have not been measured. `raw.pixel-format = 54` is P010 decoder configuration evidence only, not decoded-buffer readback. Main10/P010 words alone are only leads. |
| OpenXR: 8-BIT BOTTLENECK OBSERVED | A sampled projection uses 8-bit RGB storage. Original 10-bit precision cannot remain intact in that image. |
| OpenXR: HIGH-PRECISION STORAGE OBSERVED | Sampled projections use RGB10_A2 or floating point storage. Contents may still originate from 8-bit data. |
| OpenXR: UNKNOWN | Required trace is absent; this does not mean 8-bit. |
| SDR10 to sRGB8 endpoint: EXPECTED_OUTPUT | All sampled projections use GL_SRGB8_ALPHA8 (35907). With the SDR10 baseline recipe (neutral / sRGB8-highp / dither off) 8-bit sRGB is the designed output endpoint, not a failure. This never proves panel depth. |
| SDR10 to sRGB8 endpoint: NOT_BASELINE_ENDPOINT | At least one sampled projection uses another format, for example a different `outputPrecision` selection. Not an overall failure; check the selected APK options and patch state. |
| SDR10 to sRGB8 endpoint: UNKNOWN | No projection telemetry to evaluate the endpoint; absence of evidence, not a verdict. |
| Screen: UNVERIFIED | Logs do not measure compositor precision, panel drive or optical output. |

OpenXR analysis understands the repository's existing `GXRSurfaceTrigger` / `surface_trigger_frame` telemetry from the Android-surface high-resolution patch. It examines projection views and ignores the extra terminal quad. Installing or enabling that patch is not part of this tool. Older builds or uninstrumented APKs may return UNKNOWN. Frame telemetry is sampled after successful xrEndFrame; even a high-precision format does not prove the frame reached the panel. Mixed projection formats are reported conservatively if any sampled projection is 8-bit; logs do not identify how much of the visible video that projection covers.

Successful swapchain allocations are reported separately from sampled projection formats. An allocation establishes available image storage, but does not show that the image was submitted as a projection or contained streamed video. Only projection-frame telemetry supports the sampled projection result.

Capture covers new host log text and device-clock-bounded logcat for the currently running package PID. Logcat requests the `GXRSurfaceTrigger`, `CCodec`, `CCodecConfig` and `MediaCodec` tags at verbose level, excluding unrelated face telemetry from the collected output. Buffered context reads all retained matching lines without a last-N limit. Device ring-buffer overwrite remains possible, and codec-service logs outside the app PID are excluded. Use 1 connection per run. Reconnects, buffer loss, absent startup telemetry and clock changes can leave evidence incomplete. Host log rotation is discarded. ADB calls time out after 20 seconds. Select a device if multiple are connected, or use `-Package` for a renamed APK:

```powershell
.\Check-SteamLinkColour.ps1 -AdbPath 'D:\Android\platform-tools\adb.exe' -DeviceSerial 'DEVICE' -Package 'com.valvesoftware.steamlinkvr'
```

Saved logs can also be analyzed. Offline results describe those logs, not your current session:

```powershell
.\Check-SteamLinkColour.ps1 -Mode Offline -HostLog 'D:\capture\driver_vrlink.txt' -AndroidLog 'D:\capture\logcat.txt'
.\Check-SteamLinkColour.ps1 -Mode SelfTest
```

## What a full proof would require

1. Inspect an actual encoded stream's bit depth (for HEVC, sequence parameters), alongside the host negotiation. A 10-bit container can still carry values derived from 8-bit input.
2. Instrument the active Steam Link decoder's output-format change and actual output buffer: codec instance, dimensions, format/bit depth and frame identity. Android's [P010 format](https://developer.android.com/reference/android/media/MediaCodecInfo.CodecCapabilities#COLOR_FormatYUVP010) is 10-bit YCbCr 4:2:0. Capability lists, requested input profiles, opaque surface formats and HDR metadata are insufficient. Surface decoding may require native buffer instrumentation; ordinary logcat may never expose this.
3. Measure known low-bit test values at decoder output and after video shading, tied to the same frames and colour conversions. Verify the actual [OpenXR swapchain format](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrSwapchain.html). `highp` affects shader arithmetic; it cannot add storage bits to an 8-bit target.
4. Obtain compositor/display-path evidence and, for physical-screen confirmation, controlled optical measurements. Dithering can make 8-bit output look smoother. A browser gradient, headset screenshot or screen recording cannot prove 1024 distinct panel levels.

## SDR10 baseline (VD-like): explicit opt-in and proof levels

Tracked plan: [VD-LIKE-SDR10-IMPLEMENTATION-PLAN.md](VD-LIKE-SDR10-IMPLEMENTATION-PLAN.md). Per-base native evidence: [OLED-COMPATIBILITY-NATIVE.md](OLED-COMPATIBILITY-NATIVE.md).

The baseline is not a new format and not a new patch. It is the existing `oledCalibrationPatch` selected with explicit production options:

| Option | Value | Meaning |
|---|---|---|
| `profile` | `neutral` | gamma 1.00 / saturation 1.00; Valve's `_valve1_d2020d709` matrix is still applied, so this is not an identity transform end to end. |
| `outputPrecision` | `srgb8-highp` | highp shader arithmetic; GL_SRGB8_ALPHA8 projection storage. |
| `dithering` | `off` | noise-free baseline. `low`/`standard` are separate comparison rows and are never represented as proven VD behavior. |
| `use8BitOutputWhenDithering` | `false` | explicit; ignored while `dithering=off`. |

Explicitly selecting `neutral` is part of this opt-in workflow. The patch's global default stays `false` and the existing `final-balanced` profile default is unchanged. The host-side (CustomHeadsetOpenVrGxR) baseline policy is a separate opt-in and is not part of this SteamLink-side work.

Proof levels are distinct and must not be conflated:

| Level | Status on this machine |
|---|---|
| Static helper audit on hash-pinned decoded `.so` inputs | done — `Test-OledDecodedCompatibility.ps1`: 6 PASS, 5001740 BLOCKED |
| Complete shader assembly (production prefix + each base's real suffixes) | done — `Test-Sdr10ShaderAssemble.ps1`: 36 `.glsl` files |
| Structural/semantic ESSL 3.00 check of the assembled shaders | done — `glsl_validate.py` (fail-closed, stdlib-only Python): 36 PASS |
| GLSL driver compilation | NOT done — no glslangValidator/Vulkan SDK here; a structural check is not a driver compile |
| Runtime negotiation, sampled contents, dither state | not authorized — Capture/Offline modes require explicit permission and live telemetry |
| Physical panel precision | never measured by anything in this folder |

This first diagnostic locates available evidence and visible precision bottlenecks. It intentionally never awards an end-to-end 10-bit PASS. Decoder instrumentation and physical display validation remain separate work; no binary patches are made here.
