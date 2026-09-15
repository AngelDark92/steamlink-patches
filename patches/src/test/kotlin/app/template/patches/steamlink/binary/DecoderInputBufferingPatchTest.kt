package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import app.template.patches.steamlink.androidxr.patchModernTongueTransport
import app.template.patches.steamlink.galaxyXrRecommended5002322Patch
import app.template.patches.steamlink.galaxyXrRecommended5002363Patch
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assume.assumeTrue
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class DecoderInputBufferingPatchTest {
    @Test
    fun standalone_patch_has_exact_experimental_targets_and_no_bundle_membership() {
        val patch = decoderInputBufferingPatch
        assertEquals("Decoder input buffering (experimental)", patch.name)
        assertFalse(patch.default)
        assertTrue(patch.dependencies.isEmpty())
        assertEquals("buffered", patch.options["mode"].default)
        assertEquals(mapOf("Observe" to "observe", "Buffered" to "buffered",
            "Observe + pipeline telemetry" to "observe-telemetry", "Buffered + pipeline telemetry" to "buffered-telemetry"), patch.options["mode"].values)
        assertEquals(setOf("mode"), patch.options.map { it.key }.toSet())
        val pairs = patch.compatibility.orEmpty().flatMap { compatibility ->
            assertEquals(EXPERIMENTAL_COMPATIBILITY_NAME, compatibility.name)
            compatibility.targets.flatMap { target ->
                requireNotNull(target.versionCodes).values.map { target.version to it.toString() }
            }
        }.toSet()
        assertEquals(setOf("2.0.22" to "5002322", "2.0.23" to "5002363"), pairs)
        listOf(galaxyXrRecommended5002322Patch, galaxyXrRecommended5002363Patch).forEach {
            assertEquals(6, it.dependencies.size)
            assertFalse(patch in it.dependencies)
        }
    }

    @Test
    fun packaged_native_helpers_are_present_and_support_both_mode_transitions() {
        listOf("5002322", "5002363").forEach { code ->
            val resource = "/steamlink/decoder/libgxr_dbuf_$code.so"
            val original = requireNotNull(javaClass.getResourceAsStream(resource)) {
                "Missing production native helper: $resource"
            }.use { it.readBytes() }
            verifyDecoderInputBufferingPayload(original, code)
            assertFailsWith<PatchException> {
                verifyDecoderInputBufferingPayload(original, if (code == "5002322") "5002363" else "5002322")
            }
            assertFailsWith<PatchException> {
                verifyDecoderInputBufferingPayload(original.copyOf().apply {
                    this[lastIndex] = (this[lastIndex].toInt() xor 1).toByte()
                }, code)
            }
            val magic = DECODER_BUFFER_CONFIG_MAGIC.toByteArray(Charsets.US_ASCII)
            val config = (0..original.size - magic.size).single { start ->
                magic.indices.all { original[start + it] == magic[it] }
            }
            for (mode in listOf("observe", "buffered")) {
                val configured = configureDecoderInputBufferingHelper(original, mode)
                val expected = original.copyOf().apply {
                    ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN)
                        .putInt(config + 16, if (mode == "observe") 0 else 1)
                }
                assertContentEquals(expected, configured)
                assertContentEquals(configured, configureDecoderInputBufferingHelper(configured, mode))
                for (next in listOf("observe", "buffered")) {
                    assertContentEquals(configureDecoderInputBufferingHelper(original, next),
                        configureDecoderInputBufferingHelper(configured, next))
                }
            }
            val corruptMagic = original.copyOf().apply { this[config] = 0 }
            assertFailsWith<PatchException> { configureDecoderInputBufferingHelper(corruptMagic, "buffered") }
            val corruptMode = original.copyOf().apply {
                ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putInt(config + 16, 2)
            }
            assertFailsWith<PatchException> { configureDecoderInputBufferingHelper(corruptMode, "buffered") }
            assertFailsWith<PatchException> { configureDecoderInputBufferingHelper(original, "unknown") }
        }
    }

    @Test
    fun telemetry_resources_are_exact_guarded_and_mode_changes_are_idempotent() {
        for (code in listOf("5002322", "5002363")) {
            val bytes = requireNotNull(javaClass.getResourceAsStream(decoderHelperResource(code, "observe-telemetry"))).use { it.readBytes() }
            verifyDecoderInputBufferingPayload(bytes, code, true)
            assertFailsWith<PatchException> { verifyDecoderInputBufferingPayload(bytes, code, false) }
            val magic = DECODER_TELEMETRY_CONFIG_MAGIC.toByteArray()
            val at = (0..bytes.size - magic.size).single { i -> magic.indices.all { bytes[i + it] == magic[it] } } + 16
            for ((mode, number) in listOf("observe-telemetry" to 2, "buffered-telemetry" to 3)) {
                val configured = configureDecoderInputBufferingHelper(bytes, mode)
                val expected = bytes.copyOf().apply { ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putInt(at, number) }
                assertContentEquals(expected, configured)
                assertContentEquals(configured, configureDecoderInputBufferingHelper(configured, mode))
                for (next in listOf("observe-telemetry", "buffered-telemetry")) {
                    assertContentEquals(configureDecoderInputBufferingHelper(bytes, next), configureDecoderInputBufferingHelper(configured, next))
                }
            }
            val old = requireNotNull(javaClass.getResourceAsStream(decoderHelperResource(code, "observe"))).use { it.readBytes() }
            assertFailsWith<PatchException> { configureDecoderInputBufferingHelper(old, "observe-telemetry") }
            assertFailsWith<PatchException> { configureDecoderInputBufferingHelper(bytes.copyOf().apply {
                ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putInt(at, 4)
            }, "observe-telemetry") }
        }
    }

    @Test
    fun unsupported_exact_pairs_are_no_op_before_reading_elf() {
        val input = byteArrayOf(1, 2, 3)
        listOf("2.0.22" to "5002363", "2.0.23" to "5002322", "2.0.23" to "5002364",
            "2.0.22" to "5002318", "2.0.20" to "5001712").forEach { (version, code) ->
            assertFalse(isDecoderInputBufferingBuild(version, code))
            assertContentEquals(input, patchDecoderInputBufferingDependency(input, version, code))
        }
        assertFailsWith<PatchException> { patchDecoderInputBufferingDependency(input, "2.0.23", "5002363") }
    }

    @Test
    fun production_helpers_change_only_dependency_string_and_reapply_idempotently() {
        val availableRoot = generateSequence(File(System.getProperty("user.dir")).absoluteFile) { it.parentFile }
            .firstOrNull { File(it, "decoded-apk-android-steamlinkvr-release-base-2.0.23-5002363").isDirectory }
        assumeTrue("Actual decoded APK fixtures are required for this integration audit",
            availableRoot != null)
        val root = requireNotNull(availableRoot)
        assumeTrue("Actual 5002322 decoded native fixture is required",
            File(root, "decoded-apk-android-steamlinkvr-release-base-2.0.22-5002322/lib/arm64-v8a/libvrlink_scene.so").isFile)
        listOf(Triple("2.0.22", "5002322", 0x69656), Triple("2.0.23", "5002363", 0x69925))
            .forEach { (version, code, offset) ->
                val file = File(root, "decoded-apk-android-steamlinkvr-release-base-$version-$code/lib/arm64-v8a/libvrlink_scene.so")
                val original = file.readBytes()
                val patched = patchDecoderInputBufferingDependency(original, version, code)
                val expected = original.copyOf().apply { DECODER_BUFFER_LIBRARY.toByteArray().copyInto(this, offset) }
                assertContentEquals(expected, patched)
                assertContentEquals(patched, patchDecoderInputBufferingDependency(patched, version, code))
                fun recommendedNative(input: ByteArray): ByteArray {
                    var output = patchModernTongueTransport(input, version, code)
                    output = patchNativeMicrophonePreset(output, "voice-recognition", version, code)
                    output = patchVisualDelay(output, 60, version, code)
                    paddedVideoShader(1.20f, 1.45f, VideoOutputPrecision.SRGB8_HIGHP)
                        .copyInto(output, findVideoShader(output))
                    return setProjectionSwapchainFormat(output, VideoOutputPrecision.SRGB8_HIGHP, version, code)
                }
                val recommended = recommendedNative(original)
                val addedAfter = patchDecoderInputBufferingDependency(recommended, version, code)
                val addedBefore = recommendedNative(patched)
                assertContentEquals(addedAfter, addedBefore)
                assertContentEquals(addedAfter, patchDecoderInputBufferingDependency(addedAfter, version, code))
                assertContentEquals(addedAfter, recommendedNative(addedAfter))
                assertContentEquals(original, file.readBytes())
                val corruptBuildId = original.copyOf().apply { this[0x2e0] = (this[0x2e0].toInt() xor 1).toByte() }
                assertFailsWith<PatchException> { patchDecoderInputBufferingDependency(corruptBuildId, version, code) }
                val acquire = if (code == "5002322") 0xfd110 else 0xfded8
                val corruptFunction = original.copyOf().apply { this[acquire] = (this[acquire].toInt() xor 1).toByte() }
                assertFailsWith<PatchException> { patchDecoderInputBufferingDependency(corruptFunction, version, code) }
                val missingDependency = original.copyOf().apply { this[offset] = 'x'.code.toByte() }
                assertFailsWith<PatchException> { patchDecoderInputBufferingDependency(missingDependency, version, code) }
            }
    }
}
