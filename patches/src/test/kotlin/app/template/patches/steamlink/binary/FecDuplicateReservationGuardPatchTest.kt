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
import kotlin.test.assertNotSame
import kotlin.test.assertTrue

class FecDuplicateReservationGuardPatchTest {
    private data class Base(val version: String, val code: String, val site: Int, val functionOffset: Int)
    private val bases = listOf(Base("2.0.22", "5002322", 0x167094, 0x167050),
        Base("2.0.23", "5002363", 0x167f60, 0x167f1c))

    private fun fixture(base: Base): File {
        val relative = "decoded-apk-android-steamlinkvr-release-base-${base.version}-${base.code}/lib/arm64-v8a/libvrlink_scene.so"
        val root = generateSequence(File(System.getProperty("user.dir")).absoluteFile) { it.parentFile }
            .firstOrNull { File(it, relative).isFile }
        assumeTrue("Actual ${base.version}/${base.code} decoded native fixture is required", root != null)
        return File(requireNotNull(root), relative)
    }

    @Test
    fun standalone_metadata_is_exact_default_off_without_dependencies_or_bundle_membership() {
        val patch = fecDuplicateReservationGuardPatch
        assertEquals("FEC duplicate reservation guard (experimental)", patch.name)
        assertFalse(patch.default)
        assertTrue(patch.dependencies.isEmpty())
        assertTrue(patch.options.isEmpty())
        val pairs = patch.compatibility.orEmpty().flatMap { compatibility ->
            assertEquals(EXPERIMENTAL_COMPATIBILITY_NAME, compatibility.name)
            compatibility.targets.flatMap { target ->
                requireNotNull(target.versionCodes).values.map { target.version to it.toString() }
            }
        }.toSet()
        assertEquals(bases.map { it.version to it.code }.toSet(), pairs)
        listOf(galaxyXrRecommended5002322Patch, galaxyXrRecommended5002363Patch).forEach { bundle ->
            assertFalse(patch in bundle.dependencies)
            assertFalse(bundle.dependencies.any { patch in it.dependencies })
        }
    }

    @Test
    fun unsupported_exact_pairs_remain_unchanged_before_elf_access() {
        val bytes = byteArrayOf(1, 2, 3)
        listOf("2.0.22" to "5002363", "2.0.23" to "5002322", "2.0.22" to "5002318",
            "2.0.23" to "5002364", "2.0.20" to "5001712", "" to "").forEach { (version, code) ->
            assertFalse(isFecDuplicateReservationGuardBuild(version, code))
            assertContentEquals(bytes, patchFecDuplicateReservationGuard(bytes, version, code))
        }
        assertFailsWith<PatchException> { patchFecDuplicateReservationGuard(bytes, "2.0.23", "5002363") }
    }

    @Test
    fun actual_bases_change_only_the_duplicate_bypass_and_are_atomic_idempotent() {
        bases.forEach { base ->
            val file = fixture(base)
            val bytes = file.readBytes()
            val saved = bytes.copyOf()
            assertTrue(isFecDuplicateReservationGuardBuild(base.version, base.code))
            val patched = patchFecDuplicateReservationGuard(bytes, base.version, base.code)
            assertNotSame(bytes, patched)
            assertContentEquals(saved, bytes)
            assertEquals((base.site until base.site + 4).toList(), bytes.indices.filter { bytes[it] != patched[it] })
            assertContentEquals(byteArrayOf(0x1f, 0x20, 0x03, 0xd5.toByte()), patched.copyOfRange(base.site, base.site + 4))
            assertContentEquals(patched, patchFecDuplicateReservationGuard(patched, base.version, base.code))
            assertContentEquals(saved, file.readBytes())
        }
    }

