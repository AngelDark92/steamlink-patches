package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.PatchCategories
import app.template.patches.shared.buildLabel

@Suppress("unused")
val swapchainEnlargePatch = rawResourcePatch(
    name = "Swapchain enlarge (experimental)",
    description = "Enlarges Steam Link's projection swapchains up to 1.5x without adding a layer. " +
        buildLabel(resolutionLayerCompatibilities) +
        " Needs: deselect Alpha-0 and Atlas surface.",
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
            ResolutionLayer.SWAPCHAIN_ENLARGE,
        )
    }
}
