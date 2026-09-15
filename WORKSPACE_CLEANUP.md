# Workspace cleanup and artifact ownership

Scope: `D:\Angelo\Desktop\SteamLink-GalaxyXR-Windows-Toolkit-FULL`, including its separate repositories. Last audited: **2026-09-15**.

## Required completion rule

Cleanup is part of completing an experiment or finalizing/applying a patch. Record the outcome, exact base, evidence and runtime limits; remove disposable outputs and superseded local copies; verify retained inputs and tool references. Keep a dated failed/retired record so a failed experiment is not recommended again. Parent and repository `AGENTS.md` enforce this rule.

## Canonical files and build ownership

| Files | Owner / policy |
|---|---|
| `patches/src/main/kotlin`, `patches/src/main/resources`, `extensions/*/src` | Authoritative source and packaged resources. Preserve. |
| `patches/src/main/resources/steamlink/androidxr/*.so` | Required checked-in native payloads. **CI/Gradle does not rebuild these C++ libraries.** Native rebuild/copy/hash-update instructions are in `extensions/resolution-trace-layer/README.md`. |
| `patches/build/{libs,classes,kotlin,resources,generated,morphe,tmp}` | Generated bundles/compiler output. Remove after recording results; retain a specifically needed unaccepted experiment separately until its acceptance/retirement decision. |
| `patches/bin` | Stale IDE source/resource copies, not a source tree. Removed and ignored. Audit found 36 counterparts: 11 identical, 25 divergent stale copies; another 1 file was generated DEX. |
| `extensions/resolution-trace-layer/build-*` | Disposable CMake output. Removed and ignored, including 21 accidentally tracked files. |
| `patches-list*.json`, `patches-bundle.json`, `TECHNICAL_REFERENCE.md` | Release metadata/docs intentionally generated and committed by `.releaserc`. Preserve; regenerate from source when needed. |
| `.github/workflows/release.yml` and `.releaserc` | CI compilation, release bundle generation and publication. Normal bundle consumers use the repository's GitHub Releases/Morphe feed. Local validation remains supported. |
| `decoded-apk-*`, `build/decoded-fixture-apks`, `build/decoded-fixture-sources` | Exact-base audit inputs, not disposable output. Some fixtures are reconstructions; retaining them does not make them pristine installable APKs. |
| Root `build/` | Mixed scratch, tools, fixture inputs and evidence. Never delete wholesale. |

## Cleanup performed on 2026-09-15

| Measurement | Result |
|---|---:|
| Workspace before | 17.551 GiB (18,845,345,130 bytes) |
| Workspace after, including cleanup records | 6.894 GiB (7,402,241,511 bytes at verification) |
| Deleted artifact contents | 10.672 GiB (11,459,142,704 bytes) |
| Reviewed deletion targets / files | 59 / 9,414 |
| Tracked generated files removed | 58 |
| Retained files verified by size/mtime, excluding Git internals | 57,790 |

The folder is approximately **61% smaller**. Sizes are logical file lengths, not a filesystem allocation/free-space measurement. The approximately 16 MB of cleanup records explain most of the difference between deleted bytes and net shrinkage. 3 small A/B manifests were archived separately, and the concurrent deletion noted below is excluded from this cleanup's deletion total.

Exact allowlist, deleted-file SHA-256/byte inventory, retained-file metadata, application results, and Git status snapshots are local files in `../cleanup-records/2026-09-15/` (relative to this repository). `verification.json` records the measurement snapshot; the report itself may add a few bytes afterward.

