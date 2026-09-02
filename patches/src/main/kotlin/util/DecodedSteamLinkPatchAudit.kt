package util

import app.morphe.patcher.Patcher
import app.morphe.patcher.PatcherConfig
import app.morphe.patcher.apk.ApkUtils.applyTo
import app.morphe.patcher.patch.Patch
import app.template.patches.steamlink.androidxr.ANDROID_SURFACE_TRIGGER_MANIFEST
import app.template.patches.steamlink.androidxr.appearOnTopPatch
import app.template.patches.steamlink.androidxr.controllerVelocityPatch
import app.template.patches.steamlink.androidxr.gxrFacebridgePatch
import app.template.patches.steamlink.androidxr.unrestrictedBatteryUsagePatch
import app.template.patches.steamlink.androidxr.xrCoreRuntimePatch
import app.template.patches.steamlink.androidxr.xrDeviceConfigBaselinePatch
import app.template.patches.steamlink.androidxr.xrGalaxyXrHighResolutionPatch
import app.template.patches.steamlink.androidxr.xrInputRoutingConfigPatch
import app.template.patches.steamlink.androidxr.xrLauncherBootstrapPatch
import app.template.patches.steamlink.androidxr.xrManifestCapabilityPackPatch
import app.template.patches.steamlink.binary.androidXrNativePermissionNamesPatch
import app.template.patches.steamlink.binary.forceHmdInitializationGatesPatch
import app.template.patches.steamlink.binary.forceLobbyPermissionStateGatePatch
import app.template.patches.steamlink.binary.forceStreamXrGatesPatch
import app.template.patches.steamlink.binary.hmdOnlyPatch
import app.template.patches.steamlink.binary.microphoneInputPresetPatch
import app.template.patches.steamlink.binary.oledCalibrationPatch
import app.template.patches.steamlink.binary.videoDitherPatch
import app.template.patches.steamlink.identity.changePackageNamePatch
import app.template.patches.steamlink.identity.deviceIdentityPatch
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import java.io.File
import java.util.zip.ZipFile

private data class HighResolutionFixture(
    val versionName: String,
    val versionCode: String,
    val permissionOffset: Int,
    val permissionMustBePatched: Boolean,
)

private val permissionOriginal = "ff8301d1fd7b01a9".hexBytes()
private val permissionReplacement = "20008052c0035fd6".hexBytes()

private val highResolutionFixtures = listOf(
    HighResolutionFixture("2.0.20", "5001712", 0x142c0c, true),
    HighResolutionFixture("2.0.22", "5002244", 0x1422c4, true),
    HighResolutionFixture("2.0.22", "5002296", 0x14478c, true),
    HighResolutionFixture("2.0.22", "5002313", 0x1472a8, true),
    HighResolutionFixture("2.0.22", "5002318", 0x147418, false),
    HighResolutionFixture("2.0.22", "5002322", 0x148aac, false),
)

private val publicPatchesFor5001712: List<Patch<*>> = listOf(
    androidXrNativePermissionNamesPatch,
    appearOnTopPatch,
    changePackageNamePatch,
    controllerVelocityPatch,
    deviceIdentityPatch,
    forceHmdInitializationGatesPatch,
    forceLobbyPermissionStateGatePatch,
    forceStreamXrGatesPatch,
    gxrFacebridgePatch,
    xrGalaxyXrHighResolutionPatch,
    microphoneInputPresetPatch,
    oledCalibrationPatch,
    unrestrictedBatteryUsagePatch,
    videoDitherPatch,
    hmdOnlyPatch,
    xrCoreRuntimePatch,
    xrDeviceConfigBaselinePatch,
    xrInputRoutingConfigPatch,
    xrLauncherBootstrapPatch,
    xrManifestCapabilityPackPatch,
)

/**
 * Offline integration audit for apktool-rebuilt decoded Steam Link bases.
 *
 * This deliberately does not sign, install, deploy, or contact a device. It executes every
 * public patch independently on 2.0.20/5001712, then produces 6 unsigned high-resolution APKs
 * and verifies the build-specific native permission behavior plus installed API-layer files.
 */
object DecodedSteamLinkPatchAudit {
    @JvmStatic
    fun main(args: Array<String>) {
        if (args.size == 2) {
            runIsolatedAudits(args[0], args[1])
            return
        }
        require(args.size == 4) {
            "Usage: DecodedSteamLinkPatchAudit <decoded-fixture-apk-directory> <audit-output-directory>"
        }
        runBlocking { runSingleAudit(args) }
    }
}

