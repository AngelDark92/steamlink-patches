# Controller HAL pose layer (2026-10-04)

Source of `libgxr_controller_hal_pose.so` and `extensions/controller-hal-pose.mpe`, installed
by the opt-in patch **Controller tracking from the controller HAL through Shizuku
(experimental)**.

## What it changes

The Galaxy XR runtime hands an application one controller pose per display frame (see the
[extrapolation layer notes](../controller-extrapolation-layer/README.md)). The controller
HAL (`vendor.samsung.hardware.secxrcontroller.ISecXRController/default`) holds more: it
fuses the controller's IMU and answers `getPoseAtTimestamp` (transaction 18) with a pose
predicted for the requested time. The HAL answers a process with shell rights; an
application cannot look it up. So the call is made from a
[Shizuku](https://github.com/RikkaApps/Shizuku) user service:

- The shared [Shizuku bridge](../shizuku-bridge/README.md) binds the user service;
  `java/gxr/pose/PoseBridge` hands its binder to the layer.
- `java/gxr/pose/PoseService` runs in the user service process with shell rights and returns
  the HAL's poses of both controllers for one requested time, one HAL call per controller.
  The HAL also has `getDualPoseAtTimestamp` (transaction 19), but over binder it answers
  with a copy of the last single reply in both poses (checked on tracked controllers); the
  system controller service gets its dual poses through the HAL's message queues.
- `src/controller_hal_pose_layer.cpp` is an OpenXR API layer that wraps `xrLocateSpace` for
  the action spaces of VRLink's controller pose action (`pamir-stream-pose`) and reports the
  HAL's pose and velocities in place of the runtime's. Other spaces and hand tracking are
  not touched.

Without Shizuku, without its permission, or while a controller is not tracked, the runtime's
pose is reported unchanged, which is the stock behaviour.

## Measurements (Galaxy XR SM-I610, Steam Link 2.0.23/5002363, 2026-10-04)

The HAL, polled from the shell:

| | Result |
|---|---|
| Distinct poses in 1000 polls per second | 840-990, also with the requested time held for 100 ms |
| Pose differs between "now" and "now + 30 ms" | 90-100 % of pairs |
| One call | 0.3-0.6 ms |
| Clock of the requested time | `CLOCK_MONOTONIC`, nanoseconds |

A request with a time from another clock (hundreds of seconds ahead) made the controllers
freeze in the headset even at 90 calls per second. With the monotonic clock about 1200 HAL
calls per second during a stream caused no freezing.

The HAL's pose against the runtime's, from a 78 s recording of both in a stream:

- On still controllers `runtime grip pose = A * HAL pose * B`. `B` is a pitch of 42.25
  degrees about +X with no offset, the same for both hands (residual 0.04 degrees and
  0.03 mm). `A` is the session's base space in the HAL's world, a yaw and an offset; it
  changed when the session restarted.
- In motion the runtime's pose trailed the HAL's pose for "now" by about 35 ms (regression
  on the HAL's velocities, R2 0.86). Which of the two is closer to the hand was judged by
  feel only, not against an external reference.
- At rest the HAL's pose is as quiet as the runtime's (0.04 mm, 0.02 degrees RMS); in motion
  it carries two to three times more high-frequency content, hence the filter.

## How the pose is produced

- **Base space.** `A` is learned from the runtime's own pose while the controller is nearly
  still (below 0.02 m/s and 0.1 rad/s; looser limits until it is first found), as a slow
  running average. Eight consecutive still samples more than 3 cm or 0.05 rad away replace
  it at once, which follows a recenter or a new session.
- **Reads.** A thread of the layer reads the HAL 360 times per second while VRLink is
  locating the controllers, which is VRLink's own rate (four requests per display frame);
  VRLink gets the latest read. A steady thread keeps the filter's time step even, where
  VRLink's requests come in bursts. With the rate set to 1000, 912-936 reads per second
  were reached in a stream, but the pose looked no smoother in the headset, so the default
  is 360. Each request is for
  "now" plus half a read period, the read's own duration and `debug.gxr.halpose.ahead`.
- **Filter.** Every read goes through a low-pass whose cutoff rises with the controller's
  speed, taken from the HAL's own velocities: `cutoff = base + beta * speed`. A resting hand
  is smoothed hard and a fast one barely lags (at most about 2.6 mm and 0.15 degrees with
  the defaults).

- **Velocities.** The HAL's linear and angular velocity from the same read replace the
  runtime's. Against the motion of the HAL's own poses in the recording, the linear one is in
  the HAL's world axes (5 degrees off the positions' displacement, 26-31 degrees if read as
  local to the controller) and the angular one is in the controller's axes (5 degrees, 21-25
  degrees if read as a world vector). They are reported the way VRLink's receiver reads
  them: the linear one turned into the base space, the angular one turned by the grip pitch
  and left local to the grip pose. The runtime forwards the same values unconverted and a
  frame behind, which is what the
  [velocity frame layer](../controller-velocity-frame-layer/README.md) corrects by fixed
  angles. The HAL's speeds read 13-17 % lower than the displacement of its own predicted
  poses; they are passed on unscaled, as the runtime does. Both velocities go through the
  same kind of speed-dependent low-pass as the pose. This is where the noise of the
  controller's accelerometer and gyroscope shows; the raw IMU samples themselves reach only
  the system's single reader.

