package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.resourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import org.w3c.dom.Element
import java.io.File

private fun facebridgeResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/$name")
        ?: error("Missing bundled resource: steamlink/androidxr/$name"))
        .use { it.readBytes() }

private val gxrFacebridgeLibPatch = rawResourcePatch {
    execute {
        val libDir = get("lib/arm64-v8a/libvrlink_scene.so").parentFile!!
        File(libDir, "libgxr_face_bridge.so").writeBytes(facebridgeResource("libgxr_face_bridge.so"))

        // arslib ResourceIdProcessor requires ids.xml to exist even when the APK omits it
        val idsFile = File(libDir.parentFile!!.parentFile!!, "res/values/ids.xml")
        if (!idsFile.exists()) {
            idsFile.parentFile!!.mkdirs()
            idsFile.writeText("""<?xml version="1.0" encoding="utf-8"?><resources/>""")
        }
    }
}

private val gxrFacebridgeManifestPatch = resourcePatch {
    dependsOn(gxrFacebridgeLibPatch)

    finalize {
        document("AndroidManifest.xml").use { doc ->
            val manifest = doc.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
            val perm = "android.permission.FACE_TRACKING"
            val alreadyPresent = (0 until doc.getElementsByTagName("uses-permission").length)
                .map { doc.getElementsByTagName("uses-permission").item(it) as Element }
                .any { it.getAttribute("android:name") == perm }
            if (!alreadyPresent) {
                val el = doc.createElement("uses-permission")
                el.setAttribute("android:name", perm)
                manifest.insertBefore(el, app)
            }
        }
    }
}

@Suppress("unused")
val gxrFacebridgePatch = rawResourcePatch(
    name = "GXR face bridge",
    description = "Installs libgxr_face_bridge.so (XR_FB_face_tracking2 → XR_ANDROID_face_tracking API layer) and adds android.permission.FACE_TRACKING to the manifest.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(gxrFacebridgeManifestPatch)

    execute { /* all work done by sub-patches */ }
}
