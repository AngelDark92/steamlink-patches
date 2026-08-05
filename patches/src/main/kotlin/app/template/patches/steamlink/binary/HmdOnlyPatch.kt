package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_HMD_ONLY
import app.template.patches.steamlink.util.BinaryPatchHelper.vaddrToFileOffset
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

// Injects an AArch64 trampoline that adds 78 ms to the HMD pose query time
// and zeroes the six HMD velocity fields exported to SteamVR.
// Controller paths are not modified.

private const val HOOK_VADDR = 0x000FEAD8L

private val ORIG_HOOK = byteArrayOf(0xe2.toByte(), 0x07, 0x40, 0xf9.toByte())

// mov x16, #0x2f80 + movk x16, #0x04a6, lsl#16 + add x2, x2, x16  (constant = 78 000 000 ns)
private val TRAMPOLINE_BODY = byteArrayOf(
    0x10, 0xf0.toByte(), 0x85.toByte(), 0xd2.toByte(),
    0xd0.toByte(), 0x94.toByte(), 0xa0.toByte(), 0xf2.toByte(),
    0x42, 0x00, 0x10, 0x8b.toByte(),
)
private val NOP = byteArrayOf(0x1f, 0x20, 0x03, 0xd5.toByte())
private val BR_X17 = byteArrayOf(0x20, 0x02, 0x1f, 0xd6.toByte())

// Encode AArch64 B <target> from <pc>.
private fun buildBranch(pc: Long, target: Long): ByteArray {
    val offsetWords = ((target - pc) / 4).toInt()
    val insn = 0x14000000.toInt() or (offsetWords and 0x3FFFFFF)
    return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(insn).array()
}

// Find the last 32 bytes of the first PT_LOAD segment (two PLT entries = code cave).
private fun findPltCave(bytes: ByteArray): Pair<Int, Long> {
    val phoff  = bytes.readU32LE(32)
    val phesz  = bytes.readU16LE(54)
    val phnum  = bytes.readU16LE(56)
    for (i in 0 until phnum) {
        val base = phoff + i * phesz
        if (bytes.readU32LE(base) != 1) continue           // PT_LOAD
        if (bytes.readU64LE(base + 8) != 0L) continue      // first LOAD at fileoff=0
        val vaddr   = bytes.readU64LE(base + 16)
        val filesz  = bytes.readU64LE(base + 32).toInt()
        val caveOff = filesz - 32
        val caveVa  = vaddr + (filesz - 32).toLong()
        if (caveOff < 0 || caveOff + 32 > bytes.size) {
            throw PatchException("Invalid PLT cave range at 0x${caveOff.toString(16)}")
        }
        return Pair(caveOff, caveVa)
    }
    throw PatchException("No executable PT_LOAD segment found in ELF")
}

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

private fun isStrWzrX19(word: Int, byteOffset: Int): Boolean =
    word == ByteBuffer.wrap(strWzrX19(byteOffset)).order(ByteOrder.LITTLE_ENDIAN).int

private fun ByteArray.sha256(): String =
    MessageDigest.getInstance("SHA-256").digest(this).toHex()

@Suppress("unused")
val hmdOnlyPatch = rawResourcePatch(
    name = "HMD-only pose fix",
    description = "Adds 78 ms to the HMD OpenXR pose-query time and zeroes all six exported HMD velocity fields. Does not affect controller paths.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_HMD_ONLY)

    execute {
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        val mutable = file.readBytes().copyOf()

        // Locate PLT cave at end of first executable segment (version-agnostic).
        val (caveOff, caveVa) = findPltCave(mutable)
        val trampoline = ORIG_HOOK + TRAMPOLINE_BODY +
            buildBranch(caveVa + 16, HOOK_VADDR + 4) + NOP + NOP + NOP
        val patchedHook = buildBranch(HOOK_VADDR, caveVa)
        val hookOffset = vaddrToFileOffset(mutable, HOOK_VADDR, ORIG_HOOK.size)
        val hookActual = mutable.sliceArray(hookOffset until hookOffset + ORIG_HOOK.size)
        val caveActual = mutable.sliceArray(caveOff until caveOff + trampoline.size)
        val velocityOffsets = VELOCITY_VADDRS.map { vaddrToFileOffset(mutable, it, 4) }
        val velocityWords = velocityOffsets.map { mutable.readU32LE(it) }

        val alreadyPatched = hookActual.contentEquals(patchedHook) &&
            caveActual.contentEquals(trampoline) &&
            velocityWords.indices.all { isStrWzrX19(velocityWords[it], VELOCITY_OFFSETS[it]) }
        if (alreadyPatched) return@execute

        if (!hookActual.contentEquals(ORIG_HOOK)) {
            throw PatchException(
                "Unsupported libvrlink_scene.so for HMD-only pose fix: " +
                "size=${mutable.size}, sha256=${mutable.sha256()}, " +
                "hook@0x${hookOffset.toString(16)}=${hookActual.toHex()} " +
                "(expected ${ORIG_HOOK.toHex()})"
            )
        }

        val originalCave =
            caveActual.sliceArray(12 until 16).contentEquals(BR_X17) &&
            caveActual.sliceArray(28 until 32).contentEquals(BR_X17)
        if (!originalCave) {
            throw PatchException(
                "HMD trampoline cave precondition failed at 0x${caveOff.toString(16)}: " +
                caveActual.toHex()
            )
        }

        for (i in velocityWords.indices) {
            if (!isStrSUnsignedImm(velocityWords[i], 19, VELOCITY_OFFSETS[i])) {
                throw PatchException(
                    "Velocity store precondition failed at vaddr 0x${VELOCITY_VADDRS[i].toString(16)}: " +
                    "word 0x${velocityWords[i].toUInt().toString(16)}"
                )
            }
        }

        // All checks passed. Commit changes only now.
        trampoline.copyInto(mutable, caveOff)
        patchedHook.copyInto(mutable, hookOffset)
        for (i in velocityOffsets.indices) {
            val off = velocityOffsets[i]
            strWzrX19(VELOCITY_OFFSETS[i]).copyInto(mutable, off)
        }

        file.writeBytes(mutable)
    }
}

private fun ByteArray.readU16LE(off: Int): Int =
    (this[off].toInt() and 0xFF) or
    ((this[off + 1].toInt() and 0xFF) shl 8)

private fun ByteArray.readU32LE(off: Int): Int =
    (this[off].toInt() and 0xFF) or
    ((this[off + 1].toInt() and 0xFF) shl 8) or
    ((this[off + 2].toInt() and 0xFF) shl 16) or
    ((this[off + 3].toInt() and 0xFF) shl 24)

private fun ByteArray.readU64LE(off: Int): Long =
    (readU32LE(off).toLong() and 0xFFFFFFFFL) or
    ((readU32LE(off + 4).toLong() and 0xFFFFFFFFL) shl 32)

private fun ByteArray.toHex() = joinToString("") { "%02x".format(it) }
