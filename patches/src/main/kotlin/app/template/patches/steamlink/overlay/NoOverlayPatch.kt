package app.template.patches.steamlink.overlay

import app.morphe.patcher.extensions.InstructionExtensions.addInstructions
import app.morphe.patcher.patch.bytecodePatch
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.resourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import app.template.patches.steamlink.util.BinaryPatchHelper.findUniqueAndReplace
import org.w3c.dom.Element

// libmain.so: replaces the overlay-resolution constant block (16 bytes).
private val LIBMAIN_SEARCH = byteArrayOf(
    0x00, 0xd0.toByte(), 0x42, 0xf9.toByte(), 0xe1.toByte(), 0x13, 0x00, 0x91.toByte(),
    0xe2.toByte(), 0x03, 0x00, 0x91.toByte(), 0x19, 0x56, 0x11, 0x94.toByte(),
)
private val LIBMAIN_REPLACE = byteArrayOf(
    0x08, 0xbc.toByte(), 0x81.toByte(), 0x52, 0xe8.toByte(), 0x07, 0x00, 0xb9.toByte(),
    0x08, 0xe0.toByte(), 0x81.toByte(), 0x52, 0xe8.toByte(), 0x03, 0x00, 0xb9.toByte(),
)

private val noOverlayLibMainPatch = rawResourcePatch {
    execute {
        val file = get("lib/arm64-v8a/libmain.so")
        file.writeBytes(findUniqueAndReplace(file.readBytes(), LIBMAIN_SEARCH, LIBMAIN_REPLACE))
    }
}

private val noOverlayManifestPatch = resourcePatch {
    dependsOn(noOverlayLibMainPatch)

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

@Suppress("unused")
val noOverlayPatch = bytecodePatch(
    name = "No overlay permission",
    description = "Removes SYSTEM_ALERT_WINDOW usage so Steam Link installs and runs without the overlay permission prompt that tears down the XR stream.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    dependsOn(noOverlayManifestPatch)

    execute {
        // Make both overlay methods return immediately so they never request the permission.
        EnsureCompositorOverlayFingerprint.method.addInstructions(0, "return-void")
        RequestOverlayPermissionFingerprint.method.addInstructions(0, "return-void")
    }
}