private fun runIsolatedAudits(fixtureDirectory: String, outputDirectory: String) {
    publicPatchesFor5001712.indices.forEach { index ->
        runAuditChild(fixtureDirectory, outputDirectory, "public", index)
    }
    highResolutionFixtures.indices.forEach { index ->
        runAuditChild(fixtureDirectory, outputDirectory, "high-resolution", index)
    }
}

private fun runAuditChild(fixtureDirectory: String, outputDirectory: String, kind: String, index: Int) {
    val javaHome = File(System.getProperty("java.home"), "bin")
    val java = File(javaHome, "java.exe").takeIf(File::isFile) ?: File(javaHome, "java")
    val command = listOf(
        java.absolutePath,
        "-cp",
        System.getProperty("java.class.path"),
        DecodedSteamLinkPatchAudit::class.java.name,
        fixtureDirectory,
        outputDirectory,
        kind,
        index.toString(),
    )
    val exitCode = ProcessBuilder(command).inheritIO().start().waitFor()
    check(exitCode == 0) { "$kind audit child $index exited with $exitCode" }
}

private suspend fun runSingleAudit(args: Array<String>) {
    val fixtureDirectory = File(args[0]).canonicalFile
    val outputDirectory = File(args[1]).canonicalFile.apply { mkdirs() }
    val index = args[3].toInt()

    when (args[2]) {
        "public" -> {
            val patch = publicPatchesFor5001712[index]
            val patchName = requireNotNull(patch.name)
            val fixture = fixtureFile(fixtureDirectory, highResolutionFixtures.first())
            require(fixture.isFile) { "Missing decoded APK fixture: $fixture" }
            val patchDirectory = File(outputDirectory, "5001712-public-${patchName.safeName()}")
            val output = File(patchDirectory, "steamlink-5001712-${patchName.safeName()}-unsigned.apk")
            configurePublicAuditOptions(patch)
            executePatch(fixture, patch, File(patchDirectory, "temporary"), output)
            verifyPublicPatchOutput(output, patch)
            println("PASS 2.0.20/5001712 public patch output: $patchName: $output")
        }

        "high-resolution" -> {
            val fixture = highResolutionFixtures[index]
            val input = fixtureFile(fixtureDirectory, fixture)
            require(input.isFile) { "Missing decoded APK fixture: $input" }
            val caseDirectory = File(outputDirectory, "high-resolution-${fixture.versionCode}")
            val output = File(caseDirectory, "steamlink-${fixture.versionCode}-high-resolution-unsigned.apk")
            output.parentFile.mkdirs()
            executePatch(input, xrGalaxyXrHighResolutionPatch, File(caseDirectory, "temporary"), output)
            verifyHighResolutionOutput(output, fixture)
            println("PASS ${fixture.versionName}/${fixture.versionCode} high-resolution output: $output")
        }

        else -> error("Unknown audit kind: ${args[2]}")
    }
}

private fun configurePublicAuditOptions(patch: Patch<*>) {
    when (patch) {
        controllerVelocityPatch -> patch.options["poseSendCadence"] = "half-2x"
        deviceIdentityPatch -> patch.options["profile"] = "meta-quest-pro"
        videoDitherPatch -> patch.options["enable"] = false
        else -> Unit
    }
}

