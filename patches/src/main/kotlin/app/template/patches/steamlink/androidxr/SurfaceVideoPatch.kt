package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.resourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL
import java.io.File
import java.security.MessageDigest

internal const val SURFACE_VIDEO_MODE = "surface_video_v1"
internal const val SURFACE_VIDEO_LIBRARY = "libgxr_surface_video.so"
internal const val SURFACE_VIDEO_MANIFEST = "XR_APILAYER_local_GalaxyXR_surface_video_v1.json"

internal data class SurfaceVideoBase(
    val version: String,
    val code: String,
    val sceneSize: Int,
    val wrapperOffset: Int,
    val wrapperHash: String,
)

// Fingerprint the actual creation wrapper, which other supported shader/timing patches
// do not edit. Full pristine hashes and active caller traces are in the decoded audit.
internal val surfaceVideoBases = listOf(
    SurfaceVideoBase("2.0.20", "5001712", 2221072, 0x13562c,
        "7a57c67a4505c1a6180e86f4c0c00b6385ad0cc50f4e74acd220238456eb6e9a"),
    SurfaceVideoBase("2.0.22", "5002322", 2283400, 0x13a1c8,
        "bcd3843259261709c96cf09ca8c8d752e38bfef7c1fb82cac238a0aea1994817"),
)

internal data class SurfaceVideoAnchor(val offset: Int, val size: Int, val hash: String)
private val surfaceVideoAnchors = mapOf(
    "5001712" to listOf(
        SurfaceVideoAnchor(0x13562c, 1412, "7a57c67a4505c1a6180e86f4c0c00b6385ad0cc50f4e74acd220238456eb6e9a"),
        SurfaceVideoAnchor(0x10ad18, 2772, "bc5e4118046726343cba0edd0d40793cd53dafe5b99db9f3935f3e713b9f5b1d"),
        SurfaceVideoAnchor(0xf1af4, 616, "4c905f1e24f02ea5e0c99bdba69aab311443c14af9a85f03005520985d490e7b"),
        SurfaceVideoAnchor(0x1385f8, 176, "7c2f1bcaca503411e401bffc2e3c13de77146543a354e1ef94f0a629dfe48515"),
        SurfaceVideoAnchor(0x1386ac, 80, "4a478be49191ed6a3765fa8007dc2bfc73094af57bdabf7701d0fc6707f57016"),
    ),
    "5002322" to listOf(
        SurfaceVideoAnchor(0x13a1c8, 1412, "bcd3843259261709c96cf09ca8c8d752e38bfef7c1fb82cac238a0aea1994817"),
        SurfaceVideoAnchor(0x10bf38, 3000, "428f96bd3001fb1641246760d5c1cb70147fb2d20f087d616befe0da7e22ad68"),
        SurfaceVideoAnchor(0xf0d70, 616, "048581c295576196be621b112858f13b30dddc2093d5a0bc256c81e4eb0146a6"),
        SurfaceVideoAnchor(0x13dafc, 176, "a8e9cf2ed93612d299168b291b50816d7c0da782847f2495b29db365c81a786f"),
        SurfaceVideoAnchor(0x13dbb0, 80, "d24411e9a5d2ffb29f5ff14159721e19c72a04fb86e9fe20bb74e9e546863a4e"),
    ),
)

internal fun surfaceVideoBase(version: String, code: String) =
    surfaceVideoBases.singleOrNull { it.version == version && it.code == code }

internal fun surfaceVideoResource(base: SurfaceVideoBase, precision: String): String {
    val fp16 = when (precision) {
        "srgb8" -> 0
        "fp16-linear" -> 1
        else -> throw PatchException("Unknown Android Surface video precision: $precision")
    }
    return "libgxr_surface_video_${base.code}_$fp16.so"
}

internal fun validateSurfaceVideoScene(base: SurfaceVideoBase, scene: ByteArray) {
    if (scene.size != base.sceneSize) throw PatchException("Surface video: unknown native size for ${base.code}")
    retiredNativeProjectionHook(scene)?.let {
        throw PatchException("Surface video found retired native hook $it. Start from the original APK.")
    }
    for (anchor in surfaceVideoAnchors.getValue(base.code)) {
        val hash = MessageDigest.getInstance("SHA-256")
            .digest(scene.copyOfRange(anchor.offset, anchor.offset + anchor.size))
            .joinToString("") { "%02x".format(it) }
        if (hash != anchor.hash) throw PatchException("Surface video: unrecognized renderer/ownership path for ${base.code} at 0x${anchor.offset.toString(16)}")
    }
}

