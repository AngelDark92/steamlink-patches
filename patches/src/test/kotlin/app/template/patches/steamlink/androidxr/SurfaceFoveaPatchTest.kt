package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import app.template.patches.steamlink.galaxyXrRecommended5002322Patch
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.test.assertFailsWith

class SurfaceFoveaPatchTest {
    @Test
    fun `experiment is exact build default off and excluded from recommended set`() {
        val patch = experimentalAndroidSurfaceFoveaPatch
        assertFalse(patch.default)
        val compatibility = requireNotNull(patch.compatibility).single()
        assertEquals(EXPERIMENTAL_COMPATIBILITY_NAME, compatibility.name)
        val target = compatibility.targets.single()
        assertEquals("2.0.22", target.version)
        assertEquals(setOf(5002322), target.versionCodes!!.values.toSet())
        assertFalse(patch in galaxyXrRecommended5002322Patch.dependencies)
        assertFalse(xrGalaxyXrHighResolutionPatch in patch.dependencies)
    }

    @Test
    fun `different resolution modes conflict in either order`() {
        assertTrue(projectionModesConflict(ANDROID_SURFACE_FOVEA_MODE, ANDROID_SURFACE_TRIGGER_MODE))
        assertTrue(projectionModesConflict(ANDROID_SURFACE_TRIGGER_MODE, ANDROID_SURFACE_FOVEA_MODE))
        assertFalse(projectionModesConflict(ANDROID_SURFACE_FOVEA_MODE, ANDROID_SURFACE_FOVEA_MODE))
    }

    @Test
    fun `unknown and modified renderer bytes fail closed`() {
        assertFailsWith<PatchException> { validateSurfaceFoveaLayout(byteArrayOf()) }
        val fake = ByteArray(2_283_400)
        byteArrayOf(0x7f, 0x45, 0x4c, 0x46, 2).copyInto(fake)
        fake[18] = 0xb7.toByte()
        assertFailsWith<PatchException> { validateSurfaceFoveaLayout(fake) }
    }

    @Test
    fun `separate surface producer manifest is packaged`() {
        val manifest = requireNotNull(javaClass.getResource("/steamlink/androidxr/$ANDROID_SURFACE_FOVEA_MANIFEST")).readText()
        assertTrue(manifest.contains("\"library_path\": \"libgxr_asf.so\""))
        assertTrue(manifest.contains("GXR_DISABLE_ANDROID_SURFACE_FOVEA"))
        val helper = requireNotNull(javaClass.getResourceAsStream("/steamlink/androidxr/$ANDROID_SURFACE_FOVEA_LIBRARY")).use { it.readBytes() }
        assertTrue(helper.size > 4 && helper.take(4) == listOf<Byte>(0x7f, 0x45, 0x4c, 0x46))
    }
}
