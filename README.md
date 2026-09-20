# Steam Link GalaxyXR Patches

[Morphe](https://morphe.software) patches for using Steam Link VR on the **Samsung Galaxy XR (SM-I610)** headset.

## What this repository contains

The patches adapt Steam Link for Android XR and provide build-specific fixes for resolution, color, microphone input, tracking, permissions, and startup. Recommended bundles select the patches appropriate for your Steam Link build. Optional face tracking works with VRCFaceTracking and the Galaxy XR LinkFT module. Exact older builds use the full GXR face bridge, while exact builds 2.0.22/5002322 and 2.0.23/5002363 use a native tongue-only bridge that preserves Valve's built-in Android XR face mappings.

This repository contains the patch source and downloadable `.mpp` patch bundles, which Morphe applies to a Steam Link VR APK (`com.valvesoftware.steamlinkvr`).

## What you need

- A Samsung Galaxy XR headset and a PC with Steam and SteamVR.
- [Morphe Manager](https://morphe.software) **1.22 or newer** on the headset, with compatibility checks enabled.
- A Steam Link VR APK matching an exact version and build listed below.
- This repository's patch bundle: [add the source to Morphe](https://morphe.software/add-source?github=AngelDark92/steamlink-patches), or download the `.mpp` from [Releases](https://github.com/AngelDark92/steamlink-patches/releases) and import it into Morphe Manager.

## 1. Get the Steam Link VR APK

### Version 2.0.23 / 5002363: Meta store with DownQ

1. On your PC, download and install [DownQ from the Chrome Web Store](https://chromewebstore.google.com/detail/downq/clocmpojdjmikkaepgkmplgooejmnchb) in **Google Chrome**.
2. Open [Steam Link on the Meta store](https://www.meta.com/experiences/steam-link/5841245619310585/) in Chrome and sign in to your Meta account.
3. Click the **Downgrade** button at the **bottom right** of the page. (you will have to login to oculus and meta to be able to download)
4. Choose the release channel if prompted, select version **2.0.23**, build **5002363**, and download its APK. Check both numbers before downloading.

<img width="2986" height="1896" alt="Screenshot 2026-09-15 115003" src="https://github.com/user-attachments/assets/3cb2c534-f4b0-46a1-80be-1854d2ab2cbf" />

**Firefox alternative:** [DownQ for Firefox](https://addons.mozilla.org/en-US/firefox/addon/downq-for-firefox/) is an **unofficial Firefox port**. Install that add-on and follow the Meta store steps above in Firefox. The Firefox download workflow has not been runtime-tested for this guide.

### Older versions: Steam depot download

On your PC, open the Steam console with `steam://open/console` in your web browser, then in Steam run the command for the version you want:

| Steam Link version / build | Steam console command |
|---|---|
| 2.0.20 / 5001712 | `download_depot 250820 250824 1108221034296079583` |
| 2.0.22 / 5002244 | `download_depot 250820 250824 634053834998054244` |

After Steam reports that the download is complete, find the APK here (adjust the path if Steam is installed elsewhere):

```text
C:\Program Files (x86)\Steam\steamapps\content\app_250820\depot_250824\drivers\vrlink\resources\android-steamlinkvr-release.apk
```

After downloading through either route, copy the APK to your headset. If you already have another listed build, select that APK in Morphe instead.

N.D.: Delete the depot folder every time you switch to a new depot or the .apk will remain the first one you have downloaded.

## 2. Select and apply the patches

1. Open Morphe Manager and load this repository's patch source or downloaded `.mpp` bundle.
2. Select the Steam Link VR APK. Keep compatibility checks enabled so Morphe can filter by both version and build number.
3. Select the **single recommended bundle** for your APK from the table below. Leave its patch options at their recommended values unless you need a specific adjustment.
4. Run patching, then install the resulting APK using Morphe Manager.
5. Launch the patched Steam Link, complete its permission prompts, and connect to SteamVR on your PC. If the Battery usage page opens, select **Unrestricted** and return to Steam Link.

Builds **2.0.22/5002296** and **2.0.22/5002313** have individually selectable patches but no automatic bundle; 5002296 is recognized only by the high-resolution patch. See the [full patch list](TECHNICAL_REFERENCE.md#full-patch-list) before selecting patches manually. Do not assume another build is compatible because it has the same version name.

### Patches loaded by each bundle

A bundle is a pure selector: selecting it loads exactly the patches listed below and performs no additional mutation of its own. Patch names match Morphe's patch list. A listed patch remains a no-op on a build where its own layout guard does not match (for example, the high-resolution fix is unavailable on 5001740).

**Galaxy XR recommended set (2.0.20/5001712)** — 17 patches:

1. Android XR native permission names
2. Force HMD initialization gates
3. Force lobby permission-state gate
4. Force stream XR gates
5. GXR face bridge (version 5002318 and below)
6. Galaxy XR high-resolution 3-projection fix
7. Microphone input preset
8. OLED color calibration
9. Unrestricted battery usage
10. Visual Delay Fix
11. XR Core Runtime
12. XR Device Config Baseline
13. XR Input Routing Config
14. Startup splash and XR launch mode (before 5002322)
15. Startup permission requests (before 5002322)
16. XR Manifest Capability Pack
17. Device identity

**Galaxy XR legacy foundation (through 2.0.22/5002244)** — the same 17 patches as the 2.0.20/5001712 bundle above (for 2.0.20/5001740 and 2.0.22/5002244).

**Galaxy XR recommended set (2.0.22/5002318)** — 9 patches:

1. Galaxy XR high-resolution 3-projection fix
2. GXR face bridge (version 5002318 and below)
3. Microphone input preset
4. Unrestricted battery usage
5. Visual Delay Fix
6. OLED color calibration
7. Device identity
8. Startup permission requests (before 5002322)
9. Startup splash and XR launch mode (before 5002322)

**Galaxy XR recommended set (2.0.22/5002322)** — 6 patches:

1. Galaxy XR high-resolution 3-projection fix
2. GXR tongue bridge (version 5002322 and above)
3. Microphone input preset
4. Unrestricted battery usage
5. Visual Delay Fix
6. OLED color calibration

**Galaxy XR recommended set (2.0.23/5002363)** — the same 6 patches as the 2.0.22/5002322 bundle above.

### Patch selection notes

- For either legacy bundle, keep **HMD identity** on **Recommended for this build**, or select **Meta Quest Pro**. If you previously saved Samsung, Stock, or Pico, change that setting to use the recommended identity.
- Each bundle's exact patch content is listed in **Patches loaded by each bundle** above. The 5002322 and 5002363 bundles load the same 6-patch set; the 5002318 bundle loads 9, using the GXR face bridge instead of the tongue bridge and adding Device identity plus the two startup adaptations; both legacy bundles load the same 17-patch set.
- **Retired hitch experiments — tested, did not work, removed from source 2026-09-19:** **Decoder input buffering (experimental)** (staged incomplete compressed frames in bounded memory before codec submission, or only observed the stock input path), **FEC duplicate reservation guard (experimental)** (ran the existing accepted/submitted-frame duplicate checks before packet-driven decoder allocation, including after a stream reset), and **UDP receive buffer (experimental)** (requested 8 MiB instead of 1 MiB for the VR UDP receive socket). All three were tested on the headset for exact 2.0.22/5002322 and 2.0.23/5002363 and did not solve the hitching; the UDP trial made freezes worse and longer. Do not re-add or re-derive them; [AGENTS.md](AGENTS.md) records what each patch exactly did, and the [tried-experiment ledger](diagnostics/steamlink-hitches/TRIED-EXPERIMENTS.md) keeps the evidence.
- On **2.0.22/5002322** and **2.0.23/5002363**, **Device identity** is available only when explicitly selected; it is not in the recommended bundle. Select **Meta Quest Pro** explicitly to test that identity. Native identity profiles populate the exact `xrvst2ue`/`xrvst2` product entries, because this build does not use `unknown` when its product entry is missing. This correction still needs a new headset run and does not fix the separate streaming regression.
- Older bundles explicitly include **Startup permission requests (before 5002322)** and **Startup splash and XR launch mode (before 5002322)**. These are separately selectable and unavailable on 5002322 and 5002363. Face/tongue and high-resolution patches do not silently select them.
- On **5002322 and 5002363**, Valve owns the launcher, splash, XR activity launch mode and tracking/microphone/Bluetooth permission requests. **Unrestricted battery usage** only opens battery settings from the stock activity. The high-resolution rendering fix remains active; the revised startup flow requires headset validation.
- High-resolution output is unavailable on **5001740**. The high-resolution patch has headset validation on **5002322**; other supported builds have static validation, with further details in the [technical reference](TECHNICAL_REFERENCE.md#compatibility-and-implementation-notes).
- **OLED color calibration (Fovea VD-Like):** the retired 10-bit/FP16 output and standalone dithering options are replaced by two mutually exclusive fovea toggles — **Fovea VD-Like Input 10 bit** and **Fovea VD-Like Input 8 bit** — which declare the assumed input depth and apply a fovea-gated VD-Like pass that always outputs 8-bit sRGB. Both off keeps the legacy calibrated path byte-for-byte. The fovea gate is a compact per-pixel weight derived from `uvmask` (the same 4-section geometry as Valve's masked alpha suffix) that bounds the dithered 10→8 pass to the high-acuity region. Adapted and validated on the decoded bases **2.0.20/5001712**, **2.0.22/5002244**, and **2.0.23/5002363** (the 3 decoded folders in this checkout); the other exact builds (5001740, 5002313, 5002318, 5002322) keep their guarded layout tables (static validation, no decoded input in this checkout). See the [colour diagnostics](diagnostics/steamlink-colour/README.md) and the [SDR10 plan](diagnostics/steamlink-colour/VD-LIKE-SDR10-IMPLEMENTATION-PLAN.md).
- **Appear on top (legacy)** and **Change package name** are optional and never recommended automatically. Do not add Appear on top to either modern bundle. Change package name allows a separate install alongside the original only on its compatible builds.

## Optional: USB streaming setup

For **2.0.23/5002363**, the [manual Galaxy XR USB setup guide](Install/USB-STREAMING.md) configures Android RNDIS and the Windows USB network adapter. **USB streaming worked with the existing APK during a short Wi-Fi-off test**, with measured USB traffic and user-confirmed image/head-tracking continuity. No automatic USB patch is provided; [Quest-equivalent NCM remains unimplemented](diagnostics/steamlink-usb/QUEST-PARITY-2026-09-16.md). Keep Wi-Fi on for initial discovery in the tested workflow. A cable connected only for ADB or file transfer is not a streaming link. Longer stability and starting entirely without Wi-Fi remain untested.

## Optional: face and tongue tracking

Install **VRCFaceTracking** and the matching [Galaxy XR LinkFT module](https://github.com/compdoge/LinkFT) on your PC. In Steam Link, enable **OSC**, **eye sharing**, and **face sharing**, and set the output port to **9015**. Recommended older-build bundles include **GXR face bridge (version 5002318 and below)**; the 2.0.22/5002322 bundle instead includes the headset-tested **GXR tongue bridge (version 5002322 and above)**. The tongue patch enables exact 2.0.22/5002322 and 2.0.23/5002363, each with its independently verified native layout. Headset results from 5002322 do not establish runtime behavior on 5002363.

## New base validation

The [2.0.23/5002363 audit](diagnostics/steamlink-5002363/README.md) records the original APK, native addresses, patch scope, option checks and remaining headset validation. Its 7 individual adaptations remain available; the decoder input buffering, UDP receive buffering and FEC duplicate reservation guard experiments were tested, did not work, and were removed from source on 2026-09-19. The 14 legacy patches remain excluded because this base already uses Valve's native Android XR paths.

## More information

See the [technical reference](TECHNICAL_REFERENCE.md) for the full patch list, implementation details, validation notes, build instructions, and links to diagnostic documentation. Release changes are listed in the [changelog](CHANGELOG.md).

## License

Licensed under the [GNU General Public License v3.0](LICENSE).
