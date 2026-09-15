package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

internal const val DECODER_BUFFER_LIBRARY = "libgxr_dbuf.so"
internal const val DECODER_BUFFER_CONFIG_MAGIC = "GXRDBUFCONFIG01!"
internal const val DECODER_TELEMETRY_CONFIG_MAGIC = "GXRDBUFCONFIG02!"
private const val STOCK_MEDIA_LIBRARY = "libmediandk.so"
private val decoderPayloadHashes = mapOf(
    "5002322" to "54b2486151a1ecda67f2bd214f892b9df34798116d125e9e3d3869bcaf64bbdc",
    "5002363" to "fc8934f90c96aef04c36117ab3ac9677e7d8f5755701d030e6e9d019ac50cd1b",
)
private val decoderTelemetryPayloadHashes = mapOf(
    "5002322" to "32f53c049ae984814cc7f6951ebc486a9f081b758d81e5259daaf0ed0923abc7",
    "5002363" to "e15dee330970891091320286c2d1db80f1e637a05aee4d2d130fd4dadba94448",
)
internal val decoderModes = mapOf("Observe" to "observe", "Buffered" to "buffered",
    "Observe + pipeline telemetry" to "observe-telemetry", "Buffered + pipeline telemetry" to "buffered-telemetry")
internal fun decoderHelperResource(code: String, mode: String): String =
    "/steamlink/decoder/libgxr_dbuf_${code}${if (mode.endsWith("-telemetry")) "_telemetry" else ""}.so"

private data class DecoderRegion(val offset: Int, val size: Int, val sha256: String)
private data class DecoderLayout(
    val version: String,
    val code: String,
    val size: Int,
    val buildId: String,
    val neededStringOffset: Int,
    val regions: List<DecoderRegion>,
)

// Exact stock ranges: codec lifecycle/input, FrameSubmitted, metadata, FEC error/assembly paths.
// These immutable regions coexist with the six recommended patches in either order.
private val decoderLayouts = listOf(
    DecoderLayout("2.0.22", "5002322", 2_283_400,
        "585d88d646a8c6efe94bdd9fc6c9dbbc68fc13ba", 0x69656, listOf(
            DecoderRegion(0xfd110, 248, "c63b5a0af6f660f061e3e571150456bd0b629c0566c03b7e5ab8db3c8444412a"),
            DecoderRegion(0xfd208, 676, "94fbb037f12beeb080e51fafcb6911633223761c8f644ae962c2f8a258d542c7"),
            DecoderRegion(0xfd81c, 320, "79ea3576b8a290854c0ef2771d9fcbd939982dd10ea1a76a634ff621a96bd646"),
            DecoderRegion(0xfd95c, 460, "d894db7fb51aeaa02347b9c41969915b5cc5c790316ddd180facb96982f413e1"),
            DecoderRegion(0xfc928, 1020, "c01385bc32c2573ee8019127d328d8ab298760033b313e0038c694dcea4c4315"),
            DecoderRegion(0x161a94, 16, "aa12a11a9a4e78a18c84ca1b9239ea10b7365cc52c7e2b733d56d5d54e7279ce"),
            DecoderRegion(0xfdb28, 212, "b96f417cb85be4fffacfb7182a4e0bc614e50f9a7e4c81d5a44776206c081b53"),
            DecoderRegion(0x166c20, 148, "212dc656050ddf091585cdb1d4ffafd40ca604f06b75a5c657004143f67c43d7"),
            DecoderRegion(0x166ecc, 384, "93a1266fa1987f1206cc4e4e415da8681cbf247a45a2f019393af7c39f514aec"),
            DecoderRegion(0x165fd8, 624, "ef25c48d78feef1babf1a2daae75b3212058cf3a4a672f5ddb16a94d66037a49"),
            DecoderRegion(0x165a50, 344, "deff9d3b7a5d43973de2bafc26c11bfcc75a0c8f58759a5fde4c6e123e07e540"),
            DecoderRegion(0x165fb8, 12, "362ac61ddebdc66ed5e436f38174790646800a2120368d7784ec8a36e8b49950"),
            DecoderRegion(0x167050, 660, "e1c49caa38c49b97f2151263b39d9e7777a394bfda36faa1186c1119fae7bc14"),
            DecoderRegion(0x166994, 652, "7812a94011b56760002eac74dc7dab3e0b4bb71adf15c085cd2e82080010066d"),
        )),
    DecoderLayout("2.0.23", "5002363", 2_292_008,
        "c31bb979123b76736930d3c820d8d0619fb5bed2", 0x69925, listOf(
            DecoderRegion(0xfded8, 248, "41ab4fe38ebf38dd0594cc2765aa08d9aa6df22b494392a0a23a6cde2d96d53e"),
            DecoderRegion(0xfdfd0, 676, "7a6932fa90e39c50a43d0f50fb8a7bc371533c0fe2cd0c5b4e8d2e109b7c7bf6"),
            DecoderRegion(0xfe5e4, 320, "3919d2279fe484ef288ed362a8f0178eb9afd316604b5498260ba7563571189f"),
            DecoderRegion(0xfe724, 460, "ba8f6a6fdcf2aad1b54d7a3c443e1c2edc7de3dfcc2402ebebb9aa05cc522ebf"),
            DecoderRegion(0xfd6f0, 1020, "70d1d40c4edcde46f11185103543c06f3d535c661de3558aaa2ad4da98e02d2b"),
            DecoderRegion(0x1628c8, 16, "aa12a11a9a4e78a18c84ca1b9239ea10b7365cc52c7e2b733d56d5d54e7279ce"),
            DecoderRegion(0xfe8f0, 212, "4edf9696508112ce32199c1441aeb39757395ac5dc40e032163bee0afe504913"),
            DecoderRegion(0x167aec, 148, "f0452a0a36d93dcd19304ad170e5b93c391648afa3bc49c9e7ff1cfe87ddeaa9"),
            DecoderRegion(0x167d98, 384, "db74511c2660e9408e71f821b35edd052e067a73115811166c739ab0b00fa4ff"),
            DecoderRegion(0x166e0c, 624, "201a9571dd85072eb5a9bc5b23d5ef7e86ef30bafeaae4671540057db576f2c1"),
            DecoderRegion(0x166884, 344, "b26aedfc48684f1320dcc083c84a51efaf5c3b075d53d17b4a5f8dcbacef8d03"),
            DecoderRegion(0x166dec, 12, "362ac61ddebdc66ed5e436f38174790646800a2120368d7784ec8a36e8b49950"),
            DecoderRegion(0x167f1c, 660, "22eb5623fd4bc06b3464dcadaf26c160cd4b460c3e0f428ebefeb72052700121"),
            DecoderRegion(0x1677c8, 804, "3d23c1dd82ba3726321ef3b279be8c4da10772cdb34c4f08ce23ba00ed888d30"),
        )),
)

