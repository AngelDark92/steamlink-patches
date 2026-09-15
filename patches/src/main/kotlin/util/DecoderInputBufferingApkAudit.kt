package util

import app.morphe.patcher.Patcher
import app.morphe.patcher.PatcherConfig
import app.morphe.patcher.apk.ApkUtils.applyTo
import app.morphe.patcher.patch.Patch
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.steamlink.androidxr.gxrModernTongueBridgePatch
import app.template.patches.steamlink.androidxr.patchModernTongueTransport
import app.template.patches.steamlink.androidxr.unrestrictedBatteryUsagePatch
import app.template.patches.steamlink.androidxr.xrGalaxyXrHighResolutionPatch
import app.template.patches.steamlink.binary.*
import app.template.patches.steamlink.galaxyXrRecommended5002322Patch
import app.template.patches.steamlink.galaxyXrRecommended5002363Patch
import com.android.apksig.ApkVerifier
import com.google.gson.GsonBuilder
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import java.io.File
import java.security.MessageDigest
import java.util.zip.ZipFile

/** One APK case per JVM. Run with the tested MPP first, without compiled-class/resource directories. */
object DecoderInputBufferingApkAudit {
    private const val SCENE = "lib/arm64-v8a/libvrlink_scene.so"
    private const val HELPER = "lib/arm64-v8a/libgxr_dbuf.so"
    private val signature = Regex("META-INF/(MANIFEST\\.MF|[^/]+\\.(SF|RSA|DSA|EC))", RegexOption.IGNORE_CASE)

    @JvmStatic
    fun main(args: Array<String>) = runBlocking {
        require(args.size in 7..8) {
            "Usage: DecoderInputBufferingApkAudit <archive.mpp> <original.apk> <version> <code> " +
                "<observe|buffered> <baseline|standalone|bundle-first|decoder-first> <case-directory> [baseline.apk]"
        }
        val archive = File(args[0]).canonicalFile
        val input = File(args[1]).canonicalFile
        val version = args[2]
        val code = args[3]
        val mode = args[4]
        val selection = args[5]
        val directory = File(args[6]).canonicalFile
        val baseline = args.getOrNull(7)?.let { File(it).canonicalFile }
        require(archive.isFile && input.isFile)
        require(isDecoderInputBufferingBuild(version, code)) { "Unsupported exact APK pair" }
        require(mode in setOf("observe", "buffered"))
        require(selection in setOf("baseline", "standalone", "bundle-first", "decoder-first"))
        val usesBundle = selection != "standalone"
        if (selection in setOf("bundle-first", "decoder-first")) require(baseline?.isFile == true)
        require(!directory.exists()) { "Refusing to reuse an audit case directory: $directory" }
        check(File(DecoderInputBufferingApkAudit::class.java.protectionDomain.codeSource.location.toURI())
            .canonicalFile == archive) { "Audit code is not loaded from the requested MPP" }
        val patchClass = Class.forName("app.template.patches.steamlink.binary.DecoderInputBufferingPatchKt")
        check(File(patchClass.protectionDomain.codeSource.location.toURI())
            .canonicalFile == archive) { "Patch code is not loaded from the requested MPP" }

        val inputHash = input.sha256()
        val archiveHash = archive.sha256()
        val signatureResult = ApkVerifier.Builder(input).build().verify()
        check(signatureResult.isVerified) { "Original APK signature verification failed: ${signatureResult.errors}" }
        val inputScene = ZipFile(input).use { zip ->
            require(zip.getEntry(HELPER) == null) { "Input already contains experimental decoder helper" }
            zip.bytes(SCENE)
        }
        val stockHash = when (code) {
            "5002322" -> "e61baf34dfc4749d92561bab5fee47891d271607a0ce44824ff61c3e6a450c3f"
            else -> "628821feab199d7712be8a51273eb9a21ec440a7c91aa6a768cc7307a4fe22f0"
        }
        check(inputScene.sha256() == stockHash) { "Source APK native library is not the exact stock input" }
        val bundle = if (code == "5002322") galaxyXrRecommended5002322Patch else galaxyXrRecommended5002363Patch
        check(bundle.dependencies.toSet() == setOf(xrGalaxyXrHighResolutionPatch, gxrModernTongueBridgePatch,
            microphoneInputPresetPatch, unrestrictedBatteryUsagePatch, hmdOnlyPatch, oledCalibrationPatch)) {
            "Recommended bundle no longer contains exactly its existing six patches"
        }
        decoderInputBufferingPatch.options["mode"] = mode
        val selected: Array<Patch<*>> = when (selection) {
            "baseline" -> arrayOf(bundle)
            "standalone" -> arrayOf(decoderInputBufferingPatch)
            "bundle-first" -> arrayOf(bundle, decoderInputBufferingPatch)
            else -> arrayOf(decoderInputBufferingPatch, bundle)
        }
        val metadataGuard = rawResourcePatch(name = "Decoder audit exact input guard", default = false) {
            execute {
                check(packageMetadata.versionName == version && packageMetadata.versionCode == code) {
                    "APK metadata ${packageMetadata.versionName}/${packageMetadata.versionCode} does not match $version/$code"
                }
            }
        }
        val stageExecution = mutableListOf<String?>()
        val stages = selected.map { selectedPatch ->
            rawResourcePatch(name = "Decoder audit completed stage: ${selectedPatch.name}", default = false) {
                dependsOn(selectedPatch)
                execute { stageExecution += selectedPatch.name }
            }
        }
        val casePatch = rawResourcePatch(name = "Decoder archive audit: $selection", default = false) {
            dependsOn(metadataGuard, *stages.toTypedArray())
        }
        check(directory.mkdirs())
        val isolated = File(directory, "isolated-input.apk")
        val output = File(directory, "result-unsigned.apk")
        input.copyTo(isolated)
        input.copyTo(output)
        val execution = mutableListOf<String?>()
        Patcher(PatcherConfig(isolated, File(directory, "temporary"))).use { patcher ->
            patcher += setOf(casePatch)
            val results = patcher().toList()
            execution += results.map { it.patch.name }
            val failures = results.filter { it.exception != null }
            check(failures.isEmpty()) {
                failures.joinToString("\n") { "${it.patch.name}: ${it.exception?.stackTraceToString()}" }
            }
            patcher.get().applyTo(output)
        }
        check(stageExecution == selected.map { it.name }) {
            "Requested native mutation stage order was not observed: $stageExecution"
        }

        val recommendedScene = if (usesBundle) recommendedScene(inputScene, version, code) else inputScene
        val expectedScene = if (selection == "baseline") recommendedScene
            else patchDecoderInputBufferingDependency(recommendedScene, version, code)
        val actualScene = ZipFile(output).use { it.bytes(SCENE) }
        check(actualScene.contentEquals(expectedScene)) { "Native output differs from exact production helper result" }
        val comparison = if (selection == "standalone") input else baseline
        val comparedEntries = if (selection != "baseline") {
            requireNotNull(comparison)
            compareOnlyDecoderChanges(comparison, output, version, code, mode)
        } else {
            ZipFile(output).use { check(it.getEntry(HELPER) == null) { "Baseline unexpectedly installs decoder helper" } }
            0
        }
        check(input.sha256() == inputHash) { "Source APK changed during audit" }
        check(archive.sha256() == archiveHash) { "Tested MPP changed during audit" }
        val summary = linkedMapOf<String, Any?>(
            "status" to "passed", "version" to version, "version_code" to code, "mode" to mode,
            "selection" to selection, "input" to input.path, "input_sha256" to inputHash,
            "input_signature_verified" to signatureResult.isVerified,
            "archive" to archive.path, "archive_sha256" to archiveHash,
            "code_and_resources" to "packaged MPP first on classpath; no compiled-class/resource directories",
            "output" to output.path, "output_sha256" to output.sha256(),
            "scene_sha256" to actualScene.sha256(), "baseline" to comparison?.path,
            "unchanged_other_zip_entries" to comparedEntries, "signature_entries_excluded" to true,
            "native_mutation_stage_order" to stageExecution, "patch_results" to execution, "signed_or_installed" to false,
        )
        File(directory, "audit.json").writeText(GsonBuilder().setPrettyPrinting().create().toJson(summary) + "\n")
        println("PASS $version/$code $mode $selection: $comparedEntries other ZIP entries unchanged; $output")
    }

