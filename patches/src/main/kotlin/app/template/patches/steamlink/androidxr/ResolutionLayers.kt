package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_HIGH_RESOLUTION
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest

internal enum class ResolutionLayer(
    val library: String,
    val manifest: String,
    val buildId: String,
    val helperSha256: String,
    val manifestSha256: String,
) {
    SWAPCHAIN_ENLARGE(
        library = "libgxr_swapchain_enlarge.so",
        manifest = "XR_APILAYER_local_GalaxyXR_swapchain_enlarge_v1.json",
        buildId = "swapchain-enlarge-v1.0-20261027",
        helperSha256 = "d927525267e7e931942c90cc74235dfd5b941b551f69278d74dec3212db6bbcd",
        manifestSha256 = "f6177d5442731d9c90369f9bf1e6a8d6721223ffd6ef94b88e727803ef80bbab",
    ),
    ATLAS_SURFACE(
        library = "libgxr_atlas_surface.so",
        manifest = "XR_APILAYER_local_GalaxyXR_atlas_surface_v1.json",
        buildId = "atlas-surface-v1.0-20261027",
        helperSha256 = "7b306da435f20d4d62e579b9c4effdcf375de501df43f7770e7aebdbd8202f62",
        manifestSha256 = "6863897a084155e947a164fa94983426a48afba4b7d097843e5ce28e2b279fe7",
    ),
}

private data class ScenePin(val size: Long, val sha256: String)

private val RESOLUTION_SCENE_PINS = mapOf(
    ("2.0.20" to "5001712") to ScenePin(
        2_221_072,
        "80b62797c7e26d6b67b0cca00693b076a336bdb48ebc1383a16cccb1616ed495",
    ),
    ("2.0.24" to "5002395") to ScenePin(
        2_301_432,
        "5fab23a7eac1429c93520bdd4d7c579b75a6d55525a755379d15388272571a4b",
    ),
)

internal fun isResolutionLayerBuild(versionName: String, versionCode: String): Boolean =
    (versionName to versionCode) in RESOLUTION_SCENE_PINS

internal val resolutionLayerCompatibilities: List<Compatibility> =
    COMPATIBILITIES_STEAM_LINK_HIGH_RESOLUTION
        .filter { compatibility ->
            compatibility.targets.any { target ->
                val code = target.versionCodes.orEmpty().values.firstOrNull()?.toString()
                code != null && isResolutionLayerBuild(target.version.orEmpty(), code)
            }
        }
        .map { exact ->
            Compatibility(
                name = EXPERIMENTAL_COMPATIBILITY_NAME,
                packageName = requireNotNull(exact.packageName),
                targets = exact.targets,
            )
        }

private fun resolutionLayerResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/$name")
        ?: throw PatchException("Missing bundled resolution layer resource: $name"))
        .use { it.readBytes() }

private fun sha256(bytes: ByteArray): String =
    MessageDigest.getInstance("SHA-256").digest(bytes)
        .joinToString("") { "%02x".format(it) }

internal fun validateResolutionLayerPayload(
    layer: ResolutionLayer,
    helper: ByteArray,
    manifest: ByteArray,
) {
    if (helper.size < 4 || !helper.copyOfRange(0, 4)
            .contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46))) {
        throw PatchException("Bundled ${layer.library} is not an ELF library")
    }
    if (sha256(helper) != layer.helperSha256) {
        throw PatchException("Bundled ${layer.library} failed its SHA-256 precondition")
    }
    if (sha256(manifest) != layer.manifestSha256) {
        throw PatchException("Bundled ${layer.manifest} failed its SHA-256 precondition")
    }
    if (!manifest.toString(Charsets.UTF_8).contains(
            "\"library_path\": \"${layer.library}\"",
        )) {
        throw PatchException("Bundled ${layer.manifest} names the wrong library")
    }
    if (!helper.toString(Charsets.ISO_8859_1).contains(layer.buildId)) {
        throw PatchException("Bundled ${layer.library} build identity is missing")
    }
}

private fun removeRetiredSurfaceVideoResources(libDir: File, layerDir: File) {
    listOf(
        File(libDir, "libgxr_surface_video_trigger.so"),
        File(layerDir, "XR_APILAYER_local_GalaxyXR_surface_video_trigger_v1.json"),
    ).forEach { retired ->
        if (retired.exists() && !retired.delete()) {
            throw PatchException("Could not remove retired surface-video resource: ${retired.path}")
        }
    }
}

