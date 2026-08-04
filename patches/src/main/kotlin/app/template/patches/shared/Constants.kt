package app.template.patches.shared

import app.morphe.patcher.patch.ApkFileType
import app.morphe.patcher.patch.AppTarget
import app.morphe.patcher.patch.Compatibility

object Constants {
    val COMPATIBILITY_STEAM_LINK = Compatibility(
        name = "Steam Link",
        packageName = "com.valvesoftware.steamlink",
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(version = "2.0.22"),
            AppTarget(version = null, isExperimental = true)
        )
    )
}
