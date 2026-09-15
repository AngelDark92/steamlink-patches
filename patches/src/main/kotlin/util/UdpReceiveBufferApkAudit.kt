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

/** One case per JVM, with the tested MPP first and no compiled-class/resource directories. */
object UdpReceiveBufferApkAudit {
    private const val SCENE = "lib/arm64-v8a/libvrlink_scene.so"
    private const val HELPER = "lib/arm64-v8a/libgxr_dbuf.so"
    private const val OBSERVE_MODE = "observe-telemetry"
    private val signature = Regex("META-INF/(MANIFEST\\.MF|[^/]+\\.(SF|RSA|DSA|EC))", RegexOption.IGNORE_CASE)
    private val selections = setOf("baseline", "baseline-observe", "standalone", "bundle-first",
        "udp-first", "observe-udp-last", "udp-first-observe", "reapply")

    @JvmStatic
    fun main(args: Array<String>) = runBlocking {
        require(args.size in 6..7) {
            "Usage: UdpReceiveBufferApkAudit <archive.mpp> <input.apk> <version> <code> " +
                "<${selections.joinToString("|")}> <case-directory> [reference.apk]"
        }
        val archive = File(args[0]).canonicalFile
        val input = File(args[1]).canonicalFile
        val version = args[2]
        val code = args[3]
        val selection = args[4]
        val directory = File(args[5]).canonicalFile
        val suppliedReference = args.getOrNull(6)?.let { File(it).canonicalFile }
        require(archive.isFile && input.isFile)
        require((version == "2.0.22" && code == "5002322") ||
            (version == "2.0.23" && code == "5002363")) { "Unsupported exact APK pair" }
        require(selection in selections) { "Unknown audit selection: $selection" }
        val baselineCase = selection in setOf("baseline", "baseline-observe")
        val observeCase = selection in setOf("baseline-observe", "observe-udp-last", "udp-first-observe")
        val reference = when (selection) {
            "standalone", "reapply" -> input.also {
                require(suppliedReference == null || suppliedReference == input) {
                    "$selection must compare against its input APK"
                }
            }
            "baseline", "baseline-observe" -> null.also {
                require(suppliedReference == null) { "Baseline cases do not take a reference APK" }
            }
            else -> requireNotNull(suppliedReference) { "Combined cases require their baseline APK" }
        }
        require(reference == null || reference.isFile)
        require(!directory.exists()) { "Refusing to reuse an audit case directory: $directory" }
        check(File(UdpReceiveBufferApkAudit::class.java.protectionDomain.codeSource.location.toURI())
            .canonicalFile == archive) { "Audit code is not loaded from the requested MPP" }
        val patchClass = Class.forName("app.template.patches.steamlink.binary.UdpReceiveBufferPatchKt")
        check(File(patchClass.protectionDomain.codeSource.location.toURI())
            .canonicalFile == archive) { "UDP patch code is not loaded from the requested MPP" }

        val inputHash = input.sha256()
        val archiveHash = archive.sha256()
        val referenceHash = reference?.sha256()
        val signatureVerified = if (selection == "reapply") null else {
            val result = ApkVerifier.Builder(input).build().verify()
            check(result.isVerified) { "Original APK signature verification failed: ${result.errors}" }
            true
        }
        val inputScene = ZipFile(input).use { zip ->
            zip.contentNames()
            if (selection != "reapply") check(zip.getEntry(HELPER) == null) {
                "Pristine input already contains a decoder helper"
            }
            zip.bytes(SCENE)
        }
        if (selection != "reapply") {
            val stockHash = if (code == "5002322")
                "e61baf34dfc4749d92561bab5fee47891d271607a0ce44824ff61c3e6a450c3f"
            else "628821feab199d7712be8a51273eb9a21ec440a7c91aa6a768cc7307a4fe22f0"
            check(inputScene.sha256() == stockHash) { "Input scene is not the exact pristine native library" }
        }
        val bundle = if (code == "5002322") galaxyXrRecommended5002322Patch else galaxyXrRecommended5002363Patch
        check(bundle.dependencies.toSet() == setOf(xrGalaxyXrHighResolutionPatch, gxrModernTongueBridgePatch,
            microphoneInputPresetPatch, unrestrictedBatteryUsagePatch, hmdOnlyPatch, oledCalibrationPatch)) {
            "Recommended bundle no longer contains exactly its existing six patches"
        }
        check(!udpReceiveBufferPatch.default && udpReceiveBufferPatch.dependencies.isEmpty()) {
            "UDP patch must remain default-off with no dependencies"
        }
        decoderInputBufferingPatch.options["mode"] = OBSERVE_MODE
        val selected: Array<Patch<*>> = when (selection) {
            "baseline" -> arrayOf(bundle)
            "baseline-observe" -> arrayOf(bundle, decoderInputBufferingPatch)
            "standalone", "reapply" -> arrayOf(udpReceiveBufferPatch)
            "bundle-first" -> arrayOf(bundle, udpReceiveBufferPatch)
            "udp-first" -> arrayOf(udpReceiveBufferPatch, bundle)
            "observe-udp-last" -> arrayOf(bundle, decoderInputBufferingPatch, udpReceiveBufferPatch)
            else -> arrayOf(udpReceiveBufferPatch, bundle, decoderInputBufferingPatch)
        }
        val metadataGuard = rawResourcePatch(name = "UDP audit exact input guard", default = false) {
            execute {
                check(packageMetadata.versionName == version && packageMetadata.versionCode == code) {
                    "APK metadata ${packageMetadata.versionName}/${packageMetadata.versionCode} does not match $version/$code"
                }
            }
        }
        val stageExecution = mutableListOf<String?>()
        val stages = selected.map { selectedPatch ->
            rawResourcePatch(name = "UDP audit completed stage: ${selectedPatch.name}", default = false) {
                dependsOn(selectedPatch)
                execute { stageExecution += selectedPatch.name }
            }
        }
        val casePatch = rawResourcePatch(name = "UDP archive audit: $selection", default = false) {
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
        val beforeUdp = when (selection) {
            "standalone", "reapply" -> inputScene
            else -> recommendedScene(inputScene, version, code).let {
                if (observeCase) patchDecoderInputBufferingDependency(it, version, code) else it
            }
        }
        val expectedScene = if (baselineCase) beforeUdp else patchUdpReceiveBuffer(beforeUdp, version, code)
        val actualScene = ZipFile(output).use { zip ->
            zip.contentNames()
            if (baselineCase) {
                if (observeCase) {
                    val resource = decoderHelperResource(code, OBSERVE_MODE)
                    val helper = requireNotNull(UdpReceiveBufferApkAudit::class.java.getResourceAsStream(resource))
                        .use { configureDecoderInputBufferingHelper(it.readBytes(), OBSERVE_MODE) }
                    check(zip.bytes(HELPER).contentEquals(helper)) { "Observe baseline helper/config mismatch" }
                } else check(zip.getEntry(HELPER) == null) { "Bundle baseline unexpectedly installs decoder helper" }
            }
            zip.bytes(SCENE)
        }
        check(actualScene.contentEquals(expectedScene)) { "Scene differs from the exact production helper result" }
        val comparedEntries = if (baselineCase) 0 else compareOnlySceneChange(
            requireNotNull(reference), output, version, code, selection == "reapply")
        check(beforeUdp.size == actualScene.size) { "Scene size changed" }
        val changedOffsets = beforeUdp.indices.filter { beforeUdp[it] != actualScene[it] }
        if (!baselineCase) {
            val expectedOffsets = if (selection == "reapply") emptyList()
                else listOf(if (code == "5002322") 0x1745a9 else 0x1757a9)
            check(changedOffsets == expectedOffsets) { "Unexpected UDP mutation offsets: $changedOffsets" }
        }
        check(input.sha256() == inputHash) { "Input APK changed during audit" }
        check(archive.sha256() == archiveHash) { "Tested MPP changed during audit" }
        check(reference == null || reference.sha256() == referenceHash) { "Reference APK changed during audit" }
        val summary = linkedMapOf<String, Any?>(
            "status" to "passed", "version" to version, "version_code" to code,
            "selection" to selection, "input" to input.path, "input_sha256" to inputHash,
            "input_signature_verified" to signatureVerified,
            "signature_check_skipped_reason" to if (selection == "reapply") "Unsigned previous audit result" else null,
            "archive" to archive.path, "archive_sha256" to archiveHash,
            "code_and_resources" to "Packaged MPP first on classpath; patch code source verified",
            "output" to output.path, "output_sha256" to output.sha256(),
            "scene_sha256" to actualScene.sha256(), "reference" to reference?.path,
            "reference_sha256" to referenceHash, "unchanged_other_zip_entries" to comparedEntries,
            "signature_entries_excluded" to true,
            "udp_changed_offsets" to changedOffsets.map { "0x${it.toString(16)}" },
            "reapplication_all_content_identical" to (selection == "reapply"),
            "native_mutation_stage_order" to stageExecution, "patch_results" to execution,
            "signed_or_installed" to false,
        )
        File(directory, "audit.json").writeText(GsonBuilder().setPrettyPrinting().create().toJson(summary) + "\n")
        println("PASS $version/$code $selection: $comparedEntries other ZIP entries unchanged; $output")
    }

    private fun recommendedScene(input: ByteArray, version: String, code: String): ByteArray {
        var bytes = patchModernTongueTransport(input, version, code)
        bytes = patchNativeMicrophonePreset(bytes, "voice-recognition", version, code)
        bytes = patchVisualDelay(bytes, 60, version, code)
        paddedVideoShader(1.20f, 1.45f, VideoOutputPrecision.SRGB8_HIGHP).copyInto(bytes, findVideoShader(bytes))
        return setProjectionSwapchainFormat(bytes, VideoOutputPrecision.SRGB8_HIGHP, version, code)
    }

    private fun compareOnlySceneChange(reference: File, output: File, version: String, code: String,
        reapply: Boolean): Int {
        ZipFile(reference).use { original -> ZipFile(output).use { patched ->
            val originalNames = original.contentNames()
            val outputNames = patched.contentNames()
            check(originalNames == outputNames) {
                "Unexpected APK entries: added=${outputNames - originalNames}; removed=${originalNames - outputNames}"
            }
            val originalScene = original.bytes(SCENE)
            val patchedScene = patched.bytes(SCENE)
            check(patchedScene.contentEquals(patchUdpReceiveBuffer(originalScene, version, code))) {
                "Scene differs from UDP-only change to reference"
            }
            if (reapply) check(patchedScene.contentEquals(originalScene)) { "Reapplication changed the scene" }
            originalNames.filterNot { it == SCENE }.forEach { name ->
                check(original.bytes(name).contentEquals(patched.bytes(name))) { "Unrelated ZIP entry changed: $name" }
            }
            return originalNames.size - 1
        } }
    }

    private fun ZipFile.contentNames(): Set<String> {
        val names = entries().asSequence().filterNot { it.isDirectory }.map { it.name }.toList()
        check(names.size == names.toSet().size) { "Duplicate ZIP entry names" }
        return names.filterNot { signature.matches(it) }.toSet()
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
