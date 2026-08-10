package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.bytecodePatch
import app.morphe.patcher.patch.resourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import org.w3c.dom.Element

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

@Suppress("unused")
val xrResolutionPermissionExperimentPatch = resourcePatch(
    name = "XR resolution permission experiment",
    description = "Experimental window/permission A/B probe. Turn off Appear on top and do not combine with older resolution test APK modifications.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)
    dependsOn(xrLauncherBootstrapPatch, androidXrUiExtensionPatch)

    val mode by stringOption(
        key = "mode",
        default = "no_window_control",
        values = mapOf(
            "No window control" to "no_window_control",
            "Granted overlay control" to "overlay_granted_control",
            "Denied overlay enforcement probe" to "overlay_denied_probe",
            "Activity-owned application window" to "application_window",
            "SteamLink decor view" to "decor_view",
            "Application window immediately before VR" to "application_window_direct_vr",
        ),
        title = "Experiment mode",
        description = "Select exactly one complete test state. Only Granted overlay control requests Appear on top.",
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

            if (mode == "overlay_granted_control" || mode == "overlay_denied_probe") {
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
