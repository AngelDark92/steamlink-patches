package app.template.patches.shared

import app.morphe.patcher.patch.ApkFileType
import app.morphe.patcher.patch.AppTarget
import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.SupportedAbi

object Constants {
    const val EXPERIMENTAL_COMPATIBILITY_NAME = "Steam Link Experimental"

    private const val STEAM_LINK_PACKAGE = "com.valvesoftware.steamlinkvr"
    private const val STEAM_LINK_VERSION = "2.0.22"
    private val PRE_5002318_BUILDS = intArrayOf(5001712, 5002172, 5002206, 5002244, 5002313)

    private fun steamLinkBuildCompatibility(
        versionCode: Int,
        name: String = "Steam Link",
        description: String = "Verified Steam Link 2.0.22 build $versionCode.",
    ) = Compatibility(
        name = name,
        packageName = STEAM_LINK_PACKAGE,
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(
                version = STEAM_LINK_VERSION,
                versionCodes = SupportedAbi.entries.associateWith { versionCode },
                description = description,
            ),
        ),
    )

    val COMPATIBILITIES_STEAM_LINK_LEGACY =
        PRE_5002318_BUILDS.map(::steamLinkBuildCompatibility)

    private val COMPATIBILITY_STEAM_LINK_5002318 = steamLinkBuildCompatibility(
        versionCode = 5002318,
        description = "Build 5002318 supports only Device identity, OLED color calibration, " +
            "Appear on top, GXR face bridge, Visual Delay Fix, Unrestricted battery usage, " +
            "Video dither, and the experimental XR projection patches.",
    )

    val COMPATIBILITIES_STEAM_LINK =
        COMPATIBILITIES_STEAM_LINK_LEGACY + COMPATIBILITY_STEAM_LINK_5002318

    val COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL =
        (PRE_5002318_BUILDS + 5002318).map { versionCode ->
            steamLinkBuildCompatibility(
                versionCode = versionCode,
                name = EXPERIMENTAL_COMPATIBILITY_NAME,
                description = "Experimental XR projection patches for Steam Link 2.0.22 build $versionCode.",
            )
        }
}