private fun verifyPublicPatchOutput(outputApk: File, patch: Patch<*>) {
    check(outputApk.isFile && outputApk.length() > 0) { "Public patch did not emit an APK: ${patch.name}" }
    ZipFile(outputApk).use { apk ->
        val scene by lazy { apk.requireEntryBytes("lib/arm64-v8a/libvrlink_scene.so") }
        val manifest by lazy { apk.requireEntryBytes("AndroidManifest.xml") }
        val nop = "1f2003d5".hexBytes()

        when (patch) {
            androidXrNativePermissionNamesPatch -> {
                scene.requireBytesAt(0x99924, "android.permission.HAND_TRACKING".paddedAscii(36))
                scene.requireBytesAt(0xA1A7F, "android.permission.EYE_TRACKING_FINE".paddedAscii(36))
            }

            appearOnTopPatch -> {
                manifest.requireEncodedString("android.permission.SYSTEM_ALERT_WINDOW")
                apk.requireDexString("GxrOverlayBridge")
            }

            changePackageNamePatch -> {
                manifest.requireEncodedString("com.valvesoftware.steamlinkvr.gxr")
                apk.requireDexString("com.valvesoftware.steamlinkvr.gxr")
            }

            controllerVelocityPatch -> {
                apk.requireElf("lib/arm64-v8a/libgxr_controller_velocity.so")
                scene.requireBytesAt(0xF6468, "62008052".hexBytes())
                apk.requireEntryBytes(
                    "assets/openxr/1/api_layers/implicit.d/" +
                        "XR_APILAYER_local_GalaxyXR_controller_velocity.json",
                ).requireEncodedString("XR_APILAYER_local_GalaxyXR_controller_velocity")
            }

            deviceIdentityPatch -> {
                apk.requireEntryBytes("assets/config/hmd_config.json")
                    .requireEncodedString("\"sModelNumber\": \"Oculus Quest Pro\"")
            }

            forceHmdInitializationGatesPatch -> {
                scene.requireBytesAt(0xFFE20, nop)
                scene.requireBytesAt(0xFFE28, nop)
            }

            forceLobbyPermissionStateGatePatch -> scene.requireBytesAt(0x10DB10, nop)

            forceStreamXrGatesPatch -> {
                scene.requireBytesAt(0x116564, nop)
                scene.requireBytesAt(0x11656C, nop)
                scene.requireBytesAt(0x116620, nop)
            }

            gxrFacebridgePatch -> {
                apk.requireElf("lib/arm64-v8a/libgxr_face_bridge.so")
                manifest.requireEncodedString("android.permission.FACE_TRACKING")
            }

            xrGalaxyXrHighResolutionPatch -> verifyHighResolutionZip(apk, highResolutionFixtures.first())

            microphoneInputPresetPatch -> scene.requireBytesAt(0xF4584, "c1008052".hexBytes())

            oledCalibrationPatch -> scene.requireEncodedString("const float DITHER_ENABLE=1.;")

            videoDitherPatch -> scene.requireEncodedString("const float DITHER_ENABLE=0.;")

            unrestrictedBatteryUsagePatch -> {
                manifest.requireEncodedString("android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS")
                apk.requireDexString("GalaxyXRPermissionActivity")
            }

            hmdOnlyPatch -> {
                check(!scene.copyOfRange(0x1014E8, 0x1014EC).contentEquals("e20740f9".hexBytes())) {
                    "Visual Delay Fix left the 5001712 HMD hook unchanged"
                }
                listOf(0x101514, 0x101530, 0x101610, 0x101614, 0x101620).forEach { offset ->
                    check(!scene.copyOfRange(offset, offset + 4).contentEquals(
                        mapOf(
                            0x101514 to "fc01c263",
                            0x101530 to "bd002664",
                            0x101610 to "bd002a62",
                            0x101614 to "bd002e61",
                            0x101620 to "bd003263",
                        ).getValue(offset).hexBytes(),
                    )) { "Visual Delay Fix left velocity store 0x${offset.toString(16)} unchanged" }
                }
            }

            xrCoreRuntimePatch -> {
                apk.requireElf("lib/arm64-v8a/libgxr_xr_bridge.so")
                apk.requireDexString("GxrSdlBridge")
            }

            xrDeviceConfigBaselinePatch -> {
                apk.requireEntryBytes("assets/config/hmd_config.json")
                    .requireEncodedString("\"sModelNumber\": \"Galaxy XR\"")
                apk.requireEntryBytes("assets/config/default_config.json")
                    .requireEncodedString("\"ignore_microphone_muted\": false")
            }

            xrInputRoutingConfigPatch -> apk.requireEntryBytes("assets/config/ui_config.json")
                .requireEncodedString("XR_EXT_hand_interaction")

            xrLauncherBootstrapPatch -> {
                manifest.requireEncodedString("GalaxyXRPermissionActivity")
                manifest.requireEncodedString("org.khronos.openxr.intent.category.IMMERSIVE_HMD")
            }

            xrManifestCapabilityPackPatch -> {
                manifest.requireEncodedString("org.khronos.openxr.permission.OPENXR")
                manifest.requireEncodedString("android.permission.HAND_TRACKING")
            }

            else -> error("No public output invariant for ${patch.name}")
        }
    }
}