- Removed repeated APK audit derivatives in `build/audit-5002363`, `build/startup-boundary-audit`, and the retired `build/surface-video-apk-audit`. Original APKs/fixtures and top-level logs, comparisons, disassembly, and audit scripts remain.
- Removed superseded 5002244 A/B decoded trees/APKs, archiving each `manifest.txt` under `../cleanup-records/2026-09-15/a-b-manifests`. Removed the emptied A/B output directories because the generator requires its output directory to be absent. Fixed `generateVideoOutputAb` to read the existing canonical `decoded-apk-android-steamlinkvr-release-base-2.0.22-5002244` directory.
- Removed redundant raw `arsclib-inspect-*/root` extractions, generated bundle/compiler output, obsolete Surface-video native/test binaries, old retirement/checkbox assembly scratch, and the retired underside bundle. Dated tried/retired records remain.
- Removed `patches/bin` and generated resolution-layer CMake trees. No production Kotlin, native source, packaged payload, or compatibility entry was removed.
- Removed `../VirtualDesktop/split_data`, an Apktool-generated extraction. Its 8 original/archive files match the retained APK by SHA-256; the remaining 2 files are generated decode metadata. The retained `split_data.apk` SHA-256 is `d12ceff6ffed76c6bd25feafbea085acf1c93e3678ee1ee7abeffda46cf5d281`.

Historical reports can still mention deleted output paths/hashes. These identify the artifacts used at the time; they are not promises that old APKs/bundles remain available. Rebuild from the appropriate recorded source revision to reproduce historical behavior. Retired experiments remain retired.

## Retained exceptions and their completion conditions

| Retained location | Reason / when cleanup is allowed |
|---|---|
| `build/live-hitch-20260915`, `build/decoder-buffering` | Current capture and decoder experiment evidence/artifacts. Runtime acceptance remains unresolved; clean bulky generated outputs after acceptance or retirement and retain result records. |
| `build/banding-live-*`, `build/analysis`, `build/direct-surface-native-audit`, other compact reports | Unique capture/research evidence. Summarize before removing any irreplaceable measurements. |
| `.android-sdk` | Used by local native builds; CI presence is not a replacement for these local inputs. Remove only after callers have a verified replacement SDK. |
| `build/tooling` | `extensions/decoder-input-buffering/Build-Native.ps1` uses its CMake/Ninja; `Test-Native.ps1` uses its Zig. Remove only after those dependencies are replaced. |
| `build/startup-boundary-tools`, cached compiler dependencies | `diagnostics/steamlink-5002363/Compile-CachedAudit.ps1` uses these. Existing audit documentation reports the pinned `app.morphe.patches:1.3.3` resolution blocker; this cleanup did not repair or re-test that dependency. |
| `extensions/controller-velocity-layer/build-android/_deps/openxr_headers-src` | Resolution-layer build instructions consume these downloaded OpenXR headers. Do not delete the enclosing build tree until that reference has a replacement. |
| `../Best Apks` | Supplied source APKs; decoder build tooling directly references the 5002322 and 5002363 inputs. |
| `../Tools/install/platform-tools`, `../Tools/apk-tools` | Active diagnostics/build dependencies. |
| `../Tools/install/backups`, install state, signing/pairing material | Rollback/identity inputs. Preserve. The Tools repository has unrelated pre-existing changes. |
| `../CustomHeadsetOpenVrGxR` | Separate source repository, about 43 MiB at audit time. No significant disposable outputs identified. |
| Other `../VirtualDesktop` APKs/analysis, `../galaxyxr_resources.zip` | Original analysis inputs or unique resources. Full reproduction/equivalence was not established. |
| All `.git`, `.github`, `.codex`, `.agents` folders | Repository and agent infrastructure. Preserve. |

`Lingering_Issues.txt` disappeared in a concurrent change during preflight. This cleanup did not delete or restore it; it is recorded separately from the allowlist and reclaimed-byte total.

## Regeneration

Run these from the repository root when needed, then clean their output after recording validation. Standard Gradle commands require the pinned plugin to resolve:

```powershell
.\gradlew.bat :patches:buildAndroid
.\gradlew.bat :patches:generatePatchesList -PreleaseChannel=experimental
.\gradlew.bat test :patches:auditOledDecodedCompatibility :patches:auditSteamLink2363Native
.\gradlew.bat :patches:auditDecodedSteamLinkPatches
.\gradlew.bat :patches:generateVideoOutputAb
```

The decoded patch audit requires the retained exact fixture APKs. Native build recovery is documented in `extensions/resolution-trace-layer/README.md`; cached Kotlin validation is documented in `diagnostics/steamlink-5002363/README.md`. These commands generate new output and were not run merely to refill deleted build directories.

To reconstruct the removed Virtual Desktop decode using the retained tool/input, run from the workspace root:

