# Gradle verification and disk cleanup

From the repository root on Windows:

```powershell
.\Verify-Build.ps1
.\Verify-Build.ps1 -Tasks ':patches:test', ':patches:auditDecodedSteamLinkPatches'
.\Verify-Build.ps1 -Tasks ':patches:auditSdr10ShaderAssemble'
```

The default runs `:patches:test` and `:patches:buildAndroid`. `-Tasks` also accepts
`build`, `check`, `assemble`, `auditOledDecodedCompatibility`, and
`auditSteamLink2363Native`, with or without the `:patches:` prefix. Use
`-GradleArguments '--offline'` for an offline dependency check. Diagnostic flags
are allowlisted; arbitrary properties, publication, `clean`, and alternate project
paths are deliberately excluded from this verification entry point. Use direct
Gradle for those workflows. Authentication uses the existing Gradle configuration.

Every invocation creates `../builds/steamlink-patches/build/verification/<unique-id>/scratch`. The Gradle
property `verificationWorkDirectory` redirects only this invocation's patch
compiler/package outputs and generated decoded/shader audit outputs. Retained
inputs use `../builds/steamlink-patches/build/`. Direct `gradlew` calls use
`../builds/steamlink-patches/gradle/{root,patches}` and external project caches.
`clean` removes only Gradle output. `-BuildRoot` can select another external
verification destination; the fixture input location stays canonical.

After Gradle exits, the wrapper preserves these results under the run directory:

| Location | Contents |
|---|---|
| `artifacts/patches/libs` | Built MPP/JAR deliverables |
| `artifacts/patches/reports`, `artifacts/patches/test-results` | Gradle/JUnit reports |
| `artifacts/audits/sdr10-shader-assemble-*` | GLSL sources and report for subsequent syntax checks |
| `gradle.log` | Gradle output and decoded/native audit assertions |
| `summary.json` | Build result, cleanup result, bytes removed, retained paths |

It then removes only that invocation's scratch directory, including unsigned
fixture APKs, isolated input copies and compiler output. On a failed build it
still saves available reports and logs and returns failure. If preservation fails,
or a junction, symlink or protected repository marker is encountered, scratch is
retained and cleanup fails visibly. A build failure remains the primary error.
`-KeepBuildOutputs` preserves scratch for debugging. Forced process termination
or power loss cannot guarantee execution of `finally`; an interrupted run can
leave its identified scratch directory for inspection.

Dependency caches, native payloads, fixture APKs, decoded bases, old builds and
other runs are not removed. Because compiler output is discarded, verification
runs compile again; dependencies remain cached. This wrapper does not replace the
release/catalog publication workflow or the cached Kotlin fallback scripts.

## Existing disk usage, 2026-09-26

The repository's root `build/` held **12,272,778,161 bytes** before this change.
The following older unsigned Morphe audit derivatives are disposable. Their
compact validation results and exact-base hashes are retained in diagnostics:

| Disposable folder | Bytes | Files | Retained evidence |
|---|---:|---:|---|
| `build/blue-noise-morphe` | 2,009,228,464 | 1,440 | `diagnostics/steamlink-blue-noise-ditering` |
| `build/blue-noise-morphe-final` | 2,008,433,220 | 1,440 | `diagnostics/steamlink-blue-noise-ditering` |
| `build/foveal-gamma-20260925/morphe` | 2,005,086,640 | 1,446 | `diagnostics/steamlink-colour/foveal-gamma-20260925` |
| `build/vd-sdr-morphe` | 1,000,840,565 | 714 | `diagnostics/steamlink-vd-hevc10` |

Total: **7,023,588,889 bytes (6.54 GiB), 5,040 files**. These folders were
inventoried with 0 tracked files and 0 descendant reparse points. They were **not
deleted** by this change. Remove only these named folders after any active audit
using them has finished. Keep the surrounding foveal-gamma directory's scripts
and receipts. Reproduction commands are in each diagnostic directory's README or
`diagnostics/steamlink-colour/FOVEAL-GAMMA-2026-09-25.md`; compatibility remains
exactly 2.0.20/5001712, 2.0.22/5002244 and 2.0.23/5002363.

Do not remove all of `build/`. Retain `decoded-fixture-apks` (288,775,563 bytes),
`decoded-fixture-sources`, `tooling` (474,559,127 bytes),
`startup-boundary-tools`, and unique captures such as `live-hitch-20260915`
(3,090,564,100 bytes). Also retain `.android-sdk` (2,618,676,016 bytes), the exact
decoded bases, original APKs, required checked-in `.so` files, repository/agent
metadata, and existing delivery MPPs. The nested OpenXR repositories under
`extensions/controller-velocity-layer/build-*` are dependencies, not disposable
compiler directories. Adjacent repositories have unrelated work and are outside
this change's deletion scope.

## Validation

```powershell
pwsh -NoProfile -File diagnostics/build-verification/Test-VerifyBuild.ps1
.\Verify-Build.ps1 -GradleArguments '--offline'
```

- **79 orchestration assertions passed in each of PowerShell 7 and Windows
  PowerShell 5.1**, using a fake Gradle process and real
  filesystem cleanup: success/failure, spaces in paths, stdout/stderr retention,
  bundles/reports/GLSL preservation, keep-output behavior, unchanged fixtures/tools,
  rejected tasks/properties, protected markers and ancestor/descendant junctions.
  Test fixtures clean themselves afterward. This is not project compilation proof.
- The real Gradle 9.6.1 offline invocation still failed during settings plugin
  resolution for `app.morphe.patches:1.3.3`. Its failure and log were preserved;
  its scratch directory was removed, with 0 artifact bytes generated.
- Independent review found no actionable issues. Full successful Morphe Gradle
  compilation, Android bundle output routing and APK audit execution remain
  unverified until that dependency resolves. No installation or runtime testing.
