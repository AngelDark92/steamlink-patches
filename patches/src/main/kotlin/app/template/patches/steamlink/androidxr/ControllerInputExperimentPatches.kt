package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import java.io.File
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

private const val LIBVRLINK_SCENE_SIZE_5002244 = 2_251_920

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

private fun decodedRawRootFromLibAnchor(libFile: File): File =
    libFile.parentFile?.parentFile?.parentFile
        ?: throw PatchException("Unable to derive decoded APK raw-resource root from ${libFile.absolutePath}")

private fun findVrLinkSmali(apkRoot: File): File {
    val smaliRoots = apkRoot.listFiles()
        ?.filter { it.isDirectory && it.name.startsWith("smali") }
        .orEmpty()

    val match = smaliRoots
        .asSequence()
        .map { File(it, "com/valvesoftware/steamlink/VRLink.smali") }
        .firstOrNull { it.isFile }

    return match ?: throw PatchException(
        "Unable to locate VRLink.smali under ${apkRoot.absolutePath}; searched: " +
            smaliRoots.joinToString { it.name },
    )
}

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
        if (original.size != LIBVRLINK_SCENE_SIZE_5002244) {
            throw PatchException(
                "Legacy two-layer probe only supports the known 5002244 libvrlink_scene.so size. " +
                    "size=${original.size} sha256=${original.sha256Hex()}",
            )
        }

        val patched = original.copyOf()

        val alreadyPatched = LEGACY_TWO_LAYER_PATCHES.all { spec ->
            patched.copyOfRange(spec.offset, spec.offset + spec.replacement.size)
                .contentEquals(spec.replacement)
        }
        if (alreadyPatched) {
            return@execute
        }

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
        val apkRoot = decodedRawRootFromLibAnchor(get("lib/arm64-v8a/libvrlink_scene.so"))
        val vrlinkSmali = findVrLinkSmali(apkRoot)
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
