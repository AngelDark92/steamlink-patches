package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException

internal const val NATIVE_SINGLE_PROJECTION_MODE = "single_projection_native_renderer_v1"
internal const val NATIVE_SINGLE_PROJECTION_LIBRARY = "libgxr_nsp.so"
internal const val NATIVE_QUAD_VIEW_MODE = "single_projection_native_quad_zero_copy_v1"
internal const val NATIVE_QUAD_VIEW_LIBRARY = "libgxr_nqv.so"
internal const val NATIVE_SINGLE_PROJECTION_STOCK_SHA256 =
    "e61baf34dfc4749d92561bab5fee47891d271607a0ce44824ff61c3e6a450c3f"

private const val SCENE_SIZE_5002322 = 2_283_400
private const val OPENXR_NEEDED_OFFSET = 0x69635
private const val REQUEST_EXIT_SYMBOL_OFFSET = 0x3EB5E
private const val STREAM_END_FRAME_CALL_OFFSET = 0x11AA7C

private val BUILD_ID_5002322 =
    "585d88d646a8c6efe94bdd9fc6c9dbbc68fc13ba".hexBytes()
private val STOCK_OPENXR_NEEDED = "libopenxr_loader.so\u0000".encodeToByteArray()
private val STOCK_REQUEST_EXIT_SYMBOL = "xrRequestExitSession\u0000".encodeToByteArray()
private val PATCHED_REQUEST_EXIT_SYMBOL = paddedAscii("gxrEndFrame", STOCK_REQUEST_EXIT_SYMBOL.size)
private val STOCK_STREAM_END_FRAME_CALL = byteArrayOf(0xF1.toByte(), 0xF9.toByte(), 0x03, 0x94.toByte())
private val PATCHED_STREAM_END_FRAME_CALL = byteArrayOf(0x55, 0xFE.toByte(), 0x03, 0x94.toByte())
private val NATIVE_HELPER_LIBRARIES = listOf(
    NATIVE_SINGLE_PROJECTION_LIBRARY,
    NATIVE_QUAD_VIEW_LIBRARY,
)

private data class NativeHookSite(
    val name: String,
    val offset: Int,
    val stock: ByteArray,
    val patched: ByteArray,
)

private data class NativeHookGuard(
    val name: String,
    val offset: Int,
    val expected: ByteArray,
)

private val NATIVE_HOOK_STRUCTURAL_GUARDS = listOf(
    NativeHookGuard(
        "OpenXR DT_NEEDED entry",
        0x227D38,
        "01000000000000007d11040000000000".hexBytes(),
    ),
    NativeHookGuard(
        "xrRequestExitSession dynsym entry",
        0x1198,
        "a66601001200000000000000000000000000000000000000".hexBytes(),
    ),
    NativeHookGuard(
        "xrRequestExitSession JUMP_SLOT relocation",
        0x8C880,
        "d8e5220000000000020400009c0000000000000000000000".hexBytes(),
    ),
    NativeHookGuard(
        "xrRequestExitSession GOT slot",
        0x22A5D8,
        "8071210000000000".hexBytes(),
    ),
    NativeHookGuard(
        "xrRequestExitSession PLT stub",
        0x21A3D0,
        "b000009011ee42f91062179120021fd6".hexBytes(),
    ),
    NativeHookGuard(
        "XrSceneStream::Render pre-call context",
        0x11AA70,
        "e82702a9ec2b0629e1630091".hexBytes(),
    ),
    NativeHookGuard(
        "XrSceneStream::Render post-call context",
        0x11AA80,
        "e503002a0003f83733008052e02740f9".hexBytes(),
    ),
    NativeHookGuard(
        "XRQRequestExitSession call context",
        0x14205C,
        "000440f9dc6003946001f83720008052".hexBytes(),
    ),
)

