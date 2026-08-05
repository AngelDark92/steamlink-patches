package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL

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

private const val REQUEST_ANDROID_PERMISSIONS_OFFSET = 0x1422c4

internal val disablePermissionPromptNativePatch = rawResourcePatch {
    execute {
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        val bytes = file.readBytes()
        val current = bytes.copyOfRange(
            REQUEST_ANDROID_PERMISSIONS_OFFSET,
            REQUEST_ANDROID_PERMISSIONS_OFFSET + SEARCH.size,
        )
        when {
            current.contentEquals(REPLACE) -> Unit
            current.contentEquals(SEARCH) -> {
                REPLACE.copyInto(bytes, REQUEST_ANDROID_PERMISSIONS_OFFSET)
                file.writeBytes(bytes)
            }
            else -> throw PatchException(
                "Unexpected RequestAndroidPermissions bytes at 0x" +
                    REQUEST_ANDROID_PERMISSIONS_OFFSET.toString(16),
            )
        }
    }
}

@Suppress("unused")
val permissionPromptPatch = rawResourcePatch(
    name = "Disable permission prompt",
    description = "Replaces VRLink's RequestAndroidPermissions with a no-op (return true) to prevent stream teardown on Galaxy XR.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)
    dependsOn(disablePermissionPromptNativePatch)
}