private suspend fun executePatch(
    inputApk: File,
    patch: Patch<*>,
    temporaryDirectory: File,
    outputApk: File?,
) {
    val isolatedInput = File(temporaryDirectory.parentFile, "isolated-input.apk")
    isolatedInput.parentFile.mkdirs()
    inputApk.copyTo(isolatedInput, overwrite = true)
    outputApk?.let { inputApk.copyTo(it, overwrite = true) }

    Patcher(PatcherConfig(isolatedInput, temporaryDirectory)).use { patcher ->
        patcher += setOf(patch)
        val failures = patcher().toList().filter { it.exception != null }
        check(failures.isEmpty()) {
            failures.joinToString("\n") { result ->
                "${result.patch.name}: ${result.exception?.stackTraceToString()}"
            }
        }
        val result = patcher.get()
        if (outputApk == null) {
            result.dexFiles.forEach { it.stream.close() }
        } else {
            result.applyTo(outputApk)
        }
    }
}

private fun verifyHighResolutionOutput(outputApk: File, fixture: HighResolutionFixture) {
    ZipFile(outputApk).use { apk ->
        verifyHighResolutionZip(apk, fixture)
    }
}

private fun verifyHighResolutionZip(apk: ZipFile, fixture: HighResolutionFixture) {
    apk.requireElf("lib/arm64-v8a/libgxr_ast.so")
    apk.requireEntryBytes(
        "assets/openxr/1/api_layers/implicit.d/" + ANDROID_SURFACE_TRIGGER_MANIFEST,
    ).requireEncodedString("XR_APILAYER_local_GalaxyXR_android_surface_trigger_passthrough_v1")
    apk.requireDexString("GxrResolutionProbe")

    val bytes = apk.requireEntryBytes("lib/arm64-v8a/libvrlink_scene.so")
    val actual = bytes.copyOfRange(
        fixture.permissionOffset,
        fixture.permissionOffset + permissionOriginal.size,
    )
    val expected = if (fixture.permissionMustBePatched) permissionReplacement else permissionOriginal
    check(actual.contentEquals(expected)) {
        "${fixture.versionCode}: permission routine at 0x" +
            fixture.permissionOffset.toString(16) + " was ${actual.toHex()}, expected ${expected.toHex()}"
    }
}

private fun ZipFile.requireEntryBytes(path: String): ByteArray {
    val entry = getEntry(path) ?: error("Missing APK entry: $path")
    return getInputStream(entry).use { it.readBytes() }
}

private fun ZipFile.requireElf(path: String) {
    requireEntryBytes(path).requireBytesAt(0, byteArrayOf(0x7f, 0x45, 0x4c, 0x46))
}

private fun ZipFile.requireDexString(value: String) {
    val found = entries().asSequence()
        .filter { it.name.matches(Regex("classes\\d*\\.dex")) }
        .any { entry -> getInputStream(entry).use { it.readBytes() }.containsSubsequence(value.encodeToByteArray()) }
    check(found) { "DEX string is absent: $value" }
}

private fun ByteArray.requireBytesAt(offset: Int, expected: ByteArray) {
    check(offset >= 0 && offset + expected.size <= size) { "Byte range 0x${offset.toString(16)} is outside entry" }
    val actual = copyOfRange(offset, offset + expected.size)
    check(actual.contentEquals(expected)) {
        "Bytes at 0x${offset.toString(16)} were ${actual.toHex()}, expected ${expected.toHex()}"
    }
}

private fun ByteArray.requireEncodedString(value: String) {
    val utf8 = value.encodeToByteArray()
    val utf16Le = value.toByteArray(Charsets.UTF_16LE)
    check(containsSubsequence(utf8) || containsSubsequence(utf16Le)) {
        "Encoded string is absent: $value"
    }
}

private fun ByteArray.containsSubsequence(needle: ByteArray): Boolean {
    if (needle.isEmpty()) return true
    if (needle.size > size) return false
    outer@ for (offset in 0..size - needle.size) {
        for (index in needle.indices) if (this[offset + index] != needle[index]) continue@outer
        return true
    }
    return false
}

private fun String.paddedAscii(size: Int): ByteArray = encodeToByteArray().copyOf(size)

private fun fixtureFile(directory: File, fixture: HighResolutionFixture) = File(
    directory,
    "decoded-apk-android-steamlinkvr-release-base-${fixture.versionName}-${fixture.versionCode}.apk",
)

private fun String.safeName(): String = lowercase()
    .replace(Regex("[^a-z0-9]+"), "-")
    .trim('-')

private fun String.hexBytes(): ByteArray =
    chunked(2).map { it.toInt(16).toByte() }.toByteArray()

private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }
