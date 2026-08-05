package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.bytecodePatch
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.resourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import org.w3c.dom.Element

private fun uiResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/$name")
        ?: error("Missing bundled resource: steamlink/androidxr/$name"))
        .use { it.readBytes() }

internal val androidXrUiExtensionPatch = bytecodePatch {
    extendWith("extensions/extension.mpe")
}

@Suppress("unused")
val managedPanelAspectPatch = bytecodePatch(
    name = "Managed-panel aspect fix",
    description = "Uses the actual Galaxy XR managed-panel surface dimensions instead of the headset's physical display metrics, fixing the stretched launcher UI.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(androidXrUiExtensionPatch)
}

internal val xrUiInputConfigPatch = rawResourcePatch {
    execute {
        get("assets/config/ui_config.json").writeBytes(uiResource("ui_config.json"))
    }
}

@Suppress("unused")
val xrUiInputPatch = bytecodePatch(
    name = "XR launcher input",
    description = "Routes Galaxy XR spatial-pointer, controller, and XR_EXT_hand_interaction events to Steam Link launcher mouse/select input.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(androidXrUiExtensionPatch, xrUiInputConfigPatch)
}

internal val appearOnTopManifestPatch = resourcePatch {
    finalize {
        document("AndroidManifest.xml").use { document ->
            val manifest = document.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
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
    description = "Requests appear-on-top permission before launch and installs the 2x2 transparent compositor overlay used by the full-resolution Galaxy XR path.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(androidXrUiExtensionPatch, appearOnTopManifestPatch)
}
