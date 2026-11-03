package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import app.template.patches.shared.PatchCategories
import app.template.patches.shared.buildLabel
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest

internal const val HYBRID_CODEC_LIBRARY = "libgxr_hybrid_codec.so"
internal const val HYBRID_CODEC_LIBRARY_SIZE = 494_432
internal const val HYBRID_CODEC_LIBRARY_SHA256 =
    "995da8563c4216095534ad8bc7866990e1137987d1f9158438d723772bec12fb"

// The helper is only useful while the Codecs Support layer intercepts the scene
// library's libmediandk imports, so it accepts the same exact bases. These are
// the same decoder-import regions the bundled Codecs Support layer hashes at
// runtime; an unknown scene layout fails before any resource is written.
internal val hybridCodecScenePreconditions: Map<String, List<Triple<Int, Int, String>>> = mapOf(
    "2.0.20/5001712" to listOf(
        Triple(0xfd304, 0x80, "6e5dcb4f5094763e948388ca35c6132af242159fc3c22e812f0fe2f4ab4f2649"),
        Triple(0xfdca8, 0x80, "86ceb824e4a972fbac71d7cb6b88a99d5decde4b42fbd1b80cb5d84361a94b67"),
        Triple(0xfd2f8, 0x40, "25174852802b65e709401364232a3dd9490617cfe9983f8bfc948a27a90e11f4"),
    ),
    "2.0.20/5001812" to listOf(
        Triple(0xfd1c0, 0x80, "b1bfbf75b91c7f522411c68d30283c414517ac0ef4f948cb73fbe6cde55385ed"),
        Triple(0xfdb64, 0x80, "2d79c63e6001f5b6fd708027a3241c7004612b55c5158ee773870825c3b875e1"),
        Triple(0xfd1b4, 0x40, "8be10423034e42a7d6c398c853c0d91085420210c0178247dd3ab1960d1e51ee"),
    ),
    "2.0.21/5001968" to listOf(
        Triple(0xf9168, 0x80, "0206212ba391e8486f3d67c41a7373103c02fd41c3ce19432645ad411fa3b52b"),
        Triple(0xf9b0c, 0x80, "faeea4728e0047b25bae83bd24a14b51bd4972c21530180d73332c82b26e1db3"),
        Triple(0xf915c, 0x40, "2f9ccf5df080c89ddee7bdc010c7eeda87f536fac5d164729acbdb82b8f74ad0"),
    ),
    "2.0.22/5002244" to listOf(
        Triple(0xfa4a8, 0x80, "f2efc6f172f7dd0724a79a4fd921bca68fc180bff33182c6f51ce55a4b1c0643"),
        Triple(0xfae4c, 0x80, "3342b1a89b49d2c038ec58e0f7f76e4871f73efcb7cb0cfbebd87212e1de7e2c"),
        Triple(0xfa49c, 0x40, "d7c2ad75cd4e123d937828007c802055a17675cdc9445af8b90b9dc84c7258ac"),
    ),
    "2.0.23/5002363" to listOf(
        Triple(0xfd6fc, 0x80, "f5c43c6aacb69e61c3f929f361612e1b91bb509803d2c14824b9601b3dd8c795"),
        Triple(0xfe0a0, 0x80, "e08a664f0fd116e2735f68ff8d10a0fcbc4eac8e216fd986beec191a078bddb4"),
        Triple(0xfd6f0, 0x40, "3a38bb855e0af4c544b8568414a9e022803a8c47e5a128b20126c84fb40c7eb3"),
    ),
    "2.0.24/5002395" to listOf(
        Triple(0xfe954, 0x80, "ef7f310bbd65499404aa89a18f873c42a87282a16799b84119c75377654003a3"),
        Triple(0xff2f8, 0x80, "49570c2a54e39d80d2f8bb61c190b9d8b821675a79a4d4c4bf5f94bfb7acde3a"),
        Triple(0xfe948, 0x40, "51b57ce6a5c20fab3266923460c046f1247823cb95526252c61a92020f052464"),
    ),
)

internal fun isHybridCodecBuild(versionName: String, versionCode: String): Boolean =
    isCodecsSupportSteamLinkBuild(versionName, versionCode)

private fun hybridSha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
    .digest(bytes).joinToString("") { "%02x".format(it) }

internal fun validateHybridCodecScene(
    scene: ByteArray,
    versionName: String,
    versionCode: String,
) {
    val regions = hybridCodecScenePreconditions["$versionName/$versionCode"]
        ?: throw PatchException("Hybrid codec has no scene contract for $versionName/$versionCode")
    regions.forEach { (offset, size, expectedHash) ->
        if (offset + size > scene.size) {
            throw PatchException("Hybrid codec found a truncated scene library at 0x${offset.toString(16)}")
        }
        if (hybridSha256(scene.copyOfRange(offset, offset + size)) != expectedHash) {
            throw PatchException("Hybrid codec found an unknown scene function at 0x${offset.toString(16)}")
        }
    }
}

