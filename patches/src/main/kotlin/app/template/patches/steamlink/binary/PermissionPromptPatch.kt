package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import app.template.patches.steamlink.util.BinaryPatchHelper.findUniqueAndReplace

// Replaces RequestAndroidPermissions() with `mov w0,#1; ret` so the runtime
// permission dialog never fires and tears down the live XR stream.
private val SEARCH = byteArrayOf(
    0xff.toByte(), 0x83.toByte(), 0x01.toByte(), 0xd1.toByte(),
    0xfd.toByte(), 0x7b.toByte(), 0x01.toByte(), 0xa9.toByte(),
)
private val REPLACE = byteArrayOf(
    0x20.toByte(), 0x00.toByte(), 0x80.toByte(), 0x52.toByte(),
    0xc0.toByte(), 0x03.toByte(), 0x5f.toByte(), 0xd6.toByte(),
)

@Suppress("unused")
val permissionPromptPatch = rawResourcePatch(
    name = "Disable permission prompt",
    description = "Replaces VRLink's RequestAndroidPermissions with a no-op (return true) to prevent stream teardown on Galaxy XR.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)

    execute {
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        file.writeBytes(findUniqueAndReplace(file.readBytes(), SEARCH, REPLACE))
    }
}
