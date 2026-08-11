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

@Suppress("unused")
val xrResolutionPermissionExperimentPatch = resourcePatch(
    name = "XR resolution permission experiment",
    description = "Experimental window/permission A/B probe. Turn off Appear on top and do not combine with older resolution test APK modifications.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)
    dependsOn(xrLauncherBootstrapPatch, androidXrUiExtensionPatch, resolutionTraceLayerPatch)

    val mode by stringOption(
        key = "mode",
        default = "denied_no_window",
        values = mapOf(
            "Denied permission, no window" to "denied_no_window",
            "Granted permission, no window" to "granted_no_window",
            "Overlay live before VR" to "overlay_live_before_vr",
            "Overlay removed before VR" to "overlay_remove_before_vr",
            "Overlay added after VR" to "overlay_after_vr",
            "Application window immediately before VR" to "application_window_direct_vr",
        ),
        title = "Experiment mode",
        description = "Select exactly one isolated state. The four granted/overlay modes request Appear on top; the denied and application-window modes do not.",
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

            val needsOverlayPermission = mode in setOf(
                "granted_no_window",
                "overlay_live_before_vr",
                "overlay_remove_before_vr",
                "overlay_after_vr",
            )
            if (needsOverlayPermission) {
                if (matchingPermissions.isEmpty()) {
                    val permission = document.createElement("uses-permission")
                    permission.setAttribute("android:name", permissionName)
                    manifest.insertBefore(permission, app)
                }
            } else {
                matchingPermissions.forEach { manifest.removeChild(it) }
            }

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
