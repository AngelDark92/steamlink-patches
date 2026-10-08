# External build folders, 2026-10-08

The project uses `D:\Angelo\Desktop\SteamLink-GalaxyXR-Windows-Toolkit-FULL\builds\steamlink-patches`.
Scripts compute the same sibling location relative to the checkout, including on CI.
The [CI repair receipt](2026-10-08-ci-repair.md) records exact-version absolute
attestation paths, release-only Gradle cleanup, and local dependency setup.

| Former project folder | External folder |
|---|---|
| `build/` | `build/` |
| `patches/build/` | `gradle/patches/` |
| Root Gradle output | `gradle/root/` |
| `.gradle/`, `.kotlin/` | `.gradle/`, `.kotlin/` |
| `extensions/controller-velocity-layer/build-android/` | Same relative path |
| `extensions/controller-velocity-layer/build-host/` | Same relative path |

Moved **6 directories, 40,428 files, 14,416,180,455 bytes** intact. All file sizes
and modification timestamps matched after same-volume moves. SHA-256 matched for
**15 retained fixture APKs and bundles**. No artifact contents were deleted during
relocation. See [the compact receipt](2026-10-08-migration.json); the full per-file
manifest and protected hashes live under external `migration/2026-10-08/`.
The final scan also relocated 4 cached Python bytecode files (**83,657 bytes**)
from diagnostics; see [the additional cache receipt](additional-cache-relocation.json).
Use `python -B` for repository helpers unless their cache prefix is explicitly external.

`build/` remains mixed: preserve its exact fixtures, tools, captures, rollback
records and unresolved evidence. Gradle `clean` uses only `gradle/` output.
Canonical decoded bases, native payloads, `.android-sdk`, `.venv`, Git/Codex/agent
metadata and tracked diagnostic records remain in the project as inputs/source.
Historical receipts retain their original paths. The cleanup readers map only
known old `build/` and `patches/build/` prefixes, then retain their hash/boundary
checks. This relocation does not revive retired experiments.
Saved Java classpaths are translated by `tools/build_paths.py` when read; 10
retained classpath entries were checked to resolve after relocation. Python cache
files for these imports also use the external build root.

The rule is recorded in root `AGENTS.md`, `.agents/AGENTS.md`, the Morphe and
cavecrew skills, all 3 `.github/agents/cavecrew-*` presets, and Copilot instructions.
Future delegated builds must receive the same output/input ownership contract.

Moved CMake caches contain absolute paths. The bundled CMake 4.4.3 supports
`--fresh`, and `extensions/decoder-input-buffering/Build-Native.ps1` uses it to
regenerate configure state. For manual CMake builds, configure a fresh external
directory or use `cmake --fresh -S <source> -B <external-output>` with CMake 3.24+;
do not reuse relocated cache state directly. Downloaded OpenXR headers and native
payload evidence remain preserved. Relocated Python tool package directories
remain usable through explicit `sys.path`; old virtualenv launchers may need
recreation before direct use. Historical resolution builders retain `/mnt/data`
input lookup and now write defaults/scratch beneath external `build/legacy-resolution`.

Validation:

- `diagnostics/build-verification/Test-VerifyBuild.ps1`: **95 assertions** passed
  in PowerShell 7 and Windows PowerShell 5.1. Uses fake Gradle; verifies external
  routing, preservation, cleanup, failures, argument rejection and junction guards.
- `diagnostics/build-layout/Test-BuildPaths.ps1`: **12 assertions** plus actual
  Gradle 9.6.1 checks of root/module output and the Kotlin persistent-dir property.
  Uses the production settings routing and Windows wrapper with Gradle's base
  plugin; `clean` preserved the fixture and no local output/cache directories arose.
- `patches/src/main/cpp/blue_noise/Test-Native.ps1`: **16/16 native contract tests**
  passed using the relocated compiler and external outputs/Zig caches.
- Changed PowerShell/Python and release JSON parsed; `git diff --check` passed.
- Both modified skills passed the skill-creator validator.
- Fresh tracked-source Gradle gate attempted:
  `gradlew.bat clean :patches:test :patches:buildAndroid :patches:generatePatchesList -PreleaseChannel=experimental --no-daemon`.
  **Blocked before project configuration:** Morphe plugin `app.morphe.patches:1.3.4`
  could not resolve. The external `validation/build-layout-20261008/fresh-gradle-gate.log`
  retains the error. This is not a full Morphe build/catalog or GitHub CI pass.

Regenerate normal bundles with `gradlew.bat buildAndroid`; use `Verify-Build.ps1`
for scoped test/Android/native/decoded audits and retained verification results.
Native reproduction commands are in the extension READMEs and build scripts.
No APK installation, ADB, SteamVR mutation, commit, push or workflow dispatch occurred.

Final validation cleanup is deferred: automatic approval review rejected removal
of the task-owned fresh checkout, source ZIP and isolated validator dependencies
with `blocked by policy` before execution. They remain outside the project under
external `validation/` (**623 files / 43,779,112 bytes**); removed 0 bytes in that
attempt. [The allowlist](validation-cleanup.json) records exact paths and the
completion condition. Test-owned layout/wrapper sandboxes were already removed
by their successful runners. Original relocated artifacts remain preserved.
