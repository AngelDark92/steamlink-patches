package app.template.patches.steamlink.androidxr

import java.io.File
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class PermissionSurfaceDiagnosticResourceTest {
    @Test
    fun `pass-through trace payload is exact and does not expose projection experiment identities`() {
        val helper = checkNotNull(javaClass.getResourceAsStream("/steamlink/androidxr/libgxr_pst.so"))
            .use { it.readBytes() }
        assertContentEquals(byteArrayOf(0x7F, 0x45, 0x4C, 0x46), helper.copyOfRange(0, 4))
        assertEquals(
            "5a0bba0117445bad7674f64f45d95b932fe3964687d24432661054d31cffc61d",
            MessageDigest.getInstance("SHA-256").digest(helper)
                .joinToString("") { "%02x".format(it) },
        )
        val strings = helper.toString(Charsets.ISO_8859_1)
        listOf(
            "XR_APILAYER_local_GalaxyXR_permission_surface_trace_v1",
            "permission-surface-trace-v1.1-20260901",
            "permission_surface_matrix_v1",
            "submitted_frame",
            "shouldRender",
            "session_state_changed",
            "recommended_resolution_changed",
            "recommended_resolution_reenumerated_eye",
            "submissionMutation",
        ).forEach { assertTrue(strings.contains(it), it) }
        listOf(
            "single_projection_native_probe_v1",
            "single_projection_reconstruction_efficient_v1",
            "libgxr_nspp.so",
        ).forEach { assertFalse(strings.contains(it), it) }
    }

    @Test
    fun `trace manifest and native source preserve Valve layer submission`() {
        val manifest = checkNotNull(javaClass.getResource(
            "/steamlink/androidxr/XR_APILAYER_local_GalaxyXR_permission_surface_trace_v1.json",
        )).readText()
        assertTrue(manifest.contains("\"library_path\": \"libgxr_pst.so\""))
        assertTrue(manifest.contains("GXR_DISABLE_PERMISSION_SURFACE_TRACE"))

        val source = source("extensions/resolution-trace-layer/src/permission_surface_trace_layer.cpp")
        listOf(
            "state->shouldRender",
            "sampled_view_configuration_eye",
            "sampled_swapchain_contract",
            "XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED",
            "XR_TYPE_EVENT_DATA_RECOMMENDED_RESOLUTION_CHANGED_ANDROID",
            "recommended_resolution_reenumerated_eye",
            "frameContract(info, frame)",
            "g.endFrame(session, info)",
            "\\\"submissionMutation\\\":false",
        ).forEach { assertTrue(source.contains(it), it) }
        assertFalse(source.contains("output.layerCount=1"))
        assertFalse(source.contains("reconstruction"))
        assertFalse(source.contains("extensions.push_back"))
        assertFalse(source.contains("*data = {XR_TYPE_EVENT_DATA_BUFFER}"))
        assertTrue(source.contains("nextCreateApiLayerInstance(\n        createInfo"))
    }

    @Test
    fun `shell-protected controller defines removed hidden visible and reset states`() {
        val bridge = source(
            "patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayBridge.smali",
        )
        val receiver = source(
            "patches/src/main/resources/steamlink/androidxr/smali/com/valvesoftware/steamlink/GxrOverlayDiagnosticReceiver.smali",
        )
        listOf(
            "sDiagnosticControl:Z",
            "sCreateVisibility:I",
            "setDiagnosticState",
            "const-string v0, \"remove\"",
            "const-string v0, \"hidden\"",
            "const-string v0, \"visible\"",
            "const-string v0, \"reset\"",
            "View;->setVisibility(I)V",
        ).forEach { assertTrue(bridge.contains(it), it) }
        assertTrue(receiver.contains("GXR_OVERLAY_DIAGNOSTIC"))
        assertTrue(receiver.contains("getStringExtra"))

        val patchSource = source(
            "patches/src/main/kotlin/app/template/patches/steamlink/androidxr/OptionalXrPatches.kt",
        )
        assertTrue(patchSource.contains("android.permission.DUMP"))
        assertTrue(patchSource.contains("libgxr_pst.so"))
        assertTrue(patchSource.contains("appearOnTopDiagnosticResourcesPatch"))
        assertTrue(patchSource.contains("GxrOverlayDiagnosticReceiver"))
    }

    private fun source(relativePath: String): String = listOf(
        File(relativePath),
        File("../$relativePath"),
    ).firstOrNull(File::isFile)?.readText() ?: error("Missing source: $relativePath")
}
