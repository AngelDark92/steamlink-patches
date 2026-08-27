package app.template.patches.steamlink.androidxr

import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ProjectionMetadataCompatibilityResourceTest {
    @Test
    fun `bundled projection compatibility layer and manifest identify v2`() {
        val library = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/libgxr_projection_metadata_compat_v2.so",
            ),
        ) { "Missing bundled projection metadata compatibility layer" }.use { it.readBytes() }
        assertTrue(library.size > 4 && library.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46)))

        val sha256 = MessageDigest.getInstance("SHA-256")
            .digest(library)
            .joinToString("") { "%02x".format(it) }
        assertEquals(
            "d6758bf59d2ab27fdfc2f014a4d6f01ca2c3f4d229d20a286360e1f321c081b5",
            sha256,
        )

        val manifest = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/XR_APILAYER_local_GalaxyXR_projection_metadata_compat_v2.json",
            ),
        ).bufferedReader().use { it.readText() }
        assertTrue(manifest.contains("XR_APILAYER_local_GalaxyXR_projection_metadata_compat_v2"))
        assertTrue(manifest.contains("libgxr_projection_metadata_compat_v2.so"))
    }
}
