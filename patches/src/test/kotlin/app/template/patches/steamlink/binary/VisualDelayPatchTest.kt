package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse

class VisualDelayPatchTest {
    @Test
    fun `5002318 layout patches all guarded sites and is idempotent`() {
        val stock = synthetic5002318Elf()
        val patched = patchVisualDelay(stock, 60)

        assertFalse(stock.contentEquals(patched))
        assertContentEquals(patched, patchVisualDelay(patched, 60))
        velocityStores.forEach { (offset, byteOffset) ->
            assertContentEquals(strWzrX19(byteOffset), patched.copyOfRange(offset, offset + 4))
        }
    }

    @Test
    fun `5002318 layout rejects a changed hook without partial mutation`() {
        val changed = synthetic5002318Elf().apply { this[hookOffset] = 0 }
        val snapshot = changed.copyOf()

        assertFailsWith<PatchException> { patchVisualDelay(changed, 60) }
        assertContentEquals(snapshot, changed)
    }

    @Test
    fun `unknown layout remains untouched`() {
        val input = ByteArray(128) { it.toByte() }
        assertContentEquals(input, patchVisualDelay(input, 60))
    }

    private fun synthetic5002318Elf() = ByteArray(2_277_488).apply {
        writeU32LE(0, 0x464C457F)
        writeU64LE(32, 64)
        writeU16LE(54, 56)
        writeU16LE(56, 1)
        writeU32LE(64, 1)
        writeU64LE(64 + 8, 0)
        writeU64LE(64 + 16, 0)
        writeU64LE(64 + 32, size.toLong())

        byteArrayOf(0xe2.toByte(), 0x07, 0x40, 0xf9.toByte()).copyInto(this, hookOffset)
        velocityStores.forEachIndexed { index, (offset, byteOffset) ->
            writeU32LE(
                offset,
                0xBD000000.toInt() or ((byteOffset / 4) shl 10) or (19 shl 5) or (index + 1),
            )
        }
        val cave = size - 32
        val brX17 = byteArrayOf(0x20, 0x02, 0x1f, 0xd6.toByte())
        brX17.copyInto(this, cave + 12)
        brX17.copyInto(this, cave + 28)
    }

    private fun strWzrX19(byteOffset: Int): ByteArray {
        val word = 0xB9000000.toInt() or ((byteOffset / 4) shl 10) or (19 shl 5) or 31
        return ByteArray(4).apply { writeU32LE(0, word) }
    }

    private fun ByteArray.writeU16LE(offset: Int, value: Int) {
        this[offset] = value.toByte()
        this[offset + 1] = (value ushr 8).toByte()
    }

    private fun ByteArray.writeU32LE(offset: Int, value: Int) {
        for (index in 0 until 4) this[offset + index] = (value ushr (index * 8)).toByte()
    }

    private fun ByteArray.writeU64LE(offset: Int, value: Long) {
        for (index in 0 until 8) this[offset + index] = (value ushr (index * 8)).toByte()
    }

    private companion object {
        const val hookOffset = 0x100B0C
        val velocityStores = listOf(
            0x100D80 to 28,
            0x100D84 to 32,
            0x100D88 to 36,
            0x100D94 to 40,
            0x100D98 to 44,
            0x100DA4 to 48,
        )
    }
}
