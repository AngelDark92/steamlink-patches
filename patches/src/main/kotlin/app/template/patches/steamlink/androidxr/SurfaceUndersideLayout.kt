package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import java.security.MessageDigest

// Exact 2.0.22/5002322 scene: 2283400 bytes, pristine SHA-256
// e61baf34dfc4749d92561bab5fee47891d271607a0ce44824ff61c3e6a450c3f.
// Pin whole symbol-derived renderer functions instead of requiring a pristine whole
// library: the existing recommended OLED/identity/audio/pose edits remain compatible.
// These offsets are ELF file offsets (the executable PT_LOAD maps vaddr == offset).
// No bytes are changed here. Unknown renderer layouts cannot use the experiment.
private data class UndersideRegion(val name: String, val offset: Int, val size: Int, val sha256: String)

private val undersideRegions = listOf(
    UndersideRegion("QSVLRendererXR::Init", 0x10af60, 0x49c,
        "f65c1507aa72fe940ca6bde6f28fd44f3d10d2d116c63fb5a290e4919869c7bb"),
    UndersideRegion("QSVLRendererXR::GetProjectionLayers", 0x10b3fc, 0x2b4,
        "d9e95dc74ddeaa890e18d6a8804d5278f271ad52df8c49714efbd247a22b81a6"),
    UndersideRegion("QSVLRendererXR::FlipFrame", 0x10bf38, 0xbb8,
        "428f96bd3001fb1641246760d5c1cb70147fb2d20f087d616befe0da7e22ad68"),
)

internal fun validateSurfaceUndersideLayout(scene: ByteArray) {
    if (scene.size != 2_283_400 ||
        !scene.copyOfRange(0, 5).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46, 2)) ||
        scene[18] != 0xb7.toByte() || scene[19] != 0.toByte()
    ) throw PatchException("Underside experiment requires the verified ARM64 2.0.22/5002322 scene layout")
    undersideRegions.forEach { region ->
        val digest = MessageDigest.getInstance("SHA-256")
        digest.update(scene, region.offset, region.size)
        val actual = digest.digest().joinToString("") { "%02x".format(it) }
        if (actual != region.sha256) throw PatchException(
            "Underside experiment rejected changed ${region.name}; use the tested terminal quad",
        )
    }
}
