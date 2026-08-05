package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import app.template.patches.steamlink.util.BinaryPatchHelper.findUniqueAndReplace

// Inside SetMaximumFrameQueueLatency, replaces `mov w19, w1` so the stored
// latency budget is larger than the caller value, compensating for wireless lag.
private val ORIG = byteArrayOf(0xf3.toByte(), 0x03, 0x01, 0x2a)
// +32768 µs = add w19, w1, #8, lsl #12
private val FULL = byteArrayOf(0x33, 0x20, 0x40, 0x11)
// +16384 µs = add w19, w1, #4, lsl #12
private val HALF = byteArrayOf(0x33, 0x10, 0x40, 0x11)

@Suppress("unused")
val frameQueueLatencyPatch = rawResourcePatch(
    name = "Frame queue latency offset",
    description = "Adds a fixed offset to VRLink's frame-queue latency budget to compensate for wireless pipeline delay. 'full' adds +32.768 ms; 'half' adds +16.384 ms.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)

    val size by stringOption(
        key = "size",
        default = "half",
        values = mapOf("full" to "full", "half" to "half"),
        title = "Offset size",
        description = "'full' (+32.768 ms) suits ~25-30 ms wireless pipelines; 'half' (+16.384 ms) suits ~15-20 ms pipelines.",
        required = true,
    )

    execute {
        val replacement = if (size == "full") FULL else HALF
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        file.writeBytes(findUniqueAndReplace(file.readBytes(), ORIG, replacement))
    }
}
