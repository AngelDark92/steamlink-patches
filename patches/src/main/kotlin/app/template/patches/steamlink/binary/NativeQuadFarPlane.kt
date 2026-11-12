package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import java.security.MessageDigest

/**
 * Pinned identity of the far-plane variant of the native quad helper
 * (`extensions/native-quad-composition`, build `Build-Native.ps1 -Variant far`).
 *
 * Same source as the bundled 1.2 helper, compiled with `-DGXR_SQ_DISTANCE_SLOT`: a writable
 * marker + float32 slot at [NATIVE_QUAD_FAR_SLOT_OFFSET], read once per state in `op_create`.
 * The slot offset is derived from the marker at build time (`tools/far_slot.py`) and pinned here
 * together with the whole-file size and SHA-256.
 */
internal const val NATIVE_QUAD_FAR_BUILD_ID = "GXR-NATIVE-QUAD-1.2-FAR-ABI1"
internal const val NATIVE_QUAD_FAR_HELPER_SHA256 =
    "d4a300510124a1595124e54aacfc214ee78b5edef122dab7d010b20d4221cd84"
internal const val NATIVE_QUAD_FAR_HELPER_SIZE = 8792
internal const val NATIVE_QUAD_FAR_SLOT_OFFSET = 0x1C50
internal const val NATIVE_QUAD_FAR_MARKER = "GXRQUADDIST16M0"

/** Stock distance slot bytes: 1.996000051498413f little-endian (0x3fff7cee). */
internal val NATIVE_QUAD_FAR_DEFAULT_SLOT = byteArrayOf(0xee.toByte(), 0x7c, 0xff.toByte(), 0x3f)

/** Quad distances offered by the option, in metres. */
internal val NATIVE_QUAD_FAR_DISTANCES = listOf(16, 64, 250)

internal const val NATIVE_QUAD_FAR_DEFAULT_DISTANCE = 64

private val NATIVE_QUAD_FAR_ELF_MAGIC = byteArrayOf(0x7f, 0x45, 0x4c, 0x46)

private fun farSha256(bytes: ByteArray): String =
    MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

private fun farFloatBytes(meters: Int): ByteArray {
    val bits = java.lang.Float.floatToRawIntBits(meters.toFloat())
    return byteArrayOf(
        (bits and 0xff).toByte(),
        ((bits ushr 8) and 0xff).toByte(),
        ((bits ushr 16) and 0xff).toByte(),
        ((bits ushr 24) and 0xff).toByte(),
    )
}

private fun farMarkerMatches(bytes: ByteArray): Boolean {
    val marker = NATIVE_QUAD_FAR_MARKER.toByteArray(Charsets.ISO_8859_1)
    val start = NATIVE_QUAD_FAR_SLOT_OFFSET - marker.size - 1
    if (start < 0 || NATIVE_QUAD_FAR_SLOT_OFFSET + 4 > bytes.size) return false
    return marker.indices.all { bytes[start + it] == marker[it] } &&
        bytes[NATIVE_QUAD_FAR_SLOT_OFFSET - 1] == 0.toByte()
}

/** The distance stored in an already-far payload, or null when the slot holds anything else. */
internal fun readNativeQuadFarDistance(bytes: ByteArray): Int? {
    if (!farMarkerMatches(bytes)) return null
    val bits = (bytes[NATIVE_QUAD_FAR_SLOT_OFFSET].toInt() and 0xff) or
        ((bytes[NATIVE_QUAD_FAR_SLOT_OFFSET + 1].toInt() and 0xff) shl 8) or
        ((bytes[NATIVE_QUAD_FAR_SLOT_OFFSET + 2].toInt() and 0xff) shl 16) or
        ((bytes[NATIVE_QUAD_FAR_SLOT_OFFSET + 3].toInt() and 0xff) shl 24)
    val distance = Float.fromBits(bits)
    return NATIVE_QUAD_FAR_DISTANCES.firstOrNull { it.toFloat() == distance }
}

/**
 * True when [bytes] is the pinned far payload carrying any allowed distance. All bytes outside the
 * four slot bytes must match the pin: the slot is normalised to the stock float before hashing.
 */
internal fun isNativeQuadFarPayload(bytes: ByteArray): Boolean {
    if (bytes.size != NATIVE_QUAD_FAR_HELPER_SIZE || !farMarkerMatches(bytes)) return false
    if (readNativeQuadFarDistance(bytes) == null) return false
    val normalized = bytes.copyOf()
    NATIVE_QUAD_FAR_DEFAULT_SLOT.copyInto(normalized, NATIVE_QUAD_FAR_SLOT_OFFSET)
    return farSha256(normalized) == NATIVE_QUAD_FAR_HELPER_SHA256
}

/** Validates the bundled far payload before it is ever used: ELF, size, SHA-256, identity, slot. */
internal fun validateNativeQuadFarPayload(payload: ByteArray) {
    if (payload.size < 4 || !payload.copyOfRange(0, 4).contentEquals(NATIVE_QUAD_FAR_ELF_MAGIC)) {
        throw PatchException("Bundled far-plane helper is not an ELF library")
    }
    if (payload.size != NATIVE_QUAD_FAR_HELPER_SIZE) {
        throw PatchException(
            "Bundled far-plane helper size ${payload.size} does not match the pin ($NATIVE_QUAD_FAR_HELPER_SIZE)",
        )
    }
    if (farSha256(payload) != NATIVE_QUAD_FAR_HELPER_SHA256) {
        throw PatchException("Bundled far-plane helper failed its SHA-256 precondition")
    }
    if (!payload.toString(Charsets.ISO_8859_1).contains(NATIVE_QUAD_FAR_BUILD_ID)) {
        throw PatchException("Bundled far-plane helper build identity is missing")
    }
    if (!farMarkerMatches(payload)) {
        throw PatchException("Bundled far-plane helper lost its distance marker")
    }
    if (!payload.copyOfRange(NATIVE_QUAD_FAR_SLOT_OFFSET, NATIVE_QUAD_FAR_SLOT_OFFSET + 4)
            .contentEquals(NATIVE_QUAD_FAR_DEFAULT_SLOT)) {
        throw PatchException("Bundled far-plane helper does not carry the stock distance")
    }
}

/**
 * Replaces the installed helper with the far variant carrying [meters] in its slot.
 *
 * Accepts only the pinned 1.2 helper or an already-far payload; anything else fails closed before a
 * caller can write. Only the four slot bytes differ from [farPayload], so the result is idempotent
 * and distance-to-distance transitions reuse the same artifact.
 */
internal fun applyNativeQuadFarDistance(installed: ByteArray, meters: Int, farPayload: ByteArray): ByteArray {
    if (meters !in NATIVE_QUAD_FAR_DISTANCES) {
        throw PatchException("Far plane: unsupported quad distance $meters")
    }
    val base = when {
        installed.size == 8640 && farSha256(installed) == NATIVE_QUAD_HELPER_SHA256 -> farPayload
        isNativeQuadFarPayload(installed) -> installed
        else -> throw PatchException(
            "Far plane: the installed quad helper is neither the bundled 1.2 helper nor the far-plane variant",
        )
    }
    val result = base.copyOf()
    farFloatBytes(meters).copyInto(result, NATIVE_QUAD_FAR_SLOT_OFFSET)
    return result
}