    private fun recommendedScene(input: ByteArray, version: String, code: String): ByteArray {
        var bytes = patchModernTongueTransport(input, version, code)
        bytes = patchNativeMicrophonePreset(bytes, "voice-recognition", version, code)
        bytes = patchVisualDelay(bytes, 60, version, code)
        paddedVideoShader(1.20f, 1.45f, VideoOutputPrecision.SRGB8_HIGHP).copyInto(bytes, findVideoShader(bytes))
        return setProjectionSwapchainFormat(bytes, VideoOutputPrecision.SRGB8_HIGHP, version, code)
    }

    private fun compareOnlyDecoderChanges(reference: File, output: File, version: String, code: String, mode: String): Int {
        ZipFile(reference).use { original -> ZipFile(output).use { patched ->
            val expectedScene = patchDecoderInputBufferingDependency(original.bytes(SCENE), version, code)
            check(patched.bytes(SCENE).contentEquals(expectedScene)) { "Scene differs from decoder-only change to reference" }
            val resource = "/steamlink/decoder/libgxr_dbuf_$code.so"
            val expectedHelper = requireNotNull(DecoderInputBufferingApkAudit::class.java.getResourceAsStream(resource))
                .use { configureDecoderInputBufferingHelper(it.readBytes(), mode) }
            check(patched.bytes(HELPER).contentEquals(expectedHelper)) { "Packaged native helper/config mismatch" }
            fun ZipFile.otherEntries(): Set<String> {
                val names = entries().asSequence().filterNot { it.isDirectory }.map { it.name }.toList()
                check(names.size == names.toSet().size) { "Duplicate ZIP entry names" }
                return names.filterNot { it == SCENE || it == HELPER || signature.matches(it) }.toSet()
            }
            val originalNames = original.otherEntries()
            val outputNames = patched.otherEntries()
            check(originalNames == outputNames) {
                "Unexpected APK entries: added=${outputNames - originalNames}; removed=${originalNames - outputNames}"
            }
            originalNames.forEach { name ->
                check(original.bytes(name).contentEquals(patched.bytes(name))) { "Unrelated ZIP entry changed: $name" }
            }
            return originalNames.size
        } }
    }

    private fun ZipFile.bytes(name: String): ByteArray = getInputStream(requireNotNull(getEntry(name)) {
        "Missing ZIP entry: $name"
    }).use { it.readBytes() }

    private fun ByteArray.sha256(): String = MessageDigest.getInstance("SHA-256")
        .digest(this).joinToString("") { "%02x".format(it) }

    private fun File.sha256(): String = inputStream().use { input ->
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(1024 * 1024)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) break
            digest.update(buffer, 0, count)
        }
        digest.digest().joinToString("") { "%02x".format(it) }
    }
}
