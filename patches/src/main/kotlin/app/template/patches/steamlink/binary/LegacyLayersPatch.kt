package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import app.template.patches.steamlink.util.BinaryPatchHelper.findUniqueAndReplace

// Four targeted branch substitutions that skip the 5002244 "underside" swapchain
// and its per-eye acquire/render/release, restoring the 2-layer renderer from 5001712.
private val PATCHES = listOf(
    // omit underside projection-layer submission
    byteArrayOf(0x19, 0x00, 0x09, 0x8b.toByte()) to
    byteArrayOf(0x29, 0x00, 0x00, 0x14),
    // omit underside swapchain creation while preserving the eye-loop latch
    byteArrayOf(0x3c, 0x01, 0x19, 0x8b.toByte()) to
    byteArrayOf(0x1a, 0x00, 0x00, 0x14),
    // omit left-eye underside acquire/render/release
    byteArrayOf(0x08, 0xc7.toByte(), 0x82.toByte(), 0x52) to
    byteArrayOf(0x0a, 0x00, 0x00, 0x14),
    // omit right-eye underside acquire/render/release
    byteArrayOf(0x08, 0xd6.toByte(), 0x82.toByte(), 0x52) to
    byteArrayOf(0x0a, 0x00, 0x00, 0x14),
)

@Suppress("unused")
val legacyLayersPatch = rawResourcePatch(
    name = "Legacy two-layer renderer",
    description = "Restores the 5001712-era two-layer XR stream topology by skipping underside swapchain creation and submission added in 5002244.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)

    execute {
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        var bytes = file.readBytes()
        for ((search, replace) in PATCHES) {
            bytes = findUniqueAndReplace(bytes, search, replace)
        }
        file.writeBytes(bytes)
    }
}
