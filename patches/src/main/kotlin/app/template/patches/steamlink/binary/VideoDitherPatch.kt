package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.booleanOption
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import app.template.patches.steamlink.util.BinaryPatchHelper.findUniqueAndReplace

// The video fragment shader ships with the dither line commented out with "//" prefix.
// Enabling changes those two bytes to spaces, activating the dither at runtime.
private val SHADER_TAIL = (
    "color.rgb += fract(UniDitherOffsets.a * .43 + UniDitherOffsets.rgb + " +
    "gl_FragCoord.x * 1.67 + gl_FragCoord.y * 1.127 ) * .00292;"
).toByteArray(Charsets.US_ASCII)

private val DISABLED = byteArrayOf('/'.code.toByte(), '/'.code.toByte()) + SHADER_TAIL
private val ENABLED  = byteArrayOf(' '.code.toByte(), ' '.code.toByte()) + SHADER_TAIL

@Suppress("unused")
val videoDitherPatch = rawResourcePatch(
    name = "Video dither",
    description = "Enables (or disables) the dormant GLSL dither term in VRLink's video fragment shader. Reduces 8-bit contouring on OLED displays.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    val enable by booleanOption(
        key = "enable",
        default = true,
        title = "Enable dither",
        description = "True to uncomment the dither line; false to recomment it.",
        required = true,
    )

    execute {
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        val bytes = file.readBytes()
        file.writeBytes(
            if (enable!!) findUniqueAndReplace(bytes, DISABLED, ENABLED)
            else findUniqueAndReplace(bytes, ENABLED, DISABLED)
        )
    }
}
