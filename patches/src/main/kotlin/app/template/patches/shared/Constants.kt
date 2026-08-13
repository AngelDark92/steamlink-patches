package app.template.patches.shared

import app.morphe.patcher.patch.ApkFileType
import app.morphe.patcher.patch.AppTarget
import app.morphe.patcher.patch.Compatibility

object Constants {
    const val EXPERIMENTAL_COMPATIBILITY_NAME = "Steam Link Experimental"

    val COMPATIBILITY_STEAM_LINK = Compatibility(
        name = "Steam Link",
        packageName = "com.valvesoftware.steamlinkvr",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(version = "2.0.22"),
            AppTarget(
                version = null,
                versionCodes = null,
                isExperimental = true,
                description = "Unlisted Steam Link versions are experimental and may not patch safely.",
            ),
        )
    )

    val COMPATIBILITY_STEAM_LINK_EXPERIMENTAL = Compatibility(
        name = EXPERIMENTAL_COMPATIBILITY_NAME,
        packageName = "com.valvesoftware.steamlinkvr",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(version = "2.0.22"),
            AppTarget(
                version = null,
                versionCodes = null,
                isExperimental = true,
                description = "Unlisted Steam Link versions are experimental and may not patch safely.",
            ),
        )
    )

    val COMPATIBILITY_STEAM_LINK_HMD_ONLY = Compatibility(
        name = "Steam Link",
        packageName = "com.valvesoftware.steamlinkvr",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(
                version = "2.0.22",
                versionCodes = null,
                description = "HMD-only pose fix layouts verified for versionCodes 5001712, 5002172, 5002206, 5002244, and 5002313.",
            ),
            AppTarget(
                version = null,
                versionCodes = null,
                isExperimental = true,
                description = "Unlisted Steam Link versions are experimental and may not patch safely.",
            ),
        )
    )

}
