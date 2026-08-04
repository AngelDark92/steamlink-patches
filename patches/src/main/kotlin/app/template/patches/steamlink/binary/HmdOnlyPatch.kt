package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import app.template.patches.steamlink.util.BinaryPatchHelper.findUniqueAndReplace
import app.template.patches.steamlink.util.BinaryPatchHelper.vaddrToFileOffset
import java.nio.ByteBuffer
import java.nio.ByteOrder

// Injects an AArch64 trampoline that adds 78 ms to the HMD pose query time
// and zeroes the six HMD velocity fields exported to SteamVR.
// Controller paths are not modified.

private val ORIG_HOOK = byteArrayOf(0xe2.toByte(), 0x07, 0x40, 0xf9.toByte())
private val PATCHED_HOOK = byteArrayOf(0xfe.toByte(), 0x5e, 0x04, 0x14)

private val ORIG_CAVE = byteArrayOf(
    0x90.toByte(), 0x00, 0x00, 0xd0.toByte(), 0x11, 0xbe.toByte(), 0x44, 0xf9.toByte(),
    0x10, 0xe2.toByte(), 0x25, 0x91.toByte(), 0x20, 0x02, 0x1f, 0xd6.toByte(),
    0x90.toByte(), 0x00, 0x00, 0xd0.toByte(), 0x11, 0xc2.toByte(), 0x44, 0xf9.toByte(),
    0x10, 0x02, 0x26, 0x91.toByte(), 0x20, 0x02, 0x1f, 0xd6.toByte(),
)
private val PATCHED_CAVE = byteArrayOf(
    0xe2.toByte(), 0x07, 0x40, 0xf9.toByte(), 0x10, 0xf0.toByte(), 0x85.toByte(), 0xd2.toByte(),
    0xd0.toByte(), 0x94.toByte(), 0xa0.toByte(), 0xf2.toByte(), 0x42, 0x00, 0x10, 0x8b.toByte(),
    0xff.toByte(), 0xa0.toByte(), 0xfb.toByte(), 0x17, 0x1f, 0x20, 0x03, 0xd5.toByte(),
    0x1f, 0x20, 0x03, 0xd5.toByte(), 0x1f, 0x20, 0x03, 0xd5.toByte(),
)

// Virtual addresses of the six HMD velocity STR instructions (base register x19).
private val VELOCITY_VADDRS = longArrayOf(
    0x000FED48L, 0x000FED4CL, 0x000FED50L,
    0x000FED5CL, 0x000FED60L, 0x000FED6CL,
)
private val VELOCITY_OFFSETS = intArrayOf(28, 32, 36, 40, 44, 48)

private fun strWzrX19(byteOffset: Int): ByteArray {
    // STR WZR, [X19, #byteOffset]: opcode 0xB9, Rn=19, Rt=31 (WZR)
    val imm12 = byteOffset / 4
    val word = 0xB9000000.toInt() or (imm12 shl 10) or (19 shl 5) or 31
    return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(word).array()
}

private fun isStrSUnsignedImm(word: Int, baseReg: Int, byteOffset: Int): Boolean =
    (word and 0xFFC00000.toInt()) == 0xBD000000.toInt() &&
    ((word ushr 5) and 0x1F) == baseReg &&
    (((word ushr 10) and 0xFFF) * 4) == byteOffset &&
    (word and 0x1F) != 31

@Suppress("unused")
val hmdOnlyPatch = rawResourcePatch(
    name = "HMD-only pose fix",
    description = "Adds 78 ms to the HMD OpenXR pose-query time and zeroes all six exported HMD velocity fields. Does not affect controller paths.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    execute {
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        // Cave replacement first — the 32-byte PLT pair is unique, so it's a safe anchor.
        val mutable = findUniqueAndReplace(file.readBytes(), ORIG_CAVE, PATCHED_CAVE)

        // Hook: use ELF vaddr→offset mapping for the exact 4-byte instruction.
        val hookOffset = vaddrToFileOffset(mutable, 0x000FEAD8L, ORIG_HOOK.size)
        val hookActual = mutable.sliceArray(hookOffset until hookOffset + ORIG_HOOK.size)
        if (!hookActual.contentEquals(ORIG_HOOK)) {
            throw PatchException(
                "HMD hook precondition failed at 0x${hookOffset.toString(16)}: " +
                "expected ${ORIG_HOOK.toHex()}, found ${hookActual.toHex()}"
            )
        }
        PATCHED_HOOK.copyInto(mutable, hookOffset)

        // Zero the six HMD velocity store instructions.
        for (i in VELOCITY_VADDRS.indices) {
            val off = vaddrToFileOffset(mutable, VELOCITY_VADDRS[i], 4)
            val word = mutable.readU32LE(off)
            if (!isStrSUnsignedImm(word, 19, VELOCITY_OFFSETS[i])) {
                throw PatchException(
                    "Velocity store precondition failed at vaddr 0x${VELOCITY_VADDRS[i].toString(16)}: " +
                    "word 0x${word.toUInt().toString(16)}"
                )
            }
            strWzrX19(VELOCITY_OFFSETS[i]).copyInto(mutable, off)
        }

        file.writeBytes(mutable)
    }
}

private fun ByteArray.readU32LE(off: Int): Int =
    (this[off].toInt() and 0xFF) or
    ((this[off + 1].toInt() and 0xFF) shl 8) or
    ((this[off + 2].toInt() and 0xFF) shl 16) or
    ((this[off + 3].toInt() and 0xFF) shl 24)

private fun ByteArray.toHex() = joinToString("") { "%02x".format(it) }
