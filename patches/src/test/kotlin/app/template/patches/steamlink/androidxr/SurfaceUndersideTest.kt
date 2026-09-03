package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import java.io.File
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertSame
import kotlin.test.assertTrue
import org.junit.Assume.assumeTrue

class SurfaceUndersideTest {
    @Test
    fun `experimental placement is exact 5002322 and default remains tested quad`() {
        assertEquals("terminal-quad", xrGalaxyXrHighResolutionPatch.options["surfacePlacement"].default)
        assertEquals(ANDROID_SURFACE_UNDERSIDE_MODE,
            androidSurfaceModeForBuild("2.0.22", "5002322", "underside-projection"))
        listOf("5001712", "5001740", "5002172", "5002206", "5002244", "5002296", "5002313", "5002318", "9999999")
            .forEach { code ->
                assertEquals(ANDROID_SURFACE_TRIGGER_MODE,
                    androidSurfaceModeForBuild("2.0.22", code, "underside-projection"))
            }
        assertEquals(ANDROID_SURFACE_TRIGGER_MODE,
            androidSurfaceModeForBuild("2.0.20", "5001712", "underside-projection"))
        assertEquals(ANDROID_SURFACE_TRIGGER_MODE,
            androidSurfaceModeForBuild("2.0.20", "5002322", "underside-projection"))
        assertEquals(ANDROID_SURFACE_TRIGGER_MODE,
            androidSurfaceModeForBuild("2.0.22", "5002322", "terminal-quad"))
        val resourceDependency = xrGalaxyXrHighResolutionPatch.dependencies.single {
            it.options.any { option -> option.key == "surfacePlacement" }
        }
        assertSame(xrGalaxyXrHighResolutionPatch.options["surfacePlacement"],
            resourceDependency.options["surfacePlacement"])
    }

    @Test
    fun `active modes cannot be stacked`() {
        assertTrue(projectionModesConflict(ANDROID_SURFACE_TRIGGER_MODE, ANDROID_SURFACE_UNDERSIDE_MODE))
        assertTrue(projectionModesConflict(ANDROID_SURFACE_UNDERSIDE_MODE, ANDROID_SURFACE_TRIGGER_MODE))
        assertFalse(projectionModesConflict(ANDROID_SURFACE_UNDERSIDE_MODE, ANDROID_SURFACE_UNDERSIDE_MODE))
    }

    @Test
    fun `unknown underside layouts fail closed`() {
        listOf(byteArrayOf(), ByteArray(100), ByteArray(2_283_400)).forEach {
            assertFailsWith<PatchException> { validateSurfaceUndersideLayout(it) }
        }
    }

    @Test
    fun `known renderer accepts unrelated mutations but rejects changed submit and render code`() {
        val relative = "decoded-apk-android-steamlinkvr-release-base-2.0.22-5002322/lib/arm64-v8a/libvrlink_scene.so"
        val file = listOf(File(relative), File("../$relative")).firstOrNull(File::isFile)
        assumeTrue("Local decoded 5002322 fixture is optional", file != null)
        val bytes = file!!.readBytes()
        validateSurfaceUndersideLayout(bytes)
        val ordinaryPatch = bytes.copyOf().also { it[0x96ba5] = 0 } // OLED shader outside pinned renderer.
        validateSurfaceUndersideLayout(ordinaryPatch)
        listOf(0x10af60, 0x10b3fc, 0x10bf38).forEach { offset ->
            val changed = bytes.copyOf().also { it[offset] = (it[offset].toInt() xor 1).toByte() }
            assertFailsWith<PatchException> { validateSurfaceUndersideLayout(changed) }
        }
    }

    @Test
    fun `experimental helper has separate manifest and binary identity`() {
        val manifest = requireNotNull(javaClass.getResource("/steamlink/androidxr/$ANDROID_SURFACE_UNDERSIDE_MANIFEST")).readText()
        assertTrue(manifest.contains("\"library_path\": \"$ANDROID_SURFACE_UNDERSIDE_LIBRARY\""))
        assertTrue(manifest.contains("GXR_DISABLE_ANDROID_SURFACE_TRIGGER"))
        val helper = requireNotNull(javaClass.getResourceAsStream("/steamlink/androidxr/$ANDROID_SURFACE_UNDERSIDE_LIBRARY"))
            .use { it.readBytes() }
        assertContentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46), helper.copyOfRange(0, 4))
        val strings = helper.toString(Charsets.ISO_8859_1)
        listOf(ANDROID_SURFACE_UNDERSIDE_MODE, ANDROID_SURFACE_UNDERSIDE_BUILD_ID,
            "surface_underside_frame", "surface_underside_submission", "xrCreateSwapchainAndroidSurfaceKHR")
            .forEach { assertTrue(strings.contains(it), it) }
        listOf(ANDROID_SURFACE_TRIGGER_BUILD_ID, ANDROID_SURFACE_TRIGGER_5001712_BUILD_ID,
            "glDrawArrays", "glBlitFramebuffer", "surface_trigger_submission")
            .forEach { assertFalse(strings.contains(it), it) }
        assertEquals("4619da11b844fd8987cf954e46a4eb63540e4401ee4bce038cc1c510fbdec6f2",
            MessageDigest.getInstance("SHA-256").digest(helper).joinToString("") { "%02x".format(it) })
    }
}
