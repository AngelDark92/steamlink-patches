# Android Surface fovea experiment test

This is a separate experiment for exact Steam Link **2.0.22/5002322**. It copies the actual stereo foveal images into Android Surface swapchains and preserves their projection placement. It submits no extra terminal 2×2 quad. The GPU copies add work; success and performance improvement are not assumed. When Valve legally reuses an unchanged image, the queued Surface remains submitted and does not need another copy; a `copyPasses=0` reuse event is not omission of the projection. This experiment supports **8-bit only** and does not establish 10-bit output.

Patch a clean source APK with the experimental Surface fovea patch. Do not select the Galaxy XR high-resolution 3-projection fix or its recommended bundle: that bundle selects the production helper. The other individual patches can be selected as appropriate: GXR tongue bridge, Microphone input preset, OLED color calibration, Unrestricted battery usage, and Visual Delay Fix. In OLED color calibration select **sRGB8 highp (`srgb8-highp`)** explicitly; the current 10-bit default is unsuitable for this experiment. Keep your other settings identical between reference and experiment.

Install the resulting APK yourself. Turn off its Appear on top permission. Before the test, establish a sharpness reference using the same Steam Link build, host profile, fixed scene, refresh rate, and 8-bit color setting with the ordinary three-projection/Appear-on-top control. Then run the experiment with that permission disabled. Recording indicators, performance overlays and floating windows can independently change compositor behavior; hide them for baseline and recovery observations.

ADB must already be connected and authorized. Run from this folder:

```powershell
.\Capture-SurfaceFovea.ps1 -AdbPath 'D:\path\to\adb.exe'
```

Use `-Serial` if multiple devices are attached; use `-Package` for a renamed APK. The script checks the exact installed version and helper presence, then captures fresh device-clock-bounded logs before you manually launch Steam Link. It does not install, launch, change permissions, clear logcat, or record your screen. It temporarily pulls each installed APK for ZIP inspection and deletes only those pulled copies. Logs and JSON remain in a unique `Documents\GalaxyXR-Diagnostics\Surface-Fovea-*` folder.

Follow the prompts for baseline, palm shown, palm hidden, and optionally DFR-UI attach/detach. Rate against the same reference, not against the preceding phase. Repeat in a fresh capture for cold startup, streaming restart and focus loss/resume. Report crashes, flickering or stale/incorrect eye images as failures even if sharpness looks good.

Use asymmetric readable text or an asymmetric scene to check that neither eye is flipped, rotated, or exchanged. Move your gaze across the foveal boundary and inspect alpha seams and brightness/color changes. Move a distinct object or your head to expose stale frames. An `xrEndFrame` rejection is an experiment failure and can cause Steam Link to stop; the helper's later passthrough behavior does not guarantee a seamless recovery. Close and restart manually if needed.

`USER_REPORTED_PARITY` records your observation only. Source-transfer telemetry can establish successful sampled application submissions; it cannot measure the runtime's final composition resolution, actual display sharpness, panel precision, or continuous quality between samples. A permission grant contaminates the experiment. Window/display snapshots are included for manual inspection because unrelated overlay participation cannot be established from an AppOp alone. Treat captures as sensitive: they include device identifiers and window information.