internal fun validateResolutionScene(
    sceneBytes: ByteArray,
    versionName: String,
    versionCode: String,
) {
    val pin = requireNotNull(RESOLUTION_SCENE_PINS[versionName to versionCode]) {
        "Resolution layer has no scene contract for $versionName/$versionCode"
    }
    if (sceneBytes.size.toLong() != pin.size) {
        throw PatchException(
            "Resolution layer requires the exact $versionName/$versionCode scene size",
        )
    }
    if (sha256(sceneBytes) != pin.sha256) {
        throw PatchException(
            "Resolution layer requires the exact $versionName/$versionCode scene SHA-256",
        )
    }
}

internal fun installResolutionLayerResources(
    apkRoot: File,
    versionName: String,
    versionCode: String,
    layer: ResolutionLayer,
    helperBytes: ByteArray? = null,
    manifestBytes: ByteArray? = null,
): Boolean {
    // Dependencies bypass Morphe compatibility. Leave every other build untouched.
    if (!isResolutionLayerBuild(versionName, versionCode)) return false
    val sceneFile = File(apkRoot, "lib/arm64-v8a/libvrlink_scene.so")
    if (!sceneFile.isFile) {
        throw PatchException("Missing exact $versionName/$versionCode scene library")
    }
    validateResolutionScene(sceneFile.readBytes(), versionName, versionCode)

    val helper = helperBytes ?: resolutionLayerResource(layer.library)
    val manifest = manifestBytes ?: resolutionLayerResource(layer.manifest)
    validateResolutionLayerPayload(layer, helper, manifest)
    return installResolutionLayerFiles(
        sceneFile.parentFile!!,
        File(apkRoot, "assets/openxr/1/api_layers/implicit.d"),
        layer,
        helper,
        manifest,
    )
}

internal fun installResolutionLayerFiles(
    libDir: File,
    layerDir: File,
    layer: ResolutionLayer,
    helper: ByteArray,
    manifest: ByteArray,
): Boolean {
    validateResolutionLayerPayload(layer, helper, manifest)
    // Alpha-0 keeps Valve's projection layers and appends its own quad. The
    // resolution layers re-point or grow those projections, so both orders are
    // rejected instead of silently producing a wrong frame.
    if (File(libDir, ANDROID_SURFACE_TRIGGER_LIBRARY).exists() ||
        File(layerDir, ANDROID_SURFACE_TRIGGER_MANIFEST).exists()) {
        throw PatchException("${layer.library} needs Alpha-0 high-resolution trigger deselected")
    }
    // Stock quad replaces the projection layers with its own quads; the resolution layers
    // rewrite those same projections. Reject either order before any write.
    if (File(libDir, STOCK_QUAD_LIBRARY).exists() || File(layerDir, STOCK_QUAD_MANIFEST).exists()) {
        throw PatchException("${layer.library} needs Stock quad composition deselected")
    }
    val sibling = when (layer) {
        ResolutionLayer.SWAPCHAIN_ENLARGE -> ResolutionLayer.ATLAS_SURFACE
        ResolutionLayer.ATLAS_SURFACE -> ResolutionLayer.SWAPCHAIN_ENLARGE
    }
    if (File(libDir, sibling.library).exists() || File(layerDir, sibling.manifest).exists()) {
        throw PatchException(
            "Galaxy XR resolution layers are mutually exclusive: " +
                "${layer.library} conflicts with ${sibling.library}",
        )
    }

    removeRetiredSurfaceVideoResources(libDir, layerDir)

    val files = listOf(
        File(libDir, layer.library) to helper,
        File(layerDir, layer.manifest) to manifest,
    )
    files.forEach { (target, expected) ->
        if (target.exists() && (!target.isFile || !target.readBytes().contentEquals(expected))) {
            throw PatchException("Resolution layer found a stale or modified resource: ${target.path}")
        }
    }
    val missing = files.filterNot { (target, _) -> target.exists() }
    if (missing.isEmpty()) return true

    val staged = mutableListOf<Pair<File, File>>()
    val created = mutableListOf<File>()
    try {
        missing.forEach { (target, bytes) ->
            if (!target.parentFile.isDirectory && !target.parentFile.mkdirs()) {
                throw PatchException("Could not create resolution layer resource directory")
            }
            val temporary = File.createTempFile("gxr-resolution-layer-", ".tmp", target.parentFile)
            staged += temporary to target
            temporary.writeBytes(bytes)
        }
        staged.forEach { (temporary, target) ->
            Files.move(temporary.toPath(), target.toPath())
            created += target
        }
    } catch (failure: Exception) {
        created.asReversed().forEach { target ->
            try { Files.deleteIfExists(target.toPath()) } catch (rollback: Exception) {
                failure.addSuppressed(rollback)
            }
        }
        throw failure
    } finally {
        staged.forEach { (temporary, _) -> Files.deleteIfExists(temporary.toPath()) }
    }
    return true
}