```powershell
java -jar Tools/apk-tools/apktool.jar d VirtualDesktop/split_data.apk -o VirtualDesktop/split_data
```

## Verification scope

### Additional cleanup: decoder pipeline v2, 2026-09-15 19:51

- Experiment status: Buffered v1 ran on exact 2.0.23/5002363 but whole-view freezes persisted; [tried record and telemetry follow-up](diagnostics/steamlink-hitches/TELEMETRY-2026-09-15.md). V2 is diagnostic instrumentation, initially awaiting user installation.
- Retained evidence: [validation receipt](diagnostics/steamlink-hitches/decoder-pipeline-v2-validation.json), 113 Kotlin test results, native pool/24 bridge/6 wrapper scenarios, 26 pristine-APK cases and 10 reapplication/rollback cases, plus exact native guard/relocation audits and current live captures.
- Removed **63 allowlisted targets, 2,794,626,624 bytes**: unsigned audit APKs and decoded temporary copies, cached compiler output, native CMake output, and duplicate MPP/catalog-work copies. All 63 targets were verified absent; 7 protected current artifact/resource/capture hashes were verified unchanged.
- Preserved current `patches/build/libs/patches-1.18.0-dev.1-decoder-pipeline-v2-local.mpp`, all 4 canonical decoder helper resources (v1 comparison modes plus v2 telemetry), pristine inputs/decoded bases, SDK/tools, source, current traces and compact reports. Regenerate with `extensions/decoder-input-buffering/Build-Native.ps1 -CopyResources` (telemetry resources only), update validated pins, then `diagnostics/steamlink-hitches/Build-DecoderExperiment.ps1`.
- Exact inventory/result: `build/decoder-buffering/telemetry-cleanup/{allowlist,result}.json`. Cleanup source: `diagnostics/steamlink-hitches/Cleanup-PipelineTelemetry.ps1`; no wholesale build-tree deletion.
- External verification delta: `CustomHeadsetOpenVrGxR/ThirdParty/json` lost its pre-existing modified status during final Git verification. No deletion target was outside `steamlink-patches`; no attempt was made to restore concurrent work. Tools status remained unchanged.
- Deferred: `build/decoder-buffering/pipeline-review-test.exe` (448,512 bytes) and `.pdb` (3,665,920 bytes). Automatic approval review rejected that earlier deletion with only “blocked by policy”; no alternate deletion mechanism was used. Retire these 2 generated files when removal is permitted. Other current diagnostic captures remain intentionally retained until the investigation ends.

### Live Observe v2 evidence retention, 2026-09-15 20:09

- Exact 2.0.23/5002363 Observe + pipeline telemetry is runtime-verified; buffering disabled. [Current findings](diagnostics/steamlink-hitches/OBSERVE-RESULTS-2026-09-15.md) record app-socket overflow, decoder starvation/recovery, and the unimplemented receive-buffer candidate for both supported bases.
- Added reusable read-only host-console and UDP-counter collectors. No new compiler output, patched APK derivative, bundle or native payload was produced in this capture phase; 0 additional bytes reclaimed.
- Retain `build/live-hitch-20260915/observe-telemetry-v2` as current unresolved diagnostic evidence: 2 unique traces (517,709,309 and 242,071,107 bytes), the installed verification APK (41,686,773 bytes), counters, logs, offline analysis and native maps. Raw evidence remains ignored because it includes private session data. Remove bulky copies only after the investigation/acceptance decision is recorded; preserve compact findings and reproduction sources.
- All capture processes finished, remote traces were removed, and `adb-cleanup.json` records 0 ADB processes/listeners at 2026-09-15 18:09:49 UTC. The previously blocked review-test EXE/PDB remain deferred for the reason already recorded above.

### Android XR and cross-PC follow-up, 2026-09-15

