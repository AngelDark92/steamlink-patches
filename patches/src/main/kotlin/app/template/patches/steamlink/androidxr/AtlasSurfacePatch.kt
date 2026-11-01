package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.PatchCategories
import app.template.patches.shared.buildLabel

@Suppress("unused")
val atlasSurfacePatch = rawResourcePatch(
    name = "Atlas surface (experimental)",
    description = "Presents Steam Link's projection views through a runtime Android Surface at the " +
        "runtime's max extent without adding a layer. " +
        buildLabel(resolutionLayerCompatibilities) +
        " Needs: deselect Alpha-0 and Swapchain enlarge.",
    default = false,
) {
    category(PatchCategories.IMAGE_QUALITY)
    compatibleWith(*resolutionLayerCompatibilities.toTypedArray())

    execute {
        if (packageMetadata.packageName != "com.valvesoftware.steamlinkvr" ||
            !isResolutionLayerBuild(
                packageMetadata.versionName,
                packageMetadata.versionCode,
            )) {
            return@execute
        }
        val sceneFile = get("lib/arm64-v8a/libvrlink_scene.so")
        val apkRoot = sceneFile.parentFile!!.parentFile!!.parentFile!!
        installResolutionLayerResources(
            apkRoot,
            packageMetadata.versionName,
            packageMetadata.versionCode,
            ResolutionLayer.ATLAS_SURFACE,
        )
    }
}
