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
            "584a8d72d5ed563fff3a1a1a22414dbd6533ea6c9ab9611e2e104ae2a07fefd7",
            sha256,
        )

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
