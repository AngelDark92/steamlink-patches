package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import java.security.MessageDigest

private const val OLD_SCENE_PROBE_SHA256 =
    "826006ea99befe2e6ae894c27cde3fd6d65509950b7d26fcd4862c46364a4f53"

private const val OLD_SCENE_PROBE_RESOURCE =
    "/steamlink/androidxr/probes/old5001712-scene/libvrlink_scene.so"

private const val VRLINK_NATIVE_EXIT = """.method private native requestExit()V
.end method
"""

private const val VRLINK_JAVA_EXIT = """.method private requestExit()V
    .locals 0

    invoke-virtual {p0}, Lcom/valvesoftware/steamlink/VRLink;->finishAndRemoveTask()V

    return-void
.end method
"""

private fun ByteArray.sha256(): String =
    MessageDigest.getInstance("SHA-256")
        .digest(this)
        .joinToString("") { "%02x".format(it) }

private fun loadOldSceneProbe(): ByteArray =
    (object {}.javaClass.getResourceAsStream(OLD_SCENE_PROBE_RESOURCE)
        ?: throw PatchException(
            "Missing old-scene probe resource $OLD_SCENE_PROBE_RESOURCE. " +
                "Add the 2.0.20 libvrlink_scene.so probe payload before enabling this patch."
        ))
        .use { it.readBytes() }

@Suppress("unused")
val oldSceneRendererProbePatch = rawResourcePatch(
    name = "TEST EXPERIMENTAL - Old Scene Renderer Probe (2.0.20)",
    description = "A/B probe: replaces libvrlink_scene.so with the supplied 2.0.20 scene renderer binary for first-screen controller-input regression testing.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)

    execute {
        val replacement = loadOldSceneProbe()
        val hash = replacement.sha256()
        if (hash != OLD_SCENE_PROBE_SHA256) {
            throw PatchException(
                "Old-scene probe hash mismatch: expected $OLD_SCENE_PROBE_SHA256, got $hash"
            )
        }

        get("lib/arm64-v8a/libvrlink_scene.so").writeBytes(replacement)
    }
}

@Suppress("unused")
val oldSceneRequestExitBridgePatch = rawResourcePatch(
    name = "TEST EXPERIMENTAL - Old Scene requestExit Bridge",
    description = "A/B probe adapter: rewrites VRLink requestExit() from JNI-native to Java finishAndRemoveTask() for 2.0.20 scene-library compatibility.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL)
    dependsOn(oldSceneRendererProbePatch)

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
