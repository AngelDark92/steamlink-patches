package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

private data class FecDuplicateReservationLayout(
    val version: String, val code: String, val size: Int, val buildId: String,
    val functionOffset: Int, val instruction: Int, val functionHash: String,
)

private val fecDuplicateReservationLayouts = listOf(
    FecDuplicateReservationLayout("2.0.22", "5002322", 2_283_400,
        "585d88d646a8c6efe94bdd9fc6c9dbbc68fc13ba", 0x167050, 0x167094,
        "e1c49caa38c49b97f2151263b39d9e7777a394bfda36faa1186c1119fae7bc14"),
    FecDuplicateReservationLayout("2.0.23", "5002363", 2_292_008,
        "c31bb979123b76736930d3c820d8d0619fb5bed2", 0x167f1c, 0x167f60,
        "22eb5623fd4bc06b3464dcadaf26c160cd4b460c3e0f428ebefeb72052700121"),
)
private const val FEC_ACCEPT_PACKET_SIZE = 0x294
private val fecDuplicateReservationOriginal = byteArrayOf(0x28, 0x01, 0x00, 0x34)
private val fecDuplicateReservationGuarded = byteArrayOf(0x1f, 0x20, 0x03, 0xd5.toByte())

internal fun isFecDuplicateReservationGuardBuild(version: String, code: String): Boolean =
    fecDuplicateReservationLayouts.any { it.version == version && it.code == code }

private fun fecDuplicateReservationRequire(condition: Boolean, message: String) {
    if (!condition) throw PatchException("FEC duplicate reservation guard: $message")
}

/** Validate the file-backed executable mapping, including after Visual Delay adds a PT_LOAD. */
private fun fecDuplicateReservationFunctionOffset(bytes: ByteArray, address: Int, size: Int): Int {
    val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    fun range(offset: Long, length: Long): Int {
        fecDuplicateReservationRequire(offset >= 0 && length >= 0 && offset <= bytes.size.toLong() - length,
            "ELF range outside file")
        return offset.toInt()
    }
    fun u16(offset: Int) = buffer.getShort(range(offset.toLong(), 2)).toInt() and 0xffff
    fun u32(offset: Int) = buffer.getInt(range(offset.toLong(), 4)).toLong() and 0xffffffffL
    fun u64(offset: Int) = buffer.getLong(range(offset.toLong(), 8)).also {
        fecDuplicateReservationRequire(it >= 0, "unsupported ELF 64-bit value")
    }
    fecDuplicateReservationRequire(bytes.size >= 64 && bytes.copyOfRange(0, 7).contentEquals(
        byteArrayOf(0x7f, 0x45, 0x4c, 0x46, 2, 1, 1)), "expected ELF64 little-endian library")
    fecDuplicateReservationRequire(u16(16) == 3 && u16(18) == 183 && u32(20) == 1L && u16(52) == 64,
        "expected AArch64 ET_DYN ELF header")
    fecDuplicateReservationRequire(u16(54) == 56 && u16(56) in 1..64, "unexpected program-header layout")
    val count = u16(56)
    val start = range(u64(32), count.toLong() * 56)
    val matches = mutableListOf<Int>()
    repeat(count) { index ->
        val header = start + index * 56
        val offset = u64(header + 8)
        val fileSize = u64(header + 32)
        range(offset, fileSize)
        if (u32(header) == 1L) {
            val memorySize = u64(header + 40)
            fecDuplicateReservationRequire(memorySize >= fileSize, "PT_LOAD memory size is smaller than file size")
            val relative = address.toLong() - u64(header + 16)
            if (relative < memorySize && relative > -size.toLong()) {
                // Reject partial or zero-fill overlaps too: a second segment must
                // not remap any bytes of this verified executable function.
                fecDuplicateReservationRequire(relative >= 0 && size.toLong() <= fileSize && relative <= fileSize - size,
                    "AcceptVideoPacket overlaps a partial or non-file-backed PT_LOAD")
                fecDuplicateReservationRequire(u32(header + 4) and 1L != 0L, "AcceptVideoPacket mapping is not executable")
                matches += range(offset + relative, size.toLong())
            }
        }
    }
    fecDuplicateReservationRequire(matches.size == 1, "AcceptVideoPacket lacks a unique executable PT_LOAD")
    return matches.single()
}

