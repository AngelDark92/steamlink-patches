package app.template.patches.steamlink.identity

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.resourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL

@Suppress("unused")
val exp6VersionMetadataPatch = resourcePatch(
    name = "EXP6 version metadata",
    description = "Sets AndroidManifest.xml version metadata to the exp6 handoff values (versionCode 5002207, versionName 2.0.22-gxr-exp6).",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)

    finalize {
        document("AndroidManifest.xml").use { doc ->
            val manifest = doc.documentElement
            val targetVersionCode = "5002207"
            val targetVersionName = "2.0.22-gxr-exp6"
            val currentVersionCode = manifest.getAttribute("android:versionCode")
            val currentVersionName = manifest.getAttribute("android:versionName")

            // Keep this patch isolated: fail fast if another patch already rewired
            // either version field to non-exp6 values before this patch runs.
            if (currentVersionCode.isNotBlank() && currentVersionCode != targetVersionCode && currentVersionCode != "5002244") {
                throw PatchException(
                    "EXP6 version metadata patch conflict: AndroidManifest.xml android:versionCode is '$currentVersionCode'.",
                )
            }
            if (currentVersionName.isNotBlank() && currentVersionName != targetVersionName && currentVersionName != "2.0.22") {
                throw PatchException(
                    "EXP6 version metadata patch conflict: AndroidManifest.xml android:versionName is '$currentVersionName'.",
                )
            }

            manifest.setAttribute("android:versionCode", targetVersionCode)
            manifest.setAttribute("android:versionName", targetVersionName)
        }
    }
}
