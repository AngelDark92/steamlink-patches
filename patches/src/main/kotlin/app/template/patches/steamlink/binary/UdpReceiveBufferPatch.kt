package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

private data class UdpReceiveLayout(
    val version: String, val code: String, val size: Int, val buildId: String,
    val restart: Int, val instruction: Int, val restartHash: String,
)

private val udpReceiveLayouts = listOf(
    UdpReceiveLayout("2.0.22", "5002322", 2_283_400,
        "585d88d646a8c6efe94bdd9fc6c9dbbc68fc13ba", 0x1742d8, 0x1745a8,
        "7032a9e1d48cf855a0838bd36bfd129f1d2d3eab5a4fc3a0b0ad7e932f305777"),
    UdpReceiveLayout("2.0.23", "5002363", 2_292_008,
        "c31bb979123b76736930d3c820d8d0619fb5bed2", 0x1754d8, 0x1757a8,
        "5c9d315b4d608e33a09c7fbec164e19dfdb9eaa4e7d2029673342ba516589351"),
)
private const val UDP_RESTART_SIZE = 0x49c
private val udpReceiveOriginal = byteArrayOf(0x08, 0x02, 0xa0.toByte(), 0x52)
private val udpReceiveExpanded = byteArrayOf(0x08, 0x10, 0xa0.toByte(), 0x52)

internal fun isUdpReceiveBufferBuild(version: String, code: String): Boolean =
    udpReceiveLayouts.any { it.version == version && it.code == code }

private fun udpReceiveRequire(condition: Boolean, message: String) {
    if (!condition) throw PatchException("UDP receive buffer: $message")
}

/** Validate the file-backed executable mapping, including after Visual Delay adds a PT_LOAD. */
private fun udpReceiveFunctionOffset(bytes: ByteArray, address: Int, size: Int): Int {
    val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    fun range(offset: Long, length: Long): Int {
        udpReceiveRequire(offset >= 0 && length >= 0 && offset <= bytes.size.toLong() - length,
            "ELF range outside file")
        return offset.toInt()
    }
    fun u16(offset: Int) = buffer.getShort(range(offset.toLong(), 2)).toInt() and 0xffff
    fun u32(offset: Int) = buffer.getInt(range(offset.toLong(), 4)).toLong() and 0xffffffffL
    fun u64(offset: Int) = buffer.getLong(range(offset.toLong(), 8)).also {
        udpReceiveRequire(it >= 0, "unsupported ELF 64-bit value")
    }
    udpReceiveRequire(bytes.size >= 64 && bytes.copyOfRange(0, 7).contentEquals(
        byteArrayOf(0x7f, 0x45, 0x4c, 0x46, 2, 1, 1)), "expected ELF64 little-endian library")
    udpReceiveRequire(u16(16) == 3 && u16(18) == 183 && u32(20) == 1L && u16(52) == 64,
        "expected AArch64 ET_DYN ELF header")
    udpReceiveRequire(u16(54) == 56 && u16(56) in 1..64, "unexpected program-header layout")
    val count = u16(56)
    val start = range(u64(32), count.toLong() * 56)
    val matches = mutableListOf<Int>()
    repeat(count) { index ->
        val header = start + index * 56
        val offset = u64(header + 8)
        val fileSize = u64(header + 32)
        range(offset, fileSize)
        if (u32(header) == 1L) {
            udpReceiveRequire(u64(header + 40) >= fileSize, "PT_LOAD memory size is smaller than file size")
            val relative = address.toLong() - u64(header + 16)
            if (relative >= 0 && size.toLong() <= fileSize && relative <= fileSize - size) {
                // Count every mapping of the address; an overlapping non-executable
                // mapping is ambiguous too, even if another mapping is executable.
                udpReceiveRequire(u32(header + 4) and 1L != 0L, "Restart mapping is not executable")
                matches += range(offset + relative, size.toLong())
            }
        }
    }
    udpReceiveRequire(matches.size == 1, "Restart lacks a unique executable PT_LOAD")
    return matches.single()
}

/** Only the accepted instruction is normalized before hashing the entire Restart function. */
internal fun patchUdpReceiveBuffer(bytes: ByteArray, version: String, code: String): ByteArray {
    val layout = udpReceiveLayouts.singleOrNull { it.version == version && it.code == code }
        ?: return bytes.copyOf()
    udpReceiveRequire(bytes.size == layout.size, "unexpected libvrlink_scene.so size for $version/$code")
    udpReceiveRequire(udpReceiveFunctionOffset(bytes, layout.restart, UDP_RESTART_SIZE) == layout.restart,
        "Restart file/virtual mapping changed for $version/$code")
    // Inspect the raw GNU note: Visual Delay deliberately repurposes its PT_NOTE header.
    val note = ("040000001400000003000000474e5500" + layout.buildId)
        .chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    udpReceiveRequire(bytes.copyOfRange(0x2d0, 0x2d0 + note.size).contentEquals(note),
        "GNU build ID does not match $version/$code")
    val instruction = bytes.copyOfRange(layout.instruction, layout.instruction + 4)
    udpReceiveRequire(instruction.contentEquals(udpReceiveOriginal) || instruction.contentEquals(udpReceiveExpanded),
        "unexpected receive-buffer instruction for $version/$code")
    val function = bytes.copyOfRange(layout.restart, layout.restart + UDP_RESTART_SIZE)
    udpReceiveOriginal.copyInto(function, layout.instruction - layout.restart)
    val hash = MessageDigest.getInstance("SHA-256").digest(function).joinToString("") { "%02x".format(it) }
    udpReceiveRequire(hash == layout.restartHash, "Restart code guard failed for $version/$code")
    // No caller-owned data changes until every layout and code precondition passes.
    return bytes.copyOf().apply { udpReceiveExpanded.copyInto(this, layout.instruction) }
}

@Suppress("unused")
val udpReceiveBufferPatch = rawResourcePatch(
    name = "UDP receive buffer (experimental)",
    description = "For exact Steam Link 2.0.22/5002322 and 2.0.23/5002363. Requests 8 MiB instead of 1 MiB for the active VR UDP receive socket to tolerate short receive pauses and packet bursts. Experimental; effective capacity and hitch improvement require headset validation.",
    default = false,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL.filter { compatibility ->
        compatibility.targets.any { target ->
            target.versionCodes?.values?.any { isUdpReceiveBufferBuild(target.version.orEmpty(), it.toString()) } == true
        }
    }.toTypedArray())
    execute {
        val version = packageMetadata.versionName
        val code = packageMetadata.versionCode
        if (!isUdpReceiveBufferBuild(version, code)) return@execute
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        val original = file.readBytes()
        val patched = patchUdpReceiveBuffer(original, version, code)
        if (!patched.contentEquals(original)) file.writeBytes(patched)
    }
}
