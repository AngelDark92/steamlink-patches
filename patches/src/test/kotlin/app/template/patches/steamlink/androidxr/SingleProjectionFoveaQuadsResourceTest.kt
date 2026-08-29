package app.template.patches.steamlink.androidxr

import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class SingleProjectionFoveaQuadsResourceTest {
    @Test
    fun `fovea quad mode conflicts with every other projection experiment`() {
        assertFalse(
            projectionModesConflict(
                "single_projection_fovea_quads_v1",
                "single_projection_fovea_quads_v1",
            ),
        )
        listOf(
            "single_projection_reconstruction_v1",
            "two_projection_drop_base_v1",
            "three_projection_sampler_proxy_v1",
        ).forEach { mode ->
            assertTrue(projectionModesConflict(mode, "single_projection_fovea_quads_v1"))
            assertTrue(projectionModesConflict("single_projection_fovea_quads_v1", mode))
        }
    }

    @Test
    fun `bundled fovea quad layer has guarded source identity`() {
        val library = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/libgxr_single_projection_fovea_quads_v1.so",
            ),
        ) { "Missing bundled single-projection fovea-quad layer" }.use { it.readBytes() }
        assertTrue(
            library.size > 4 &&
                library.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46)),
        )
        val sha256 = MessageDigest.getInstance("SHA-256")
            .digest(library)
            .joinToString("") { "%02x".format(it) }
        assertEquals(
            "ab81d40c94c82636b67706936ef5c87d7f9c048a257080ab1404fa5660da4308",
            sha256,
        )
        val nativeStrings = library.toString(Charsets.ISO_8859_1)
        assertTrue(nativeStrings.contains("single-projection-fovea-quads-v1.0-20260829"))
        assertTrue(nativeStrings.contains("single_projection_fovea_quads_transform"))
        assertTrue(nativeStrings.contains("single_projection_fovea_quads_auxiliary"))
        assertTrue(nativeStrings.contains("single_projection_fovea_quads_post_activation_target_bypass"))

        val manifest = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/XR_APILAYER_local_GalaxyXR_single_projection_fovea_quads_v1.json",
            ),
        ).bufferedReader().use { it.readText() }
        assertTrue(manifest.contains("XR_APILAYER_local_GalaxyXR_single_projection_fovea_quads_v1"))
        assertTrue(manifest.contains("libgxr_single_projection_fovea_quads_v1.so"))
        assertTrue(manifest.contains("GXR_DISABLE_SINGLE_PROJECTION_FOVEA_QUADS"))
    }
}
