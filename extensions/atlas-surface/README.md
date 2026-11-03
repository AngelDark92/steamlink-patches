# Atlas surface and swapchain enlarge (2026-10-27)

Source of the two default-off experimental OpenXR API layers:

- `libgxr_swapchain_enlarge.so` — **Swapchain enlarge (experimental)**
- `libgxr_atlas_surface.so` — **Atlas surface (experimental)**

Both target exact Steam Link **2.0.20/5001712** and **2.0.24/5002395** and add no
composition layer. They are alternatives to Alpha-0, not additions to it; the
installer rejects either order.

## Why these two experiments

The retired Surface video + trigger patch copied Valve's projection images 1:1 at
Valve's source extent and therefore could not raise detail. The next question is
whether the runtime's output sharpness is driven by the projection swapchain extent.
Chronolink's own probe reports:

> "Sharp with no overlay present = the compositor sizes from the swapchain; soft =
> from the view"

`Swapchain enlarge` is the cheapest test of that rule. `Atlas surface` is the
runtime-Surface version: it presents each projection view at the runtime's max
per-eye extent. Neither is a decoder redirect, so neither can add detail beyond the
decoded stream.

## What they change

### Swapchain enlarge

For an sRGB8 projection swapchain (sampled + color attachment, single-sample,
single-face, single-mip, array size 1-2):

1. The layer asks the runtime for a swapchain 1.5x larger, capped at 8192 and never
   below the source extent.
2. The app keeps its own viewport and `imageRect`; only the backing image grows.
3. The layer clears the enlarged images once so unused area is transparent black.
4. No projection view, layer count, format or app native code is changed.

If the enlarged create fails, the layer retries the original create and stands down.

### Atlas surface

For a frame with 2 or 3 projection layers:

1. Each view's source sub-rect is cached from `xrEndFrame`.
2. On the next release, the layer copies that sub-rect into a runtime
   `XR_KHR_android_surface_swapchain` sized to the runtime's max per-eye extent.
   The target preserves the source rect's aspect ratio and is never downscaled.
3. The existing projection view is re-pointed at the target Surface image, with
   `imageRect` set to the full target. Pose, FOV, layer flags, space and `next`
   chains are preserved.
4. No 2x2 trigger quad and no extra layer are added.

Any unsupported format, missing target, copy failure or runtime rejection leaves
the original frame untouched. The layer logs `GXRAtlasSurface`.

## Build

From the repository root:

```powershell
$cmake = "..\builds\steamlink-patches\build\tooling\cmake\data\bin\cmake.exe"
$ninja = "..\builds\steamlink-patches\build\tooling\bin\ninja.exe"
$ndk = (Resolve-Path ".android-sdk\ndk\27.2.12479018").Path
$sdk = (Resolve-Path "..\builds\steamlink-patches\extensions\controller-velocity-layer\build-android\_deps\openxr_headers-src").Path
$out = "..\builds\steamlink-patches\extensions\atlas-surface\build-android"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $cmake -S extensions\atlas-surface -B $out -G Ninja `
  "-DCMAKE_MAKE_PROGRAM=$ninja" `
  "-DCMAKE_TOOLCHAIN_FILE=$ndk\build\cmake\android.toolchain.cmake" `
  "-DOPENXR_SDK_SOURCE_DIR=$sdk" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static `
  -DCMAKE_BUILD_TYPE=Release
& $cmake --build $out --config Release
```

Copy both `.so` files to `patches/src/main/resources/steamlink/androidxr/` and update
the build IDs and SHA-256 pins in `ResolutionLayers.kt` and
`ResolutionLayerPatchTest.kt` when publishing a different payload. Gradle does not
rebuild these C++ libraries.

## Host tests

```powershell
$zig = "..\builds\steamlink-patches\build\tooling\zig\ziglang\zig.exe"
$sdk = (Resolve-Path "..\builds\steamlink-patches\extensions\controller-velocity-layer\build-android\_deps\openxr_headers-src").Path
$out = "..\builds\steamlink-patches\extensions\atlas-surface\build-host-tests"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $zig c++ -std=c++17 -O2 -I extensions\atlas-surface\src -I "$sdk\include" `
  extensions\atlas-surface\tests\layer_policy_tests.cpp -o "$out\layer_policy_tests.exe"
& "$out\layer_policy_tests.exe"
& $zig c++ -std=c++17 -O2 -I extensions\atlas-surface\src -I "$sdk\include" `
  extensions\atlas-surface\tests\layer_frame_tests.cpp -o "$out\layer_frame_tests.exe"
& "$out\layer_frame_tests.exe"
```

The tests cover target extent math (aspect preserved, never downscaled), image
ownership and timeout ordering, rect bounds, UV mapping, 2- and 3-projection frame
validation, rect capture, and pose/FOV/field preservation. They do not simulate the
vendor compositor.

## Headset A/B

Use the same base, host quality, scene and refresh for the control and the experiment:

| Run | Selection | Expected |
|---|---|---|
| Control | Alpha-0 | Known-good sharpness |
| Extent probe | Swapchain enlarge | Sharpness changes only if the runtime sizes from the swapchain |
| Surface probe | Atlas surface | Sharpness changes only if Surface presentation at max extent helps |

Record `GXRSwapchainEnlarge` and `GXRAtlasSurface` logcat, frame time, thermals,
pose/FOV alignment, black/green frames and any `surface_frame_rejected` result.
A static or host pass is not runtime proof.

## Boundary

- **Swapchain enlarge** is a presentation extent preflight. It upscales the
  composited image; it cannot add stream detail.
- **Atlas surface** is a presentation supersample. It cannot add detail beyond the
  decoder output. A real detail increase needs a host-resolution change or a
  decoder-output redirect.
- Neither layer changes codec selection, decoder configuration, host render size or
  bitrate. Neither adds a composition layer.
- Do not stack either layer with Alpha-0 or with each other. Repatch a clean source
  APK when switching.
