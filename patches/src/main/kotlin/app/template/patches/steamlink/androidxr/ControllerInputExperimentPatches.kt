package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL

private const val VRLINK_NATIVE_EXIT = """.method private native requestExit()V
.end method
"""

private const val VRLINK_JAVA_EXIT = """.method private requestExit()V
    .locals 0

    invoke-virtual {p0}, Lcom/valvesoftware/steamlink/VRLink;->finishAndRemoveTask()V

    return-void
.end method
"""

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
