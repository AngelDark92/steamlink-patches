package app.template.patches.steamlink.androidxr

import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ThreeProjectionSamplerProxyResourceTest {
    @Test
    fun `three projection proxy conflicts with every other projection transform`() {
        assertTrue(
            projectionModesConflict(
                "three_projection_sampler_proxy_v1",
                "single_projection_reconstruction_v1",
            ),
        )
        assertTrue(
            projectionModesConflict(
                "three_projection_sampler_proxy_v1",
                "two_projection_drop_base_v1",
            ),
        )
        assertTrue(
            projectionModesConflict(
                "single_projection_reconstruction_v1",
                "three_projection_sampler_proxy_v1",
            ),
        )
    }

    @Test
    fun `bundled sampler proxy and manifest use unique v1 identity`() {
        val library = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/libgxr_three_projection_sampler_proxy_v1.so",
            ),
        ) { "Missing bundled three-projection sampler proxy layer" }.use { it.readBytes() }
        assertTrue(
            library.size > 4 &&
                library.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46)),
        )
        val sha256 = MessageDigest.getInstance("SHA-256")
            .digest(library)
            .joinToString("") { "%02x".format(it) }
        assertEquals(
            "6139078ffa31a960234184563c2cc1655698d22e920346103a7651ee341f05a2",
            sha256,
        )
        val nativeStrings = library.toString(Charsets.ISO_8859_1)
        assertTrue(nativeStrings.contains("three-projection-sampler-proxy-v1.2-20260829"))
        assertTrue(nativeStrings.contains("proxy_fingerprint_learned"))
        assertTrue(nativeStrings.contains("proxy_stage_progress"))
        assertTrue(nativeStrings.contains("proxy_ready"))
        assertTrue(nativeStrings.contains("three_projection_sampler_proxy_auxiliary"))
        assertTrue(nativeStrings.contains("cacheRefreshed"))
        assertTrue(nativeStrings.contains("three_projection_sampler_proxy_transform"))
        assertTrue(nativeStrings.contains("three_projection_sampler_proxy_texture_state"))
        assertTrue(nativeStrings.contains("three_projection_sampler_proxy_disabled"))
        assertTrue(nativeStrings.contains("three_projection_sampler_proxy_passthrough"))

        val manifest = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/XR_APILAYER_local_GalaxyXR_three_projection_sampler_proxy_v1.json",
            ),
        ).bufferedReader().use { it.readText() }
        assertTrue(manifest.contains("XR_APILAYER_local_GalaxyXR_three_projection_sampler_proxy_v1"))
        assertTrue(manifest.contains("libgxr_three_projection_sampler_proxy_v1.so"))
        assertTrue(manifest.contains("GXR_DISABLE_THREE_PROJECTION_SAMPLER_PROXY"))
    }
}
