package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertFailsWith

class LegacyNativeCompatibilityPatchTest {
    private val faceOriginal =
        "com.oculus.permission.FACE_TRACKING\u0000".toByteArray(Charsets.US_ASCII)
    private val facePatched =
        "android.permission.HAND_TRACKING\u0000\u0000\u0000\u0000".toByteArray(Charsets.US_ASCII)
    private val eyeOriginal =
        "com.oculus.permission.EYE_TRACKING\u0000}\u0000".toByteArray(Charsets.US_ASCII)
    private val eyePatched =
        "android.permission.EYE_TRACKING_FINE\u0000".toByteArray(Charsets.US_ASCII)

    @Test
    fun `permission names relocate on an unknown native layout`() {
        val input = byteArrayOf(1, 2, 3) + faceOriginal + byteArrayOf(4, 5) + eyeOriginal + byteArrayOf(6)
        val expected = byteArrayOf(1, 2, 3) + facePatched + byteArrayOf(4, 5) + eyePatched + byteArrayOf(6)

        val patched = patchNativePermissionNames(input)

        assertContentEquals(expected, patched)
        assertContentEquals(expected, patchNativePermissionNames(patched))
    }

    @Test
    fun `unknown layout without permission patterns is left untouched`() {
        val input = ByteArray(128) { it.toByte() }

        assertContentEquals(input, patchNativePermissionNames(input))
    }

    @Test
    fun `unknown layout with only one permission pattern is left untouched atomically`() {
        val input = byteArrayOf(1, 2, 3) + faceOriginal + byteArrayOf(4, 5, 6)

        assertContentEquals(input, patchNativePermissionNames(input))
    }

    @Test
    fun `known layout without permission patterns remains strict`() {
        assertFailsWith<PatchException> {
            patchNativePermissionNames(ByteArray(2_251_920))
        }
    }
}