/** Production installer is also exercised against the actual decoded bases by the audit. */
internal fun installSurfaceVideo(root: File, version: String, code: String, precision: String): Boolean {
    val base = surfaceVideoBase(version, code) ?: return false // Before any APK reads/writes.
    val scene = File(root, "lib/arm64-v8a/libvrlink_scene.so")
    validateSurfaceVideoScene(base, scene.readBytes())
    val name = surfaceVideoResource(base, precision)
    val bytes = projectionModeResource(name)
    if (bytes.size < 20 || !bytes.copyOfRange(0, 5).contentEquals(byteArrayOf(127, 69, 76, 70, 2)) ||
        bytes[18] != 0xb7.toByte() || bytes[19] != 0.toByte()) {
        throw PatchException("Surface video resource is not an arm64 ELF: $name")
    }
    installProjectionModeResources(scene.parentFile!!,
        File(root, "assets/openxr/1/api_layers/implicit.d"),
        ProjectionModeResources(SURFACE_VIDEO_MODE, SURFACE_VIDEO_LIBRARY, SURFACE_VIDEO_MANIFEST), bytes)
    return true
}

@Suppress("unused")
val xrAndroidSurfaceVideoPatch = resourcePatch(
    name = "Android Surface actual video (experimental)",
    description = "Copies every rendered video projection, including foveal alpha, into XR_KHR_android_surface_swapchain on exact 2.0.20/5001712 and 2.0.22/5002322. Separate 8-bit sRGB and FP16 linear Surface choices. GPU copy, not direct decoder output; headset acceptance, latency and HDR/panel depth are unverified. Conflicts with the high-resolution trigger and recommended bundles. No kernel changes.",
    default = false,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL.filter { compatibility ->
        compatibility.targets.any { target -> surfaceVideoBases.any { base ->
            target.version == base.version && target.versionCodes?.values?.contains(base.code.toInt()) == true
        } }
    }.toTypedArray())

    val surfacePrecision by stringOption(
        key = "surfacePrecision",
        default = "srgb8",
        values = mapOf("8-bit sRGB Surface (first test)" to "srgb8",
            "FP16 linear scRGB Surface (precision experiment)" to "fp16-linear"),
        title = "Actual video Surface precision",
        description = "Start with 8-bit to test the presentation path. FP16 requires matching EGL/window support and keeps alpha precision. Match OLED source output separately; FP16 cannot recover an 8-bit source. Neither option enables HDR or forces panel depth. Unsupported Surface setup passes through the original video.",
        required = true,
    )
    execute {
        // Dependencies can bypass compatibility. Guard the complete body independently.
        if (surfaceVideoBase(packageMetadata.versionName, packageMetadata.versionCode) == null) return@execute
        // Validate manifest conflicts before resource mutation, irrespective of patch order.
        document("AndroidManifest.xml").use { doc ->
            val nodes = doc.getElementsByTagName("meta-data")
            for (i in 0 until nodes.length) {
                val node = nodes.item(i) as? org.w3c.dom.Element ?: continue
                if (node.getAttribute("android:name") == "com.valvesoftware.steamlink.GXR_RESOLUTION_MODE" &&
                    projectionModesConflict(node.getAttribute("android:value"), SURFACE_VIDEO_MODE)) {
                    throw PatchException("Actual Surface video conflicts with the selected resolution mode. Use a fresh APK and deselect the recommended bundle/high-resolution trigger.")
                }
            }
        }
        val scene = get("lib/arm64-v8a/libvrlink_scene.so")
        installSurfaceVideo(scene.parentFile!!.parentFile!!.parentFile!!,
            packageMetadata.versionName, packageMetadata.versionCode, surfacePrecision!!)
        ensureIdsXml(get("res/values/ids.xml"))
    }
    finalize {
        if (surfaceVideoBase(packageMetadata.versionName, packageMetadata.versionCode) == null) return@finalize
        document("AndroidManifest.xml").use { configurePermissionFreeProjectionMode(it, SURFACE_VIDEO_MODE) }
    }
}
