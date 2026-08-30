package app.template.patches.steamlink.androidxr

import java.io.File
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
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
    fun `bundled efficient reconstruction preserves a distinct guarded identity`() {
        val library = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/libgxr_single_projection_reconstruction_efficient_v1.so",
            ),
        ) { "Missing bundled efficient single-projection reconstruction layer" }.use { it.readBytes() }
        assertTrue(
            library.size > 4 &&
                library.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46)),
        )
        val sha256 = MessageDigest.getInstance("SHA-256")
            .digest(library)
            .joinToString("") { "%02x".format(it) }
        assertEquals(
            "66e645d59d9c8422fea6103fdd208745b9066a740bb7519a86ab63883e9fecab",
            sha256,
        )
        val nativeStrings = library.toString(Charsets.ISO_8859_1)
        assertTrue(nativeStrings.contains("single-projection-reconstruction-efficient-v1.0-20260830"))
        assertTrue(nativeStrings.contains("linear_4tap_subpixel_box"))
        assertTrue(nativeStrings.contains("single_projection_reconstruction_success_summary"))
        assertTrue(nativeStrings.contains("summaryReason"))

        val manifest = checkNotNull(
            javaClass.getResourceAsStream(
                "/steamlink/androidxr/XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_efficient_v1.json",
            ),
        ).bufferedReader().use { it.readText() }
        assertTrue(
            manifest.contains(
                "XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_efficient_v1",
            ),
        )
        assertTrue(manifest.contains("libgxr_single_projection_reconstruction_efficient_v1.so"))

        val source = listOf(
            File("extensions/resolution-trace-layer/src/single_projection_reconstruction_efficient_layer.cpp"),
            File("../extensions/resolution-trace-layer/src/single_projection_reconstruction_efficient_layer.cpp"),
        ).firstOrNull(File::isFile)?.readText()
            ?: error("Efficient reconstruction source contract is unavailable")
        val originalSource = listOf(
            File("extensions/resolution-trace-layer/src/resolution_trace_layer.cpp"),
            File("../extensions/resolution-trace-layer/src/resolution_trace_layer.cpp"),
        ).firstOrNull(File::isFile)?.readText()
            ?: error("Original reconstruction source contract is unavailable")
        fun normalized(value: String) = value.replace(Regex("\\s+"), "")
        fun shaderBody(value: String, name: String): String {
            val pattern = Regex(
                "static const char\\* $name = R\\\"\\((.*?)\\)\\\";",
                setOf(RegexOption.DOT_MATCHES_ALL),
            )
            return normalized(checkNotNull(pattern.find(value)) { "Missing $name shader" }.groupValues[1])
        }
        assertEquals(shaderBody(originalSource, "vertex"), shaderBody(source, "vertex"))
        assertEquals(shaderBody(originalSource, "fragment"), shaderBody(source, "fragment"))
        listOf(
            "float rx=1,ry=1;for(int eye=0;eye<2;++eye)",
            "const uint32_t requestedWidth=std::max",
            "const uint32_t requestedHeight=std::max",
            "views[eye]=f.p[1]->views[eye]",
            "views[eye].subImage.swapchain=s.outputs[eye].handle",
            "views[eye].subImage.imageRect={{0,0}",
            "views[eye].subImage.imageArrayIndex=0",
        ).forEach { mappingExpression ->
            val token = normalized(mappingExpression)
            assertTrue(normalized(originalSource).contains(token), "v1 mapping: $mappingExpression")
            assertTrue(normalized(source).contains(token), "efficient mapping: $mappingExpression")
        }
        listOf(
            "constexpr uint32_t kSourceExtent = 1536",
            "constexpr int64_t kSourceFormat = GL_SRGB8_ALPHA8",
            "glTexStorage2D(GL_TEXTURE_2D,1,GL_SRGB8_ALPHA8,kSourceExtent,kSourceExtent)",
            "std::array<GLuint, 2> resolved",
            "for(uint32_t eye=0;eye<2&&ok;++eye)",
            """sourceLayerCount\":3""",
            """sourceProjectionCount\":3""",
            """sourceViewCount\":6""",
            """forwardedLayerCount\":1""",
            """outputProjectionCount\":1""",
            """outputViewCount\":2""",
            """foveaFilter\":\"linear_4tap_subpixel_box""",
            "ci.format=kSourceFormat;ci.sampleCount=1",
            "qualitySettings.layerFlags=XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB",
        ).forEach { invariant -> assertTrue(source.contains(invariant), invariant) }
        assertEquals(1, Regex("glDrawArrays\\(GL_TRIANGLES,0,3\\)").findAll(source).count())
        assertEquals(1, Regex("glBlitFramebuffer\\(").findAll(source).count())
        assertTrue(source.contains("std::array<XrCompositionLayerProjectionView,2> views"))
        assertTrue(source.contains("views[eye]=f.p[1]->views[eye]"))
    }

    @Test
    fun `active reconstruction modes conflict while retired modes are cleanup only`() {
        assertTrue(
            projectionModesConflict(
                "single_projection_reconstruction_v1",
                "single_projection_reconstruction_efficient_v1",
            ),
        )
        assertTrue(
            projectionModesConflict(
                "single_projection_reconstruction_efficient_v1",
                "single_projection_reconstruction_v1",
            ),
        )
        assertFalse(
            projectionModesConflict(
                "two_projection_drop_base_v1",
                "single_projection_reconstruction_efficient_v1",
            ),
        )
        assertFalse(
            projectionModesConflict(
                "three_projection_sampler_proxy_v1",
                "single_projection_reconstruction_efficient_v1",
            ),
        )
    }

    @Test
    fun `retired projection resources are absent`() {
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
        listOf(
            "libgxr_two_projection_drop_base_v1.so",
            "XR_APILAYER_local_GalaxyXR_two_projection_drop_base_v1.json",
            "libgxr_three_projection_sampler_proxy_v1.so",
            "XR_APILAYER_local_GalaxyXR_three_projection_sampler_proxy_v1.json",
        ).forEach { resource ->
            assertNull(javaClass.getResourceAsStream("/steamlink/androidxr/$resource"), resource)
        }
    }
}