- **Rest.** The HAL's own pose steps on a controller held still, more in poses the cameras
  see badly: between two reports its position moved by 0.5-5 mm and its rotation by
  0.05-0.68 degrees, at full reported confidence (the runtime's pose stepped by 0.7-4.4 mm).
  The reported pose stepped by at most 0.33 mm and 0.13 degrees. Making the pose cutoffs
  follow the smoothed velocities instead of each sample's speed was tried against the
  remaining jitter and made no visible difference in the headset, so it was dropped.
  Without Shizuku, on the runtime's pose with the same filter, the controllers jitter in
  the same poses just as much, so the jitter comes from the tracking itself.

The [extrapolation layer](../controller-extrapolation-layer/README.md) has the same filter
for the runtime's pose, and the velocity frame layer rotates the runtime's velocities; both
stand down while this layer supplies the pose and the velocities.

## Properties

`debug.gxr.halpose`, `.velocity`, `.pitch` and `.hz` are read when Steam Link starts; `.ahead` and the
filter properties are re-read every second while streaming. Logcat tag: `GxrHalPose`
(a statistics line every 5 s).

| Property | Default | Meaning |
|---|---|---|
| `debug.gxr.halpose` | on | `0` reports the runtime's pose unchanged |
| `debug.gxr.halpose.velocity` | on | `0` leaves the runtime's velocities in place |
| `debug.gxr.halpose.hz` | 360 | HAL reads per second by the layer's thread; `0` reads only when VRLink asks |
| `debug.gxr.halpose.ahead` | 1 | Milliseconds added to the requested time |
| `debug.gxr.halpose.pitch` | 42.25 | Pitch of the grip pose against the HAL's pose, degrees |
| `debug.gxr.halpose.filter` | 1 | `0` reports the HAL's pose unfiltered |
| `debug.gxr.halpose.pos.cutoff` | 3 | Position cutoff at rest, Hz |
| `debug.gxr.halpose.pos.beta` | 60 | Position cutoff added per m/s, Hz |
| `debug.gxr.halpose.rot.cutoff` | 3 | Rotation cutoff at rest, Hz |
| `debug.gxr.halpose.rot.beta` | 60 | Rotation cutoff added per rad/s, Hz |
| `debug.gxr.halpose.lin.cutoff` | 10 | Linear velocity cutoff at rest, Hz |
| `debug.gxr.halpose.lin.beta` | 40 | Linear velocity cutoff added per m/s, Hz |
| `debug.gxr.halpose.ang.cutoff` | 10 | Angular velocity cutoff at rest, Hz |
| `debug.gxr.halpose.ang.beta` | 10 | Angular velocity cutoff added per rad/s, Hz |

The pose filter values and the 1 ms were chosen by feel on the headset, and the velocity
filter values were accepted there as they are.

## Limits

- Run on 2.0.23/5002363 only. The legacy bases create the same pose action but were not run.
- The HAL's velocities were checked against the recording only, not by throwing objects in
  a game.
- The stock controller service reads the HAL 90 times per second; this layer adds two HAL
  calls per read, about 720 per second, on top.

## Build

Native layer (use a short build directory, the fetched OpenXR-SDK tree is deep):

```powershell
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$cmake = "$sdk\cmake\3.22.1\bin"
$ndk = "$sdk\ndk\28.2.13676358"
& "$cmake\cmake.exe" -S extensions/controller-hal-pose -B ../builds/steamlink-patches/extensions/controller-hal-pose/build-android -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$cmake\ninja.exe" `
    "-DCMAKE_TOOLCHAIN_FILE=$ndk\build\cmake\android.toolchain.cmake" `
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static `
    -DCMAKE_BUILD_TYPE=Release
& "$cmake\cmake.exe" --build ../builds/steamlink-patches/extensions/controller-hal-pose/build-android --target gxr_controller_hal_pose
```

Java extension (the Shizuku API itself is in the Shizuku bridge's extension):

```powershell
javac --release 8 -cp "<android.jar>" -d classes java/gxr/pose/*.java
jar cf gxr.jar -C classes gxr
d8 --release --min-api 29 --lib <android.jar> --output <dir> gxr.jar
```

Copy `libgxr_controller_hal_pose.so` to `patches/src/main/resources/steamlink/androidxr/` and
`classes.dex` to `patches/src/main/resources/extensions/controller-hal-pose.mpe`, then
update both SHA-256 values in `ControllerHalPosePatchTest`.
