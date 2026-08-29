package app.template.patches.steamlink.androidxr

import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class TwoProjectionDropBaseResourceTest {
    @Test
    fun `active projection modes are mutually exclusive`() {
        assertFalse(
            projectionModesConflict(
                "two_projection_drop_base_v1",
                "two_projection_drop_base_v1",
            ),
        )
        assertTrue(
            projectionModesConflict(
                "single_projection_reconstruction_v1",
                "two_projection_drop_base_v1",
            ),
        )
        assertTrue(
            projectionModesConflict(
                "two_projection_drop_base_v1",
                "single_projection_reconstruction_v1",
            ),
        )
        assertTrue(
            projectionModesConflict(
                "three_projection_sampler_proxy_v1",
                "two_projection_drop_base_v1",
            ),
        )
    }

    @Test
    fun `bundled drop-base layer and manifest use unique v1 identity`() {
        val library = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/libgxr_two_projection_drop_base_v1.so",
            ),
        ) { "Missing bundled two-projection drop-base layer" }.use { it.readBytes() }
        assertTrue(
            library.size > 4 &&
                library.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46)),
        )
        val sha256 = MessageDigest.getInstance("SHA-256")
            .digest(library)
            .joinToString("") { "%02x".format(it) }
        assertEquals(
            "dd9611516075f9d1969c59c019f8420ace58dfdab50746bfe1a54f03df204356",
            sha256,
        )
        val nativeStrings = library.toString(Charsets.ISO_8859_1)
        assertTrue(nativeStrings.contains("two-projection-drop-base-v1.1-20260829"))
        assertTrue(nativeStrings.contains("two_projection_drop_base_transform"))
        assertTrue(nativeStrings.contains("two_projection_drop_base_auxiliary"))

        val manifest = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/XR_APILAYER_local_GalaxyXR_two_projection_drop_base_v1.json",
            ),
        ).bufferedReader().use { it.readText() }
        assertTrue(manifest.contains("XR_APILAYER_local_GalaxyXR_two_projection_drop_base_v1"))
        assertTrue(manifest.contains("libgxr_two_projection_drop_base_v1.so"))
        assertTrue(manifest.contains("GXR_DISABLE_TWO_PROJECTION_DROP_BASE"))
    }
}
