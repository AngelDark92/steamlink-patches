package app.template.patches.steamlink.androidxr

import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SingleProjectionReconstructionResourceTest {
    @Test
    fun `bundled reconstruction layer and manifest use unique v1 identity`() {
        val library = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/libgxr_single_projection_reconstruction_v1.so",
            ),
        ) { "Missing bundled single-projection reconstruction layer" }.use { it.readBytes() }
        assertTrue(
            library.size > 4 &&
                library.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46)),
        )
        val sha256 = MessageDigest.getInstance("SHA-256")
            .digest(library)
            .joinToString("") { "%02x".format(it) }
        assertEquals(
            "286fe157dc7b814f9eed5204f01f2188cb2ae1608ca91534452cb8f20ebb9257",
            sha256,
        )
        val nativeStrings = library.toString(Charsets.ISO_8859_1)
        assertTrue(nativeStrings.contains("single-projection-reconstruction-v1.2-20260829"))
        assertTrue(nativeStrings.contains("linear_4tap_subpixel_box"))
        assertTrue(nativeStrings.contains("qualityExtensionAppended"))
        assertTrue(nativeStrings.contains("outputQualitySettingsAttached"))

        val manifest = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_v1.json",
            ),
        ).bufferedReader().use { it.readText() }
        assertTrue(manifest.contains("XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_v1"))
        assertTrue(manifest.contains("libgxr_single_projection_reconstruction_v1.so"))
    }

    @Test
    fun `retired projection metadata v2 resources are absent`() {
        assertNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/libgxr_projection_metadata_compat_v2.so",
            ),
        )
        assertNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/XR_APILAYER_local_GalaxyXR_projection_metadata_compat_v2.json",
            ),
        )
    }
}
