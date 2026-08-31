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
    fun `bundled efficient reconstruction preserves its guarded identity and sharp sample`() {
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
            "3028a667223774c6dd62f17dee6c07c5f0f67aed205effd3cd133122d291a319",
            sha256,
        )
        val nativeStrings = library.toString(Charsets.ISO_8859_1)
        assertTrue(nativeStrings.contains("single-projection-reconstruction-efficient-v1.4-20260831"))
        assertTrue(nativeStrings.contains("linear_center_1tap"))
        assertFalse(nativeStrings.contains("linear_4tap_subpixel_box"))
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
        fun normalized(value: String) = value.replace(Regex("\\s+"), "")
        listOf(
            "float rx=1,ry=1;for(int eye=0;eye<2;++eye)",
            "checkedOutputExtent(std::max<double>(s.recommendedWidth",
            "checkedOutputExtent(std::max<double>(s.recommendedHeight",
            "kDensityPreservingTier,requestedWidth,requestedHeight",
            "kPanelNativeTier,panelWidth,panelHeight",
            "kReportedMaximumTier,reportedWidth,reportedHeight",
            "views[eye]=f.p[1]->views[eye]",
            "views[eye].subImage.swapchain=s.outputs[eye].handle",
            "views[eye].subImage.imageRect={{0,0}",
            "views[eye].subImage.imageArrayIndex=0",
        ).forEach { mappingExpression ->
            val token = normalized(mappingExpression)
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
            """foveaFilter\":\"linear_center_1tap""",
            "ci.format=kSourceFormat;",
            "ci.sampleCount=1;ci.width=width;ci.height=height",
            "qualitySettings.layerFlags=XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB",
            "XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB",
            "s.dither=glIsEnabled(GL_DITHER)",
            "glDisable(GL_DITHER)",
            "en(GL_DITHER,s.dither)",
            "fixedFunctionDitherDisabled\\\":true",
            "fixedFunctionDitherWasEnabled",
            "reconstruction_dither_state",
            "reconstruction_output_attempt",
            "reconstruction_output_tier_accepted",
            "reconstruction_output_tier_rejected",
            "XR_ANDROID_RECOMMENDED_RESOLUTION_EXTENSION_NAME",
            "XR_TYPE_EVENT_DATA_RECOMMENDED_RESOLUTION_CHANGED_ANDROID",
            "recommended_resolution_outputs_invalidated",
            "publishedStereoViewLimitsGeneration.store(generation+1",
            "if(before!=after||(after&1u))continue",
            "frameDiscarded\\\":true,\\\"retrySameFrame\\\":false",
        ).forEach { invariant -> assertTrue(source.contains(invariant), invariant) }
        assertEquals(1, Regex("glDrawArrays\\(GL_TRIANGLES,0,3\\)").findAll(source).count())
        assertEquals(1, Regex("glBlitFramebuffer\\(").findAll(source).count())
        assertTrue(source.contains("std::array<XrCompositionLayerProjectionView,2> views"))
        assertTrue(source.contains("views[eye]=f.p[1]->views[eye]"))
        assertTrue(source.contains("vec4 inset=texture(foveaTex"))
        assertFalse(source.contains("sampleFovea"))
    }

    @Test
    fun `removed reconstruction modes are cleanup only`() {
        val activeModes = listOf(
            "single_projection_native_renderer_v1",
            "single_projection_reconstruction_efficient_v1",
            "single_projection_native_renderer_dual_v1",
            "single_projection_native_probe_v1",
        )
        activeModes.forEach { existing ->
            activeModes.forEach { requested ->
                assertEquals(
                    existing != requested,
                    projectionModesConflict(existing, requested),
                    "$existing versus $requested",
                )
            }
        }
        assertFalse(
            projectionModesConflict(
                "single_projection_reconstruction_v1",
                "single_projection_reconstruction_efficient_v1",
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
            "libgxr_single_projection_reconstruction_v1.so",
            "XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_v1.json",
            "libgxr_single_projection_fovea_quads_v1.so",
            "XR_APILAYER_local_GalaxyXR_single_projection_fovea_quads_v1.json",
            "libgxr_two_projection_drop_base_v1.so",
            "XR_APILAYER_local_GalaxyXR_two_projection_drop_base_v1.json",
            "libgxr_three_projection_sampler_proxy_v1.so",
            "XR_APILAYER_local_GalaxyXR_three_projection_sampler_proxy_v1.json",
            "libgxr_nqv.so",
            "libgxr_nqvd.so",
        ).forEach { resource ->
            assertNull(javaClass.getResourceAsStream("/steamlink/androidxr/$resource"), resource)
        }
    }
}
