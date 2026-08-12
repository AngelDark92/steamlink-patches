package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.bytecodePatch
import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.resourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import org.w3c.dom.Element
import java.io.File

private val unrestrictedBatteryManifestPatch = resourcePatch {
    finalize {
        document("AndroidManifest.xml").use { document ->
            val manifest = document.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
            val permission = "android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS"
            val permissions = document.getElementsByTagName("uses-permission")
            val exists = (0 until permissions.length)
                .mapNotNull { permissions.item(it) as? Element }
                .any { it.getAttribute("android:name") == permission }
            if (!exists) {
                val element = document.createElement("uses-permission")
                element.setAttribute("android:name", permission)
                manifest.insertBefore(element, app)
            }
        }
    }
}

@Suppress("unused")
val unrestrictedBatteryUsagePatch = bytecodePatch(
    name = "Unrestricted battery usage",
    description = "Recommended. Opens Android's per-app Battery usage page at startup so Unrestricted can be selected for XR streaming.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(xrLauncherBootstrapPatch, unrestrictedBatteryManifestPatch)
}

private val appearOnTopManifestPatch = resourcePatch {
    finalize {
        document("AndroidManifest.xml").use { document ->
            val manifest = document.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
            // Required by GxrOverlayBridge to add a TYPE_APPLICATION_OVERLAY compositor signal window
            val permission = "android.permission.SYSTEM_ALERT_WINDOW"
            val permissions = document.getElementsByTagName("uses-permission")
            val exists = (0 until permissions.length)
                .mapNotNull { permissions.item(it) as? Element }
                .any { it.getAttribute("android:name") == permission }
            if (!exists) {
                val element = document.createElement("uses-permission")
                element.setAttribute("android:name", permission)
                manifest.insertBefore(element, app)
            }
        }
    }
}

@Suppress("unused")
val appearOnTopPatch = bytecodePatch(
    name = "Appear on top",
    description = "Recommended. Adds SYSTEM_ALERT_WINDOW to the manifest so GalaxyXRPermissionActivity can request overlay permission at startup.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(xrLauncherBootstrapPatch, appearOnTopManifestPatch)
}

private fun resolutionTraceResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/$name")
        ?: throw PatchException("Missing bundled resolution trace resource: $name"))
        .use { it.readBytes() }

private val resolutionTraceLayerPatch = rawResourcePatch {
    execute {
        val libDir = get("lib/arm64-v8a/libvrlink_scene.so").parentFile!!
        File(libDir, "libgxr_resolution_trace.so").writeBytes(
            resolutionTraceResource("libgxr_resolution_trace.so"),
        )

        val layerManifest = get(
            "assets/openxr/1/api_layers/implicit.d/" +
                "XR_APILAYER_local_GalaxyXR_resolution_trace.json",
        )
        layerManifest.parentFile!!.mkdirs()
        layerManifest.writeBytes(
            resolutionTraceResource("XR_APILAYER_local_GalaxyXR_resolution_trace.json"),
        )
    }
}

private val compositorQuadLayerPatch = rawResourcePatch {
    execute {
        val libDir = get("lib/arm64-v8a/libvrlink_scene.so").parentFile!!
        File(libDir, "libgxr_compositor_probe.so").writeBytes(
            resolutionTraceResource("libgxr_compositor_probe.so"),
        )

        val layerManifest = get(
            "assets/openxr/1/api_layers/implicit.d/" +
                "XR_APILAYER_local_GalaxyXR_compositor_probe.json",
        )
        layerManifest.parentFile!!.mkdirs()
        layerManifest.writeBytes(
            resolutionTraceResource("XR_APILAYER_local_GalaxyXR_compositor_probe.json"),
        )
    }
}

@Suppress("unused")
val xrCompositorPathExperimentPatch = resourcePatch(
    name = "XR compositor path experiment",
    description = "Permission-free baseline or live VRLink activity-window probe. Turn off Appear on top and do not combine with the quad probe.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)
    dependsOn(xrLauncherBootstrapPatch, androidXrUiExtensionPatch, resolutionTraceLayerPatch)

    val mode by stringOption(
        key = "mode",
        default = "denied_no_window",
        values = mapOf(
            "Permission-free baseline" to "denied_no_window",
            "Application window live in VRLink" to "application_window_vrlink_live",
        ),
        title = "Experiment mode",
        description = "Select the no-window baseline or the permission-free token-backed window kept live for the immersive session.",
        required = true,
    )

    finalize {
        document("AndroidManifest.xml").use { document ->
            val manifest = document.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
            val permissionName = "android.permission.SYSTEM_ALERT_WINDOW"
            val permissionNodes = document.getElementsByTagName("uses-permission")
            val matchingPermissions = (0 until permissionNodes.length)
                .mapNotNull { permissionNodes.item(it) as? Element }
                .filter { it.getAttribute("android:name") == permissionName }

            matchingPermissions.forEach { manifest.removeChild(it) }

            val metadataName = "com.valvesoftware.steamlink.GXR_RESOLUTION_MODE"
            val existingMetadata = app.getElementsByTagName("meta-data").let { nodes ->
                (0 until nodes.length)
                    .mapNotNull { nodes.item(it) as? Element }
                    .firstOrNull { it.getAttribute("android:name") == metadataName }
            }
            val metadata = existingMetadata ?: document.createElement("meta-data").also(app::appendChild)
            metadata.setAttribute("android:name", metadataName)
            metadata.setAttribute("android:value", mode!!)
        }
    }
}

@Suppress("unused")
val xrCompositorQuadProbePatch = resourcePatch(
    name = "XR compositor quad probe",
    description = "Experimental permission-free OpenXR quad that tests whether an extra composition layer fixes the low-resolution direct path. Do not combine with XR compositor path experiment.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)
    dependsOn(xrLauncherBootstrapPatch, androidXrUiExtensionPatch, compositorQuadLayerPatch)

    finalize {
        document("AndroidManifest.xml").use { document ->
            val manifest = document.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
            val permissionName = "android.permission.SYSTEM_ALERT_WINDOW"
            val permissionNodes = document.getElementsByTagName("uses-permission")
            (0 until permissionNodes.length)
                .mapNotNull { permissionNodes.item(it) as? Element }
                .filter { it.getAttribute("android:name") == permissionName }
                .forEach { manifest.removeChild(it) }

            val metadataName = "com.valvesoftware.steamlink.GXR_RESOLUTION_MODE"
            val existingMetadata = app.getElementsByTagName("meta-data").let { nodes ->
                (0 until nodes.length)
                    .mapNotNull { nodes.item(it) as? Element }
                    .firstOrNull { it.getAttribute("android:name") == metadataName }
            }
            val metadata = existingMetadata ?: document.createElement("meta-data").also(app::appendChild)
            metadata.setAttribute("android:name", metadataName)
            metadata.setAttribute("android:value", "openxr_quad_compositor_probe")
        }
    }
}