/** For another patch's full-function hash guard; never writes the caller's bytes. */
internal fun normalizeFecDuplicateReservationGuardInstruction(
    bytes: ByteArray, version: String, code: String,
): ByteArray {
    val layout = fecDuplicateReservationLayouts.singleOrNull { it.version == version && it.code == code }
        ?: return bytes.copyOf()
    fecDuplicateReservationRequire(bytes.size == layout.size && layout.instruction <= bytes.size - 4,
        "unexpected libvrlink_scene.so size for $version/$code")
    val instruction = bytes.copyOfRange(layout.instruction, layout.instruction + 4)
    fecDuplicateReservationRequire(instruction.contentEquals(fecDuplicateReservationOriginal) ||
        instruction.contentEquals(fecDuplicateReservationGuarded), "unexpected duplicate-check instruction for $version/$code")
    return bytes.copyOf().apply { fecDuplicateReservationOriginal.copyInto(this, layout.instruction) }
}

/** Only the accepted instruction is normalized before hashing the entire AcceptVideoPacket function. */
internal fun patchFecDuplicateReservationGuard(bytes: ByteArray, version: String, code: String): ByteArray {
    val layout = fecDuplicateReservationLayouts.singleOrNull { it.version == version && it.code == code }
        ?: return bytes.copyOf()
    fecDuplicateReservationRequire(bytes.size == layout.size, "unexpected libvrlink_scene.so size for $version/$code")
    fecDuplicateReservationRequire(fecDuplicateReservationFunctionOffset(bytes, layout.functionOffset, FEC_ACCEPT_PACKET_SIZE) == layout.functionOffset,
        "AcceptVideoPacket file/virtual mapping changed for $version/$code")
    // Inspect the raw GNU note: Visual Delay deliberately repurposes its PT_NOTE header.
    val note = ("040000001400000003000000474e5500" + layout.buildId)
        .chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    fecDuplicateReservationRequire(bytes.copyOfRange(0x2d0, 0x2d0 + note.size).contentEquals(note),
        "GNU build ID does not match $version/$code")
    val instruction = bytes.copyOfRange(layout.instruction, layout.instruction + 4)
    fecDuplicateReservationRequire(instruction.contentEquals(fecDuplicateReservationOriginal) || instruction.contentEquals(fecDuplicateReservationGuarded),
        "unexpected duplicate-check instruction for $version/$code")
    val function = bytes.copyOfRange(layout.functionOffset, layout.functionOffset + FEC_ACCEPT_PACKET_SIZE)
    fecDuplicateReservationOriginal.copyInto(function, layout.instruction - layout.functionOffset)
    val hash = MessageDigest.getInstance("SHA-256").digest(function).joinToString("") { "%02x".format(it) }
    fecDuplicateReservationRequire(hash == layout.functionHash, "AcceptVideoPacket code guard failed for $version/$code")
    // No caller-owned data changes until every layout and code precondition passes.
    return bytes.copyOf().apply { fecDuplicateReservationGuarded.copyInto(this, layout.instruction) }
}

@Suppress("unused")
val fecDuplicateReservationGuardPatch = rawResourcePatch(
    name = "FEC duplicate reservation guard (experimental)",
    description = "For exact Steam Link 2.0.22/5002322 and 2.0.23/5002363. Runs the existing accepted/submitted-frame duplicate checks before packet-driven decoder input acquisition, including after a stream reset. Experimental; targets repeated reservations for already handled frames. Does not cover skipped-frame requests; headset validation required.",
    default = false,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL.filter { compatibility ->
        compatibility.targets.any { target ->
            target.versionCodes?.values?.any { isFecDuplicateReservationGuardBuild(target.version.orEmpty(), it.toString()) } == true
        }
    }.toTypedArray())
    execute {
        val version = packageMetadata.versionName
        val code = packageMetadata.versionCode
        if (!isFecDuplicateReservationGuardBuild(version, code)) return@execute
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        val original = file.readBytes()
        val patched = patchFecDuplicateReservationGuard(original, version, code)
        val decoderHelper = get("lib/arm64-v8a/libgxr_dbuf.so")
        if (decoderHelper.exists()) validateFecDuplicateReservationGuardDecoderHelper(decoderHelper.readBytes(), code)
        if (!patched.contentEquals(original)) file.writeBytes(patched)
    }
}