internal fun validateHybridCodecPayload(helper: ByteArray) {
    if (helper.size != HYBRID_CODEC_LIBRARY_SIZE) {
        throw PatchException("Bundled hybrid codec helper has an unexpected size")
    }
    if (helper.size < 20 || !helper.copyOfRange(0, 4)
            .contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46)) ||
        helper[4] != 2.toByte() || helper[5] != 1.toByte() ||
        helper[18] != 0xb7.toByte() || helper[19] != 0.toByte()
    ) {
        throw PatchException("Bundled hybrid codec helper is not an ARM64 ELF library")
    }
    if (hybridSha256(helper) != HYBRID_CODEC_LIBRARY_SHA256) {
        throw PatchException("Bundled hybrid codec helper failed its SHA-256 precondition")
    }
    val symbols = helper.toString(Charsets.ISO_8859_1)
    listOf("gxr_hybrid_probe", "gxr_hybrid_on_access_unit").forEach { symbol ->
        if (!symbols.contains(symbol)) {
            throw PatchException("Bundled hybrid codec helper is missing $symbol")
        }
    }
}

/** All layout, payload and dependency preconditions are checked before writes. */
internal fun installHybridCodecResources(
    apkRoot: File,
    versionName: String,
    versionCode: String,
    helperBytes: ByteArray? = null,
): Boolean {
    // Dependencies bypass Morphe compatibility. Excluded pairs must not even read files.
    if (!isHybridCodecBuild(versionName, versionCode)) return false

    val sceneFile = File(apkRoot, "lib/arm64-v8a/libvrlink_scene.so")
    if (!sceneFile.isFile) throw PatchException("Missing exact hybrid codec renderer library")
    validateHybridCodecScene(sceneFile.readBytes(), versionName, versionCode)

    val helper = helperBytes ?: projectionModeResource(HYBRID_CODEC_LIBRARY)
    validateHybridCodecPayload(helper)

    // The helper is only reachable through the Codecs Support layer, which
    // dlopen()s it beside its own module. Never install a floating helper.
    if (!File(sceneFile.parentFile, CODECS_SUPPORT_LIBRARY).isFile) {
        throw PatchException("Hybrid codec needs the Codecs Support layer installed beside it")
    }
    return installHybridCodecFiles(sceneFile.parentFile!!, helper)
}

internal fun installHybridCodecFiles(libDir: File, helper: ByteArray): Boolean {
    validateHybridCodecPayload(helper)
    val target = File(libDir, HYBRID_CODEC_LIBRARY)
    if (target.exists() && (!target.isFile || !target.readBytes().contentEquals(helper))) {
        throw PatchException("Hybrid codec found a stale or modified resource: ${target.path}")
    }
    if (target.exists()) return true
    if (!libDir.isDirectory && !libDir.mkdirs()) {
        throw PatchException("Could not create hybrid codec resource directory")
    }
    val temporary = File.createTempFile("gxr-hybrid-codec-", ".tmp", libDir)
    try {
        temporary.writeBytes(helper)
        // No replacement: an unexpected concurrent file must never be overwritten.
        Files.move(temporary.toPath(), target.toPath())
    } finally {
        Files.deleteIfExists(temporary.toPath())
    }
    return true
}

@Suppress("unused")
val hybridCodecPatch = rawResourcePatch(
    name = "Hybrid codec helper (experimental)",
    description = "Experimental fovea helper for the hybrid ASTC/PyroWave stream; " +
        "decode-only, no compositing claim. " +
        buildLabel(COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL) +
        " Needs: the Codecs Support layer. " +
        "Kill switch: GXR_DISABLE_HYBRID_CODEC.",
    default = false,
) {
    category(PatchCategories.STREAMING_AND_NETWORK)
    compatibleWith(*(COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL).map { exact ->
        Compatibility(
            name = EXPERIMENTAL_COMPATIBILITY_NAME,
            packageName = requireNotNull(exact.packageName),
            targets = exact.targets,
        )
    }.toTypedArray())
    dependsOn(xrCodecsSupportPatch)

    execute {
        if (packageMetadata.packageName != "com.valvesoftware.steamlinkvr" ||
            !isHybridCodecBuild(packageMetadata.versionName, packageMetadata.versionCode)) {
            return@execute
        }
        val sceneFile = get("lib/arm64-v8a/libvrlink_scene.so")
        val apkRoot = sceneFile.parentFile!!.parentFile!!.parentFile!!
        installHybridCodecResources(
            apkRoot,
            packageMetadata.versionName,
            packageMetadata.versionCode,
        )
    }
}