internal fun patchNativeSingleProjectionRenderer(
    bytes: ByteArray,
    targetLibrary: String = NATIVE_SINGLE_PROJECTION_LIBRARY,
): ByteArray {
    if (targetLibrary !in NATIVE_HELPER_LIBRARIES) {
        throw PatchException("Unsupported native projection helper: $targetLibrary")
    }
    if (bytes.size != SCENE_SIZE_5002322) {
        throw PatchException(
            "Native single-projection renderer requires exact 5002322 libvrlink_scene.so " +
                "size=$SCENE_SIZE_5002322, found=${bytes.size}",
        )
    }
    if (bytes.countOccurrences(BUILD_ID_5002322) != 1) {
        throw PatchException("Native single-projection renderer build-id precondition failed")
    }
    NATIVE_HOOK_STRUCTURAL_GUARDS.forEach { guard ->
        val actual = bytes.sliceArray(guard.offset until guard.offset + guard.expected.size)
        if (!actual.contentEquals(guard.expected)) {
            throw PatchException(
                "Native single-projection renderer ELF guard failed at ${guard.name} " +
                    "offset=0x${guard.offset.toString(16)} actual=${actual.toHex()}",
            )
        }
    }

    val targetOpenXrNeeded = paddedAscii(targetLibrary, STOCK_OPENXR_NEEDED.size)
    val siblingOpenXrNeeded = NATIVE_HELPER_LIBRARIES
        .filterNot { it == targetLibrary }
        .associateWith { paddedAscii(it, STOCK_OPENXR_NEEDED.size) }
    val actualOpenXrNeeded = bytes.sliceArray(
        OPENXR_NEEDED_OFFSET until OPENXR_NEEDED_OFFSET + STOCK_OPENXR_NEEDED.size,
    )
    val existingSibling = siblingOpenXrNeeded.entries
        .firstOrNull { (_, value) -> actualOpenXrNeeded.contentEquals(value) }
        ?.key

    val nativeHookSites = listOf(
        NativeHookSite(
            "OpenXR DT_NEEDED",
            OPENXR_NEEDED_OFFSET,
            STOCK_OPENXR_NEEDED,
            targetOpenXrNeeded,
        ),
        NativeHookSite(
            "xrRequestExitSession dynsym",
            REQUEST_EXIT_SYMBOL_OFFSET,
            STOCK_REQUEST_EXIT_SYMBOL,
            PATCHED_REQUEST_EXIT_SYMBOL,
        ),
        NativeHookSite(
            "XrSceneStream::Render xrEndFrame BL",
            STREAM_END_FRAME_CALL_OFFSET,
            STOCK_STREAM_END_FRAME_CALL,
            PATCHED_STREAM_END_FRAME_CALL,
        ),
    )
    val commonStates = nativeHookSites.drop(1).map { site ->
        val actual = bytes.sliceArray(site.offset until site.offset + site.stock.size)
        when {
            actual.contentEquals(site.stock) -> false
            actual.contentEquals(site.patched) -> true
            else -> throw PatchException(
                "Native single-projection renderer precondition failed at ${site.name} " +
                    "offset=0x${site.offset.toString(16)} actual=${actual.toHex()}",
            )
        }
    }

    if (existingSibling != null) {
        if (commonStates.all { it }) {
            throw PatchException(
                "Native projection helper conflict: $targetLibrary conflicts with $existingSibling",
            )
        }
        throw PatchException(
            "Native projection renderer has a mixed sibling/partial-patch state: " +
                "$targetLibrary conflicts with $existingSibling",
        )
    }

    val openXrNeededState = when {
        actualOpenXrNeeded.contentEquals(STOCK_OPENXR_NEEDED) -> false
        actualOpenXrNeeded.contentEquals(targetOpenXrNeeded) -> true
        else -> throw PatchException(
            "Native single-projection renderer precondition failed at OpenXR DT_NEEDED " +
                "offset=0x${OPENXR_NEEDED_OFFSET.toString(16)} actual=${actualOpenXrNeeded.toHex()}",
        )
    }
    val states = listOf(openXrNeededState) + commonStates
    if (states.all { it }) return bytes
    if (states.any { it }) {
        throw PatchException("Native single-projection renderer has a mixed partial-patch state")
    }

    return bytes.copyOf().also { patched ->
        nativeHookSites.forEach { site -> site.patched.copyInto(patched, site.offset) }
    }
}

private fun paddedAscii(value: String, size: Int): ByteArray =
    ByteArray(size).also { output ->
        val encoded = value.encodeToByteArray()
        require(encoded.size < size)
        encoded.copyInto(output)
    }

private fun String.hexBytes(): ByteArray =
    chunked(2).map { it.toInt(16).toByte() }.toByteArray()

private fun ByteArray.countOccurrences(needle: ByteArray): Int {
    var count = 0
    for (offset in 0..size - needle.size) {
        if (needle.indices.all { index -> this[offset + index] == needle[index] }) count++
    }
    return count
}

private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }
