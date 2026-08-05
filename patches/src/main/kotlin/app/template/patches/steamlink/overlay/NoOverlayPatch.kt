package app.template.patches.steamlink.overlay

import app.morphe.patcher.patch.resourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import app.template.patches.steamlink.androidxr.appearOnTopManifestPatch
import org.w3c.dom.Element

@Suppress("unused")
val noOverlayPatch = resourcePatch(
    name = "No overlay permission",
    description = "Disables the appear-on-top compositor-overlay path while preserving Android XR input and managed-panel aspect fixes.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)
    dependsOn(appearOnTopManifestPatch)
    finalize {
        document("AndroidManifest.xml").use { document ->
            val perms = document.getElementsByTagName("uses-permission")
            for (i in 0 until perms.length) {
                val el = perms.item(i) as? Element ?: continue
                if (el.getAttribute("android:name") == "android.permission.SYSTEM_ALERT_WINDOW") {
                    // Replace with same-length fake name to preserve any internal references.
                    el.setAttribute("android:name", "com.valvesoftware.steamlinkvr.NOOVRLY0")
                    break
                }
            }
        }
    }
}