internal fun isDecoderInputBufferingBuild(version: String, code: String): Boolean =
    decoderLayouts.any { it.version == version && it.code == code }

private fun decoderRequire(condition: Boolean, message: String) {
    if (!condition) throw PatchException("Decoder input buffering: $message")
}

private fun ByteArray.decoderHash(): String = MessageDigest.getInstance("SHA-256")
    .digest(this).joinToString("") { "%02x".format(it) }

/** The dependency must be located through DT_NEEDED, never an arbitrary ASCII occurrence. */
private class DecoderElf(private val bytes: ByteArray) {
    private val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    private fun range(offset: Long, length: Long): Int {
        decoderRequire(offset >= 0 && length >= 0 && offset <= bytes.size.toLong() - length,
            "ELF range outside file")
        return offset.toInt()
    }
    private fun u16(offset: Int): Int = buffer.getShort(range(offset.toLong(), 2)).toInt() and 0xffff
    private fun u32(offset: Int): Long = buffer.getInt(range(offset.toLong(), 4)).toLong() and 0xffffffffL
    private fun u64(offset: Int): Long = buffer.getLong(range(offset.toLong(), 8)).also {
        decoderRequire(it >= 0, "unsupported ELF 64-bit value")
    }
    private val headers: List<Int>

    init {
        decoderRequire(bytes.size >= 64 && bytes.copyOfRange(0, 7).contentEquals(
            byteArrayOf(0x7f, 0x45, 0x4c, 0x46, 2, 1, 1)), "expected ELF64 little-endian library")
        decoderRequire(u16(16) == 3 && u16(18) == 183 && u32(20) == 1L,
            "expected AArch64 ET_DYN library")
        decoderRequire(u16(54) == 56 && u16(56) in 1..64, "unexpected program-header layout")
        val start = range(u64(32), u16(56).toLong() * 56)
        headers = List(u16(56)) { start + it * 56 }
        headers.forEach { range(u64(it + 8), u64(it + 32)) }
    }

