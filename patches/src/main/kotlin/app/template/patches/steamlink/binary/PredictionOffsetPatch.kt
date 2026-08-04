package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import app.template.patches.steamlink.util.BinaryPatchHelper.findUniqueAndReplace

// In XRQLocateReferenceSpace, replaces `mov x2, x3` (copies XrTime to x2)
// with `add x2, x3, #0xfff, lsl #12` (+16773120 ns = +16.77 ms).
// Apply after frameQueueLatencyPatch; both can coexist since they touch different instructions.
private val ORIG    = byteArrayOf(0xe2.toByte(), 0x03, 0x03, 0xaa.toByte())
private val PATCHED = byteArrayOf(0x62, 0xfc.toByte(), 0x7f, 0x91.toByte())

@Suppress("unused")
val predictionOffsetPatch = rawResourcePatch(
    name = "Pose prediction offset",
    description = "Adds +16.77 ms to the XrTime argument passed to xrLocateSpace, compensating for the Android XR runtime's local-display prediction being too early for a wireless stream.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    execute {
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        file.writeBytes(findUniqueAndReplace(file.readBytes(), ORIG, PATCHED))
    }
}
