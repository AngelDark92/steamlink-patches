package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import java.security.MessageDigest

// Exact symbol-derived 5002322 GetProjectionLayers contract: underside/base/fovea,
// flags 0/0/6, 2 views each. Hash the renderer function instead of the whole scene,
// so unrelated guarded OLED/audio/pose changes can coexist. No native bytes change.
internal fun validateSurfaceFoveaLayout(scene: ByteArray) {
    val offset = 0x10b3fc
    val size = 0x2b4
    if (scene.size < 2_283_400 ||
        !scene.copyOfRange(0, 5).contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46, 2)) ||
        scene[18] != 0xb7.toByte() || scene[19] != 0.toByte()
    ) throw PatchException("Surface fovea requires the verified ARM64 5002322 renderer")
    // Other supported patches append an ELF load segment. The renderer region must
    // still be present byte-for-byte at its verified file offset.
    val actual = MessageDigest.getInstance("SHA-256").apply { update(scene, offset, size) }
        .digest().joinToString("") { "%02x".format(it) }
    if (actual != "d9e95dc74ddeaa890e18d6a8804d5278f271ad52df8c49714efbd247a22b81a6") {
        throw PatchException("Surface fovea rejected changed GetProjectionLayers; use a clean supported APK")
    }
}