    fun fileOffset(address: Long, length: Int, executable: Boolean = false): Int {
        val matches = headers.filter { header ->
            if (u32(header) != 1L || (executable && u32(header + 4) and 1L == 0L)) false
            else {
                val relative = address - u64(header + 16)
                relative >= 0 && length.toLong() <= u64(header + 32) &&
                    relative <= u64(header + 32) - length
            }
        }
        decoderRequire(matches.size == 1, "ELF address lacks a unique compatible PT_LOAD")
        val header = matches.single()
        return range(u64(header + 8) + address - u64(header + 16), length.toLong())
    }

    fun neededEntries(): List<Pair<String, Int>> {
        val dynamic = headers.filter { u32(it) == 2L }
        decoderRequire(dynamic.size == 1, "expected one PT_DYNAMIC")
        val header = dynamic.single()
        val start = range(u64(header + 8), u64(header + 32))
        val size = u64(header + 32).toInt()
        decoderRequire(size >= 16 && size % 16 == 0, "invalid dynamic table size")
        val entries = mutableListOf<Pair<Long, Long>>()
        var terminated = false
        for (offset in start until start + size step 16) {
            val tag = u64(offset)
            if (tag == 0L) { terminated = true; break }
            entries += tag to u64(offset + 8)
        }
        decoderRequire(terminated, "unterminated dynamic table")
        val tables = entries.filter { it.first == 5L }
        val sizes = entries.filter { it.first == 10L }
        decoderRequire(tables.size == 1 && sizes.size == 1 && sizes.single().second <= bytes.size,
            "invalid dynamic string table")
        val stringSize = sizes.single().second.toInt()
        val stringStart = fileOffset(tables.single().second, stringSize)
        return entries.filter { it.first == 1L }.map { (_, index) ->
            decoderRequire(index < stringSize, "DT_NEEDED outside dynamic string table")
            val offset = stringStart + index.toInt()
            var end = offset
            while (end < stringStart + stringSize && bytes[end] != 0.toByte()) end++
            decoderRequire(end < stringStart + stringSize, "unterminated DT_NEEDED string")
            String(bytes, offset, end - offset, Charsets.US_ASCII) to offset
        }
    }
}

internal fun patchDecoderInputBufferingDependency(
    bytes: ByteArray,
    version: String,
    code: String,
): ByteArray {
    val layout = decoderLayouts.singleOrNull { it.version == version && it.code == code }
        ?: return bytes.copyOf()
    decoderRequire(bytes.size == layout.size, "unexpected libvrlink_scene.so size for $version/$code")
    val elf = DecoderElf(bytes)
    // The GNU note's bytes survive Visual Delay's PT_NOTE-to-PT_LOAD conversion.
    val note = ("040000001400000003000000474e5500" + layout.buildId)
        .chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    decoderRequire(bytes.copyOfRange(0x2d0, 0x2d0 + note.size).contentEquals(note),
        "GNU build ID does not match $version/$code")
    layout.regions.forEach { region ->
        decoderRequire(elf.fileOffset(region.offset.toLong(), region.size, executable = true) == region.offset,
            "decoder function mapping changed at 0x${region.offset.toString(16)}")
        decoderRequire(bytes.copyOfRange(region.offset, region.offset + region.size).decoderHash() == region.sha256,
            "stock decoder function changed at 0x${region.offset.toString(16)} for $version/$code")
    }
    val dependencies = elf.neededEntries().filter { it.first in setOf(STOCK_MEDIA_LIBRARY, DECODER_BUFFER_LIBRARY) }
    decoderRequire(dependencies.size == 1, "expected one stock or already-patched media dependency")
    val (name, offset) = dependencies.single()
    decoderRequire(offset == layout.neededStringOffset, "media dependency moved for $version/$code")
    return bytes.copyOf().apply {
        if (name == STOCK_MEDIA_LIBRARY) DECODER_BUFFER_LIBRARY.toByteArray(Charsets.US_ASCII).copyInto(this, offset)
    }
}

