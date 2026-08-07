package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import java.io.File

private const val LEGACY_NATIVE_LAYOUT_SIZE = 2_251_920

private val NOP = byteArrayOf(0x1f, 0x20, 0x03, 0xd5.toByte())

private data class NativeEdit(
    val offset: Int,
    val original: ByteArray,
    val replacement: ByteArray,
)

private fun ascii(value: String): ByteArray = value.toByteArray(Charsets.US_ASCII)

private fun paddedAscii(value: String, size: Int): ByteArray =
    ByteArray(size).also { ascii(value).copyInto(it) }

private fun applyNativeEdits(file: File, patchName: String, edits: List<NativeEdit>) {
    val bytes = file.readBytes()
    if (bytes.size != LEGACY_NATIVE_LAYOUT_SIZE) {
        throw PatchException(
            "$patchName supports only the Steam Link 2.0.22 build 5002244 native layout; " +
                "libvrlink_scene.so size=${bytes.size}, expected=$LEGACY_NATIVE_LAYOUT_SIZE",
        )
    }

    val alreadyPatched = edits.map { edit ->
        require(edit.original.size == edit.replacement.size)
        val end = edit.offset + edit.original.size
        if (edit.offset < 0 || end > bytes.size) {
            throw PatchException("$patchName offset 0x${edit.offset.toString(16)} is outside the library")
        }

        val actual = bytes.copyOfRange(edit.offset, end)
        when {
            actual.contentEquals(edit.replacement) -> true
            actual.contentEquals(edit.original) -> false
            else -> throw PatchException(
                "$patchName precondition failed at 0x${edit.offset.toString(16)}: " +
                    "actual=${actual.toHex()}, expected=${edit.original.toHex()} " +
                    "or patched=${edit.replacement.toHex()}",
            )
        }
    }

    if (alreadyPatched.all { it }) return

    val mutable = bytes.copyOf()
    edits.forEachIndexed { index, edit ->
        if (!alreadyPatched[index]) edit.replacement.copyInto(mutable, edit.offset)
    }
    file.writeBytes(mutable)
}

private val permissionNameEdits = listOf(
    NativeEdit(
        offset = 0x93952,
        original = ascii("com.oculus.permission.FACE_TRACKING") + byteArrayOf(0),
        replacement = paddedAscii("android.permission.HAND_TRACKING", 36),
    ),
    NativeEdit(
        offset = 0x9C10E,
        original = ascii("com.oculus.permission.EYE_TRACKING") + byteArrayOf(0, 0x7d, 0),
        replacement = ascii("android.permission.EYE_TRACKING_FINE") + byteArrayOf(0),
    ),
)

private val hmdInitializationEdits = listOf(
    NativeEdit(0xFD040, byteArrayOf(0xe0.toByte(), 0x00, 0x00, 0x36), NOP),
    NativeEdit(0xFD048, byteArrayOf(0xa8.toByte(), 0x00, 0x00, 0x34), NOP),
)

private val lobbyPermissionStateEdits = listOf(
    NativeEdit(0x10B658, byteArrayOf(0x14, 0x04, 0x00, 0x36), NOP),
)

private val streamInitializationEdits = listOf(
    NativeEdit(0x1140AC, byteArrayOf(0x68, 0x00, 0x00, 0x35), NOP),
    NativeEdit(0x1140B4, byteArrayOf(0x68, 0x05, 0x00, 0x34), NOP),
    NativeEdit(0x114168, byteArrayOf(0xa8.toByte(), 0x05, 0x00, 0x34), NOP),
)

@Suppress("unused")
val androidXrNativePermissionNamesPatch = rawResourcePatch(
    name = "Android XR native permission names",
    description = "Replaces native Oculus face/eye permission checks with the Android XR permission names used by the legacy Galaxy XR build.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    execute {
        applyNativeEdits(
            get("lib/arm64-v8a/libvrlink_scene.so"),
            "Android XR native permission names",
            permissionNameEdits,
        )
    }
}

@Suppress("unused")
val forceHmdInitializationGatesPatch = rawResourcePatch(
    name = "Force HMD initialization gates",
    description = "Bypasses the two legacy-tested capability gates in QSVLDeviceHmd::Init for Steam Link build 5002244.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    execute {
        applyNativeEdits(
            get("lib/arm64-v8a/libvrlink_scene.so"),
            "Force HMD initialization gates",
            hmdInitializationEdits,
        )
    }
}

@Suppress("unused")
val forceLobbyPermissionStateGatePatch = rawResourcePatch(
    name = "Force lobby permission-state gate",
    description = "Bypasses the legacy-tested permission-state gate in XrSceneLobby for Steam Link build 5002244.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    execute {
        applyNativeEdits(
            get("lib/arm64-v8a/libvrlink_scene.so"),
            "Force lobby permission-state gate",
            lobbyPermissionStateEdits,
        )
    }
}

@Suppress("unused")
val forceStreamXrGatesPatch = rawResourcePatch(
    name = "Force stream XR gates",
    description = "Bypasses the three legacy-tested XR feature/permission gates in XrSceneStream::Init for Steam Link build 5002244.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    execute {
        applyNativeEdits(
            get("lib/arm64-v8a/libvrlink_scene.so"),
            "Force stream XR gates",
            streamInitializationEdits,
        )
    }
}

private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }