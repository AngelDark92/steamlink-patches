package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.bytecodePatch
import app.morphe.patcher.patch.rawResourcePatch

private fun uiResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/$name")
        ?: error("Missing bundled resource: steamlink/androidxr/$name"))
        .use { it.readBytes() }

internal val androidXrUiExtensionPatch = bytecodePatch {
    extendWith("extensions/extension.mpe")
}

internal val xrUiInputConfigPatch = rawResourcePatch {
    execute {
        get("assets/config/ui_config.json").writeBytes(uiResource("ui_config.json"))
    }
}