    @Test
    fun actual_bases_reject_corrupted_size_build_id_code_and_mapping_without_mutating_input() {
        bases.forEach { base ->
            val bytes = fixture(base).readBytes()
            fun rejects(input: ByteArray) {
                val saved = input.copyOf()
                assertFailsWith<PatchException> { patchFecDuplicateReservationGuard(input, base.version, base.code) }
                assertContentEquals(saved, input)
            }
            rejects(bytes.copyOf(bytes.size - 1))
            rejects(bytes.copyOf(bytes.size + 1))
            for (offset in listOf(0, 4, 5, 6, 16, 18, 20, 52, 54, 0x2d0, 0x2e0,
                base.functionOffset, base.functionOffset + 0x293, base.site, base.site + 1, base.site + 2, base.site + 3)) {
                rejects(bytes.copyOf().apply { this[offset] = (this[offset].toInt() xor 1).toByte() })
            }
            // Idempotence must not normalize corruption elsewhere in AcceptVideoPacket.
            rejects(patchFecDuplicateReservationGuard(bytes, base.version, base.code).apply {
                this[base.functionOffset + 4] = (this[base.functionOffset + 4].toInt() xor 1).toByte()
            })
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            val phoff = buffer.getLong(32).toInt()
            val count = buffer.getShort(56).toInt() and 0xffff
            val executable = (0 until count).map { phoff + it * 56 }.single {
                buffer.getInt(it) == 1 && buffer.getInt(it + 4) and 1 != 0
            }
            fun headerChange(offset: Int, value: Long) = bytes.copyOf().apply {
                ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putLong(offset, value)
            }
            rejects(headerChange(32, Long.MAX_VALUE))
            rejects(headerChange(executable + 8, 4))
            rejects(headerChange(executable + 16, 4))
            rejects(headerChange(executable + 32, base.functionOffset.toLong()))
            rejects(headerChange(executable + 40, 1))
            rejects(bytes.copyOf().apply {
                ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putInt(executable + 4, 4)
            })
            // A second PT_LOAD claiming the function makes its address ambiguous.
            val spare = (0 until count).map { phoff + it * 56 }.first { buffer.getInt(it) == 4 }
            rejects(bytes.copyOf().apply { bytes.copyInto(this, spare, executable, executable + 56) })
            rejects(bytes.copyOf().apply {
                bytes.copyInto(this, spare, executable, executable + 56)
                ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putInt(spare + 4, 4)
            })
            // Even a 4-byte executable mapping overlapping just the edited site is ambiguous.
            rejects(bytes.copyOf().apply {
                bytes.copyInto(this, spare, executable, executable + 56)
                ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).apply {
                    putLong(spare + 8, base.site.toLong())
                    putLong(spare + 16, base.site.toLong())
                    putLong(spare + 32, 4)
                    putLong(spare + 40, 4)
                }
            })
        }
    }

    @Test
    fun normalized_guard_copy_changes_only_an_explicitly_supported_instruction() {
        bases.forEach { base ->
            val bytes = fixture(base).readBytes()
            val patched = patchFecDuplicateReservationGuard(bytes, base.version, base.code)
            val saved = patched.copyOf()
            assertContentEquals(bytes, normalizeFecDuplicateReservationGuardInstruction(patched, base.version, base.code))
            assertContentEquals(bytes, normalizeFecDuplicateReservationGuardInstruction(bytes, base.version, base.code))
            assertContentEquals(saved, patched)
            assertFailsWith<PatchException> {
                normalizeFecDuplicateReservationGuardInstruction(patched.copyOf().apply { this[base.site] = 0 },
                    base.version, base.code)
            }
        }
    }

    @Test
    fun recommended_native_mutations_and_decoder_dependency_work_in_both_orders() {
        bases.forEach { base ->
            val bytes = fixture(base).readBytes()
            fun recommended(input: ByteArray): ByteArray {
                var output = patchModernTongueTransport(input, base.version, base.code)
                output = patchNativeMicrophonePreset(output, "voice-recognition", base.version, base.code)
                output = patchVisualDelay(output, 60, base.version, base.code)
                paddedVideoShader(1.20f, 1.45f, VideoOutputPrecision.SRGB8_HIGHP)
                    .copyInto(output, findVideoShader(output))
                return setProjectionSwapchainFormat(output, VideoOutputPrecision.SRGB8_HIGHP, base.version, base.code)
            }
            val before = recommended(patchFecDuplicateReservationGuard(bytes, base.version, base.code))
            val after = patchFecDuplicateReservationGuard(recommended(bytes), base.version, base.code)
            assertContentEquals(before, after)
            assertContentEquals(after, patchFecDuplicateReservationGuard(after, base.version, base.code))
            assertContentEquals(
                patchDecoderInputBufferingDependency(after, base.version, base.code),
                patchFecDuplicateReservationGuard(patchDecoderInputBufferingDependency(recommended(bytes), base.version, base.code),
                    base.version, base.code),
            )
            assertContentEquals(
                patchUdpReceiveBuffer(after, base.version, base.code),
                patchFecDuplicateReservationGuard(patchUdpReceiveBuffer(recommended(bytes), base.version, base.code),
                    base.version, base.code),
            )
        }
    }
}
