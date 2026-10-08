# steamlink-patches — Copilot context

Kotlin morphe-patcher patch library targeting exact `(versionName, versionCode)` Steam Link bases:
2.0.20/5001712, 2.0.22/5002244, and 2.0.23/5002363. Read repository-root `AGENTS.md` first.

## Patch authoring rules

- No inline smali injection (`addInstructions(index, "smali string")`). Crashes in morphe-patcher 1.7.0.
- Typed dexlib2 bytecode edits and exact class/method lookups are allowed when validated against every declared base.
- Use `rawResourcePatch` for raw APK files (lib/, assets/, .so binaries).
- Use `resourcePatch` with `finalize {}` for AndroidManifest.xml / XML edits.
- Every top-level patch: `@Suppress("unused")` plus the appropriate exact compatibility list from `shared/Constants.kt`.
- Preserve every existing version adaptation, global default, and build-aware dependency guard when adding a base.

## Extension DEX (smali)

- Sources: `patches/src/main/resources/steamlink/androidxr/smali/`
- Built by `assembleExtension` task; output: `../builds/steamlink-patches/gradle/patches/generated/extension-resources/extensions/extension.mpe`
- **Smali assembler flag: `-a 33`** — NEVER `-a 35` (produces DEX 040/041 container format; dexlib2 crashes).

## Build

All generated outputs, temporary checkouts and project caches always go to
`../builds/steamlink-patches/` outside this repository. Use `gradle/root` and
`gradle/patches` for Gradle output, mixed `build/` for retained fixtures/tools/evidence,
and `extensions/<name>/build-*` for native CMake. Preserve mixed inputs during
`clean`; keep canonical source/resources and committed release metadata here.
See `AGENTS.md` and `diagnostics/build-layout/README.md`; pass this contract to
delegated agents and check release asset/attestation consumers when editing builds.

```powershell
.\gradlew.bat build
.\gradlew.bat assembleExtension   # rebuild extension DEX only
```
