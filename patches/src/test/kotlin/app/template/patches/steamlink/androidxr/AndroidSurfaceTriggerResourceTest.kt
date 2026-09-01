package app.template.patches.steamlink.androidxr

import java.io.File
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class AndroidSurfaceTriggerResourceTest {
    @Test
    fun `surface trigger and native probe are mutually exclusive active experiments`() {
        assertFalse(projectionModesConflict("", ANDROID_SURFACE_TRIGGER_MODE))
        assertFalse(projectionModesConflict(ANDROID_SURFACE_TRIGGER_MODE, ANDROID_SURFACE_TRIGGER_MODE))
        assertFalse(projectionModesConflict(NATIVE_SINGLE_PROJECTION_PROBE_MODE, NATIVE_SINGLE_PROJECTION_PROBE_MODE))
        assertTrue(projectionModesConflict(NATIVE_SINGLE_PROJECTION_PROBE_MODE, ANDROID_SURFACE_TRIGGER_MODE))
        assertTrue(projectionModesConflict(ANDROID_SURFACE_TRIGGER_MODE, NATIVE_SINGLE_PROJECTION_PROBE_MODE))
        listOf(
            "single_projection_reconstruction_v1",
            "single_projection_reconstruction_efficient_v1",
            "single_projection_native_renderer_v1",
            "single_projection_native_renderer_dual_v1",
            "two_projection_drop_base_v1",
            "three_projection_sampler_proxy_v1",
        ).forEach { retired ->
            assertFalse(projectionModesConflict(retired, ANDROID_SURFACE_TRIGGER_MODE), retired)
        }
    }

    @Test
    fun `surface trigger manifest and source preserve Valve projections`() {
        val manifest = requireNotNull(javaClass.getResource(
            "/steamlink/androidxr/$ANDROID_SURFACE_TRIGGER_MANIFEST",
        )).readText()
        assertTrue(manifest.contains("\"library_path\": \"$ANDROID_SURFACE_TRIGGER_LIBRARY\""))
        assertTrue(manifest.contains("GXR_DISABLE_ANDROID_SURFACE_TRIGGER"))

        val source = source("extensions/resolution-trace-layer/src/android_surface_trigger_passthrough_layer.cpp")
        listOf(
            ANDROID_SURFACE_TRIGGER_MODE,
            ANDROID_SURFACE_TRIGGER_BUILD_ID,
            "XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME",
            "xrCreateSwapchainAndroidSurfaceKHR",
            "ANativeWindow_fromSurface",
            "WINDOW_FORMAT_RGBA_8888",
            "surface_buffer_queued",
            "surface_trigger_frame",
            "originalPointersPreserved",
            "noCopy",
            "noReconstruction",
            "outputLayerCount",
            "XR_REFERENCE_SPACE_TYPE_VIEW",
            "maxLayerCount < kRequiredLayerCount",
            "XR_SESSION_STATE_VISIBLE",
            "XR_SESSION_STATE_FOCUSED",
            "XR_SWAPCHAIN_USAGE_SAMPLED_BIT",
            "std::shared_ptr<SessionState>",
            "std::mutex sessionsMutex",
            "std::mutex swapchainsMutex",
        ).forEach { invariant -> assertTrue(source.contains(invariant), invariant) }
        listOf("format=0", "sampleCount=0", "faceCount=0", "arraySize=0", "mipCount=0")
            .forEach { invariant -> assertTrue(source.replace(" ", "").contains(invariant), invariant) }
        assertTrue(source.replace(" ", "").contains("output.layers=layers.data()"))
        assertTrue(source.contains("layers[index] = info->layers[index]"))
        assertFalse(source.contains("glDrawArrays"))
        assertFalse(source.contains("glBlitFramebuffer"))
        assertFalse(source.contains("PFN_xrEnumerateSwapchainImages"))
        assertFalse(source.contains("PFN_xrAcquireSwapchainImage"))
        assertFalse(source.contains("PFN_xrWaitSwapchainImage"))
        assertFalse(source.contains("PFN_xrReleaseSwapchainImage"))

        val patchSource = source(
            "patches/src/main/kotlin/app/template/patches/steamlink/androidxr/OptionalXrPatches.kt",
        )
        assertTrue(patchSource.contains("ANDROID_SURFACE_TRIGGER_MANIFEST"))
        assertTrue(patchSource.contains("ANDROID_SURFACE_TRIGGER_STOCK_SCENE_SHA256"))
        assertTrue(patchSource.contains("sceneFile.readBytes().sha256()"))
        assertTrue(patchSource.contains("permission_surface_trace_v1"))
        assertTrue(patchSource.contains("libgxr_pst.so"))
        assertTrue(patchSource.contains("activeProjectionModes.first { it.mode == ANDROID_SURFACE_TRIGGER_MODE }"))
        assertFalse(patchSource.contains("patchNativeEndFrameHelper"))
        assertFalse(patchSource.contains("gxrEndFrame"))
    }

    @Test
    fun `bundled helper is the surface-only API layer`() {
        val helper = requireNotNull(
            javaClass.getResourceAsStream("/steamlink/androidxr/$ANDROID_SURFACE_TRIGGER_LIBRARY"),
        ).use { it.readBytes() }
        assertContentEquals(byteArrayOf(0x7F, 0x45, 0x4C, 0x46), helper.copyOfRange(0, 4))
        assertEquals(
            "21b6c903470b00919953f2e5b72d65bc4dd3cc0b87fb9c4f5d082b2fcee0205f",
            MessageDigest.getInstance("SHA-256").digest(helper)
                .joinToString("") { "%02x".format(it) },
        )
        val strings = helper.toString(Charsets.ISO_8859_1)
        listOf(
            ANDROID_SURFACE_TRIGGER_MODE,
            ANDROID_SURFACE_TRIGGER_BUILD_ID,
            "XR_APILAYER_local_GalaxyXR_android_surface_trigger_passthrough_v1",
            "XR_KHR_android_surface_swapchain",
            "xrCreateSwapchainAndroidSurfaceKHR",
            "surface_buffer_queued",
            "surface_trigger_frame",
        ).forEach { invariant -> assertTrue(strings.contains(invariant), invariant) }
        listOf("reconstruction", "single_projection_native_probe_v1", "decoder_probe_initialized")
            .forEach { retired -> assertFalse(strings.contains(retired), retired) }
    }

    @Test
    fun `retired projection and permission-matrix resources are absent`() {
        listOf(
            "libgxr_pst.so",
            "XR_APILAYER_local_GalaxyXR_permission_surface_trace_v1.json",
            "libgxr_nqv.so",
            "libgxr_nqvd.so",
            "libgxr_nsp.so",
            "libgxr_nspd.so",
            "libgxr_single_projection_reconstruction_efficient_v1.so",
            "XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_efficient_v1.json",
        ).forEach { resource ->
            assertNull(javaClass.getResourceAsStream("/steamlink/androidxr/$resource"), resource)
        }
    }

    private fun source(relativePath: String): String = listOf(
        File(relativePath),
        File("../$relativePath"),
    ).firstOrNull(File::isFile)?.readText() ?: error("Missing source: $relativePath")
}
