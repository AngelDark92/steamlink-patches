package app.template.patches.steamlink.decoder

import app.morphe.patcher.extensions.InstructionExtensions.replaceInstruction
import app.morphe.patcher.patch.bytecodePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL

// Replaces the IGET of m_nLowLatencyVideoMode with const/4 p0, 0x1 so
// findBestDecoder() always selects the KEY_LOW_LATENCY hardware decoder path.
@Suppress("unused")
val lowLatDecoderPatch = bytecodePatch(
    name = "Low-latency decoder",
    description = "Forces findBestDecoder() to always select the low-latency hardware decoder (KEY_LOW_LATENCY), reducing decode jitter from ~11 ms median to ≤8 ms on Galaxy XR.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)

    execute {
        LowLatDecoderFingerprint.method.replaceInstruction(0, "const/4 p0, 0x1")
    }
}
