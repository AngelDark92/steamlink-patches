package app.template.patches.shared

import app.morphe.patcher.patch.ApkFileType
import app.morphe.patcher.patch.AppTarget
import app.morphe.patcher.patch.Compatibility

object Constants {
    val COMPATIBILITY_STEAM_LINK = Compatibility(
        name = "Steam Link",
        packageName = "com.valvesoftware.steamlinkvr",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(version = "2.0.22"),
        )
    )

    val COMPATIBILITY_STEAM_LINK_EXPERIMENTAL = Compatibility(
        name = "Steam Link Experimental",
        packageName = "com.valvesoftware.steamlinkvr",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(version = "2.0.22", isExperimental = true),
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
                description = "HMD-only pose fix layouts verified for versionCodes 5001712, 5002172, 5002206, and 5002244.",
            )
        )
    )
}
