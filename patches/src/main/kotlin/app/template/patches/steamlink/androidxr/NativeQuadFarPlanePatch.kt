package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_FAR_PLANE
import app.template.patches.shared.Constants.isNativeQuadFarPlaneSteamLinkBuild
import app.template.patches.shared.PatchCategories
import app.template.patches.shared.buildLabel
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_DEFAULT_DISTANCE
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_DISTANCES
import app.template.patches.steamlink.binary.applyNativeQuadFarDistance
import app.template.patches.steamlink.binary.validateNativeQuadFarPayload
import java.io.File
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

private const val STEAM_LINK_PACKAGE = "com.valvesoftware.steamlinkvr"
private const val FAR_PLANE_RESOURCE = "/steamlink/nativequad/far/libgxr_quad_native.so"

/** Loads the bundled far-plane payload (the 1.2 helper compiled with the distance slot). */
internal fun nativeQuadFarResource(): ByteArray =
    (object {}.javaClass.getResourceAsStream(FAR_PLANE_RESOURCE)
        ?: throw PatchException("Missing bundled far-plane resource: $FAR_PLANE_RESOURCE"))
        .use { it.readBytes() }

/**
 * Installs the far-plane helper over the helper the native quad dependency put in place. Returns
 * false, touching nothing, for any package or build outside the exact 2.0.20 registry. Accepts only
 * the pinned 1.2 helper or an already-far payload; the replacement is a single atomic file swap.
 */
internal fun installNativeQuadFarPlane(
    apkRoot: File,
    packageName: String,
    versionName: String,
    versionCode: String,
    meters: Int,
    farPayload: ByteArray? = null,
    moveFile: (Path, Path) -> Unit = { source, target ->
        Files.move(source, target, StandardCopyOption.REPLACE_EXISTING)
    },
): Boolean {
    // Dependencies bypass Morphe compatibility. Leave every other package/build untouched.
    if (packageName != STEAM_LINK_PACKAGE || !isNativeQuadFarPlaneSteamLinkBuild(versionName, versionCode)) {
        return false
    }
    if (meters !in NATIVE_QUAD_FAR_DISTANCES) {
        throw PatchException("Far plane: unsupported quad distance $meters")
    }

    val payload = farPayload ?: nativeQuadFarResource()
    validateNativeQuadFarPayload(payload)

    val target = File(apkRoot, "lib/arm64-v8a/$NATIVE_QUAD_LIBRARY")
    if (!target.isFile) {
        throw PatchException("Far plane needs the native quad composition helper")
    }
    val installed = target.readBytes()
    val replacement = applyNativeQuadFarDistance(installed, meters, payload)
    if (replacement.contentEquals(installed)) return true

    val temporary = File.createTempFile("gxr-native-quad-far-", ".tmp", target.parentFile)
    try {
        temporary.writeBytes(replacement)
        moveFile(temporary.toPath(), target.toPath())
    } catch (failure: Exception) {
        Files.deleteIfExists(temporary.toPath())
        throw failure
    }
    return true
}

@Suppress("unused")
val nativeQuadFarPlanePatch = rawResourcePatch(
    name = "Native quad far plane (experimental)",
    description = "Places the stream quads far from the eye to test whether whole-image flicker stops. " +
        buildLabel(COMPATIBILITIES_STEAM_LINK_FAR_PLANE),
    default = false,
) {
    category(PatchCategories.IMAGE_QUALITY)
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_FAR_PLANE.toTypedArray())
    dependsOn(nativeQuadCompositionPatch)

    val distance by stringOption(
        key = "distance",
        default = NATIVE_QUAD_FAR_DEFAULT_DISTANCE.toString(),
        values = mapOf(
            "16 m" to "16",
            "64 m" to "64",
            "250 m" to "250",
        ),
        title = "Quad distance",
        description = "How far the stream quads sit from the eye. Larger values make head or eye " +
            "position errors move the image less. Stock is about 2 m.",
        required = true,
    )

    execute {
        // Dependencies bypass Morphe compatibility: stay inert on every other package/build.
        if (packageMetadata.packageName != STEAM_LINK_PACKAGE ||
            !isNativeQuadFarPlaneSteamLinkBuild(packageMetadata.versionName, packageMetadata.versionCode)
        ) {
            return@execute
        }
        val raw = distance ?: throw PatchException("Far plane: quad distance is not set")
        val meters = raw.toIntOrNull()
            ?: throw PatchException("Far plane: unknown quad distance '$raw'")
        if (meters !in NATIVE_QUAD_FAR_DISTANCES) {
            throw PatchException("Far plane: unsupported quad distance $meters")
        }
        val helper = get("lib/arm64-v8a/$NATIVE_QUAD_LIBRARY")
        installNativeQuadFarPlane(
            helper.parentFile!!.parentFile!!.parentFile!!,
            packageMetadata.packageName,
            packageMetadata.versionName,
            packageMetadata.versionCode,
            meters,
        )
    }
}
