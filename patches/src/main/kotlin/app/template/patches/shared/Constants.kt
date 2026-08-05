package app.template.patches.shared

import app.morphe.patcher.patch.ApkFileType
import app.morphe.patcher.patch.AppTarget
import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.SupportedAbi

object Constants {
    val COMPATIBILITY_STEAM_LINK = Compatibility(
        name = "Steam Link",
        packageName = "com.valvesoftware.steamlinkvr",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(version = "2.0.22"),
            AppTarget(version = null, isExperimental = true)
        )
    )

    val COMPATIBILITY_STEAM_LINK_EXPERIMENTAL = Compatibility(
        name = "Steam Link Experimental",
        packageName = "com.valvesoftware.steamlinkvr",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(version = "2.0.22", isExperimental = true),
            AppTarget(version = null, isExperimental = true)
        )
    )

    val COMPATIBILITY_STEAM_LINK_HMD_ONLY = Compatibility(
        name = "Steam Link",
        packageName = "com.valvesoftware.steamlinkvr",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(
                version = "2.0.20",
                versionCodes = mapOf(SupportedAbi.ARM64_V8A to 5001712),
                description = "HMD-only pose fix layout verified for versionCode 5001712.",
            ),
            AppTarget(
                version = "2.0.22",
                description = "HMD-only pose fix layouts verified for versionCodes 5002172, 5002206, and 5002244.",
            )
        )
    )
}