internal fun configureDecoderInputBufferingHelper(bytes: ByteArray, mode: String): ByteArray {
    val value = when (mode) {
        "observe" -> 0
        "buffered" -> 1
        "observe-telemetry" -> 2
        "buffered-telemetry" -> 3
        else -> throw PatchException("Unknown decoder input buffering mode: $mode")
    }
    val elf = DecoderElf(bytes)
    decoderRequire(elf.neededEntries().count { it.first == STOCK_MEDIA_LIBRARY } == 1,
        "bundled helper must retain its libmediandk.so dependency")
    val candidateMagics = listOf(DECODER_BUFFER_CONFIG_MAGIC, DECODER_TELEMETRY_CONFIG_MAGIC)
    val present = candidateMagics.filter { candidate ->
        val valueBytes = candidate.toByteArray(Charsets.US_ASCII)
        (0..bytes.size - valueBytes.size).any { start -> valueBytes.indices.all { bytes[start + it] == valueBytes[it] } }
    }
    decoderRequire(present.size == 1, "expected exactly one supported helper configuration version")
    val telemetryCapable = present.single() == DECODER_TELEMETRY_CONFIG_MAGIC
    decoderRequire(value < 2 || telemetryCapable, "pipeline telemetry requires the v2 helper")
    val magic = present.single().toByteArray(Charsets.US_ASCII)
    decoderRequire(magic.size == 16, "invalid configuration magic")
    val matches = (0..bytes.size - magic.size).filter { start ->
        magic.indices.all { bytes[start + it] == magic[it] }
    }
    decoderRequire(matches.size == 1, "expected one native helper configuration block")
    val offset = matches.single() + magic.size
    decoderRequire(offset <= bytes.size - 4, "truncated native helper configuration")
    val old = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).getInt(offset)
    decoderRequire(old in 0..(if (telemetryCapable) 3 else 1), "unsupported native helper configuration mode")
    return bytes.copyOf().apply { ByteBuffer.wrap(this).order(ByteOrder.LITTLE_ENDIAN).putInt(offset, value) }
}

internal fun verifyDecoderInputBufferingPayload(bytes: ByteArray, code: String, telemetry: Boolean = false) {
    decoderRequire((if (telemetry) decoderTelemetryPayloadHashes else decoderPayloadHashes)[code] == bytes.decoderHash(),
        "native payload hash does not match the compiled helper for $code")
}

@Suppress("unused")
val decoderInputBufferingPatch = rawResourcePatch(
    name = "Decoder input buffering (experimental)",
    description = "For exact Steam Link 2.0.22/5002322 and 2.0.23/5002363. Buffered stages incomplete compressed frames in bounded memory, then uses Valve's synchronous codec acquisition and submission when a frame is complete. Observe records the stock input path. Experimental; headset validation required.",
    default = false,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL.filter { compatibility ->
        compatibility.targets.any { target ->
            target.versionCodes?.values?.any { isDecoderInputBufferingBuild(target.version.orEmpty(), it.toString()) } == true
        }
    }.toTypedArray())
    val mode by stringOption(
        key = "mode",
        default = "buffered",
        values = decoderModes,
        title = "Decoder input mode",
        description = "Observe uses Valve's stock input path. Buffered stages incomplete frames before synchronous decoder submission. Pipeline telemetry adds frame IDs, codec timings and distinct fault reasons for a Perfetto capture; it is diagnostic, not a freeze fix. Plain Observe/Buffered retain the original v1 helper.",
        required = true,
    )
    execute {
        val version = packageMetadata.versionName
        val code = packageMetadata.versionCode
        if (!isDecoderInputBufferingBuild(version, code)) return@execute
        val sceneFile = get("lib/arm64-v8a/libvrlink_scene.so")
        val original = sceneFile.readBytes()
        val patched = patchDecoderInputBufferingDependency(original, version, code)
        val selectedMode = requireNotNull(mode)
        val resource = decoderHelperResource(code, selectedMode)
        val payload = (object {}.javaClass.getResourceAsStream(resource)
            ?: throw PatchException("Missing bundled decoder input helper: $resource"))
            .use { it.readBytes() }
        verifyDecoderInputBufferingPayload(payload, code, selectedMode.endsWith("-telemetry"))
        val helper = configureDecoderInputBufferingHelper(payload, selectedMode)
        val helperFile = get("lib/arm64-v8a/$DECODER_BUFFER_LIBRARY")
        if (helperFile.exists()) {
            val existing = helperFile.readBytes()
            decoderRequire(decoderModes.values.any { knownMode ->
                val knownPayload = (object {}.javaClass.getResourceAsStream(decoderHelperResource(code, knownMode))
                    ?: throw PatchException("Missing decoder helper for validated transition"))
                    .use { it.readBytes() }
                verifyDecoderInputBufferingPayload(knownPayload, code, knownMode.endsWith("-telemetry"))
                existing.contentEquals(configureDecoderInputBufferingHelper(knownPayload, knownMode))
            }, "existing decoder helper is not this experiment; start from a pristine APK")
        }
        // Validate scene and helper completely before writing either APK entry.
        helperFile.writeBytes(helper)
        if (!patched.contentEquals(original)) sceneFile.writeBytes(patched)
    }
}
