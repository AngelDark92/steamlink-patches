package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import java.security.MessageDigest

private const val VRLINK_NATIVE_EXIT = """.method private native requestExit()V
.end method
"""

private const val VRLINK_JAVA_EXIT = """.method private requestExit()V
    .locals 0

    invoke-virtual {p0}, Lcom/valvesoftware/steamlink/VRLink;->finishAndRemoveTask()V

    return-void
.end method
"""

private const val LEGACY_TWO_LAYER_INPUT_SHA256 =
    "64712089fd46c4f8f73a1ab65667d58edc789ae416e5f57a9d87e591c976703e"
private const val LEGACY_TWO_LAYER_OUTPUT_SHA256 =
    "65f7f4db26cf32c3e28ef65c6e3f76f335cd00eb51153e47fd204a4ddf167eca"

private data class LegacyTwoLayerPatch(
    val name: String,
    val offset: Int,
    val expected: ByteArray,
    val replacement: ByteArray,
)

private val LEGACY_TWO_LAYER_PATCHES = listOf(
    LegacyTwoLayerPatch(
        name = "omit underside projection-layer submission",
        offset = 0x107C18,
        expected = byteArrayOf(0x19, 0x00, 0x09, 0x8b.toByte()),
        replacement = byteArrayOf(0x29, 0x00, 0x00, 0x14),
    ),
    LegacyTwoLayerPatch(
        name = "omit underside swapchain creation while preserving eye-loop latch",
        offset = 0x108238,
        expected = byteArrayOf(0x3c, 0x01, 0x19, 0x8b.toByte()),
        replacement = byteArrayOf(0x1a, 0x00, 0x00, 0x14),
    ),
    LegacyTwoLayerPatch(
        name = "omit left-eye underside acquire/render/release",
        offset = 0x10900C,
        expected = byteArrayOf(0x08, 0xc7.toByte(), 0x82.toByte(), 0x52),
        replacement = byteArrayOf(0x0a, 0x00, 0x00, 0x14),
    ),
    LegacyTwoLayerPatch(
        name = "omit right-eye underside acquire/render/release",
        offset = 0x1090EC,
        expected = byteArrayOf(0x08, 0xd6.toByte(), 0x82.toByte(), 0x52),
        replacement = byteArrayOf(0x0a, 0x00, 0x00, 0x14),
    ),
)

private fun ByteArray.sha256Hex(): String =
    MessageDigest.getInstance("SHA-256")
        .digest(this)
        .joinToString(separator = "") { b -> "%02x".format(b) }

private fun ByteArray.hexBytes(): String = joinToString(" ") { "%02x".format(it) }

@Suppress("unused")
val legacyTwoLayerRendererProbePatch = rawResourcePatch(
    name = "TEST EXPERIMENTAL - Legacy Two-Layer Renderer Probe",
    description = "A/B probe: applies the known 5002244 four-offset patch set to skip underside swapchain creation/submission and restore the legacy two-layer stream topology.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)

    execute {
        val libFile = get("lib/arm64-v8a/libvrlink_scene.so")
        val original = libFile.readBytes()
        val beforeHash = original.sha256Hex()

        if (beforeHash == LEGACY_TWO_LAYER_OUTPUT_SHA256) {
            return@execute
        }

        if (beforeHash != LEGACY_TWO_LAYER_INPUT_SHA256) {
            throw PatchException(
                "Legacy two-layer probe only supports known 5002244 input. sha256=$beforeHash",
            )
        }

        val patched = original.copyOf()

        for (spec in LEGACY_TWO_LAYER_PATCHES) {
            val actual = patched.copyOfRange(spec.offset, spec.offset + spec.expected.size)
            if (!actual.contentEquals(spec.expected)) {
                throw PatchException(
                    "Unexpected bytes for ${spec.name} at 0x${spec.offset.toString(16)}: " +
                        "expected=${spec.expected.hexBytes()} actual=${actual.hexBytes()}",
                )
            }
        }

        for (spec in LEGACY_TWO_LAYER_PATCHES) {
            spec.replacement.copyInto(patched, spec.offset)
        }

        val afterHash = patched.sha256Hex()
        if (afterHash != LEGACY_TWO_LAYER_OUTPUT_SHA256) {
            throw PatchException(
                "Legacy two-layer post-patch hash mismatch. sha256=$afterHash",
            )
        }

        for (spec in LEGACY_TWO_LAYER_PATCHES) {
            val actual = patched.copyOfRange(spec.offset, spec.offset + spec.replacement.size)
            if (!actual.contentEquals(spec.replacement)) {
                throw PatchException("Post-patch byte verification failed for ${spec.name}")
            }
        }

        libFile.writeBytes(patched)
    }
}

@Suppress("unused")
val oldSceneRequestExitBridgePatch = rawResourcePatch(
    name = "TEST EXPERIMENTAL - Old Scene requestExit Bridge",
    description = "A/B probe adapter: rewrites VRLink requestExit() from JNI-native to Java finishAndRemoveTask() without requiring old-scene binary replacement.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)

    execute {
        val vrlinkSmali = get("smali/com/valvesoftware/steamlink/VRLink.smali")
        val original = vrlinkSmali.readText()
        val lineEnding = if (original.contains("\r\n")) "\r\n" else "\n"
        val normalized = original.replace("\r\n", "\n")

        if (normalized.contains(VRLINK_JAVA_EXIT) && !normalized.contains(VRLINK_NATIVE_EXIT)) {
            return@execute
        }

        val nativeCount = normalized.split(VRLINK_NATIVE_EXIT).size - 1
        if (nativeCount != 1) {
            throw PatchException(
                "Unexpected VRLink requestExit declaration count=$nativeCount; refusing lifecycle guess."
            )
        }

        val patched = normalized.replace(VRLINK_NATIVE_EXIT, VRLINK_JAVA_EXIT)
        val output = if (lineEnding == "\r\n") patched.replace("\n", "\r\n") else patched
        vrlinkSmali.writeText(output)
    }
}