- [Follow-up report](diagnostics/steamlink-hitches/XR-HOST-COMPARISON-2026-09-15.md) preserves exact 2.0.20/5001712 versus 2.0.22/5002322 and 2.0.23/5002363 findings, compositor continuity, route/counter snapshots, host evidence and unresolved causes. No patch or bundle changes in this phase.
- Retain `build/live-hitch-20260915/xr-host-comparison` as current private diagnostic evidence: fresh Android/host snapshots and logs, native comparisons, and small query outputs against the existing traces. No new APK derivatives, compiler outputs or Perfetto traces were generated; 0 additional bytes reclaimed. Existing traces and exact input bases remain required.
- Read-only device inspection finished and ADB was stopped. `xr-host-comparison/adb-cleanup.json` verifies 0 processes/listeners at 2026-09-15 18:23:46 UTC. Previously deferred cleanup remains documented above; no blocked deletion was retried.

### Supplied paired-PC archives, 2026-09-15

- Incorporated both user-supplied ZIPs and `Comparison Conclusion.txt` into the [current solution assessment](diagnostics/steamlink-hitches/XR-HOST-COMPARISON-2026-09-15.md). Found a session-scoped `asyncSend` override difference; retained UDP buffer increase as an independent experiment, not a proven fix.
- ZIPs read directly without extraction or execution. Original archives/conclusion preserved unchanged. Retain compact inventories, hashes, settings timelines and exact-session recounts under `build/live-hitch-20260915/supplied-pc-archives`; no temporary decoded/APK/compiler outputs were generated and 0 bytes reclaimed.
- No ADB process started, no device/settings/patch/bundle changes. Existing deferred cleanup is unchanged.

### UDP receive-buffer experiment completion, 2026-09-15

- Built the standalone, default-off UDP receive-buffer experiment for exact 2.0.22/5002322 and 2.0.23/5002363; recommended bundles remain unchanged. Runtime effectiveness is pending. Recorded the user's unsuccessful `asyncSend=true` test in [the tried ledger](diagnostics/steamlink-hitches/TRIED-EXPERIMENTS.md) and the explicitly requested future-chat memory note.
- Validation passed: 118 Kotlin tests, 16 actual Morphe APK cases, and 4 catalog regression checks. [Canonical receipt](diagnostics/steamlink-hitches/udp-receive-buffer-validation.json) and [experiment record](diagnostics/steamlink-hitches/EXPERIMENT-2026-09-15-udp-receive-buffer.md) preserve native guards, exact inputs, hashes, limitations, and reproduction instructions.
- Removed **27 allowlisted targets, 672,229,542 bytes** at 18:50:07 UTC: unsigned audit APKs, compiler output, duplicate staged MPP and generated catalog scratch. Verified all 11 protected files unchanged. Earlier per-case temporary copies were already removed by the build script and are excluded from this measured total.
- Inventory covered 10 repositories and 150,470 files with 0 read errors; Git snapshots showed 0 repository-status differences across cleanup. Inventory, allowlist, cleanup result, test XML, case logs/receipts, and completion verification remain under `build/udp-receive-buffer/validation-20260915`.
- Retained `patches/build/libs/patches-1.18.0-dev.3-udp-receive-buffer-local.mpp`, canonical sources/resources, pristine APKs, exact decoded bases, required tooling, and current unresolved diagnostic captures. Regenerate using `diagnostics/steamlink-hitches/Build-UdpExperiment.ps1`; scoped cleanup is reproducible with `Cleanup-UdpExperiment.ps1`.
- No APK installation, ADB startup, or live host-setting change occurred. The previously blocked review-test EXE/PDB remain deferred unchanged; no blocked deletion was retried.

### Original workspace cleanup verification record

Cleanup verification checks path containment/reparse points, an explicit allowlist, preserved files and input dependencies, Git changes, and ignore rules. No APK installation, ADB command, headset test, SteamVR mutation, driver deployment, or GitHub publication is part of this cleanup.

Results: all 59 targets absent; 0 missing or altered retained files (with archived manifests accounted for); 0 unexpected tracked deletions; Tools and CustomHeadsetOpenVrGxR Git status unchanged. The original Virtual Desktop APK hash still matches. All protected infrastructure directories remain. `git diff --check`, ignore-rule probes, and PowerShell syntax checks passed. The A/B input exists with the generator's pinned native-library hash, and its output directory is absent as required. Independent cavecrew review found no remaining issues. Full Gradle compilation/APK regeneration was not run for this cleanup.
