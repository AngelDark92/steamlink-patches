package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.PatchCategories
import app.template.patches.shared.assertPatchDescriptionContract
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_BUILD_ID
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_DEFAULT_DISTANCE
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_DEFAULT_SLOT
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_DISTANCES
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_HELPER_SHA256
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_HELPER_SIZE
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_MARKER
import app.template.patches.steamlink.binary.NATIVE_QUAD_FAR_SLOT_OFFSET
import app.template.patches.steamlink.binary.applyNativeQuadFarDistance
import app.template.patches.steamlink.binary.isNativeQuadFarPayload
import app.template.patches.steamlink.binary.readNativeQuadFarDistance
import app.template.patches.steamlink.binary.validateNativeQuadFarPayload
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class NativeQuadFarPlanePatchTest {
    private val exactBases = setOf("2.0.20" to "5001712", "2.0.20" to "5001812")
    private val steamLink = "com.valvesoftware.steamlinkvr"

    @Test
    fun `patch is exact 2_0_20 experimental image quality and off by default`() {
        assertEquals("Native quad far plane (experimental)", nativeQuadFarPlanePatch.name)
        assertTrue(nativeQuadFarPlanePatch.name.orEmpty().contains("experimental", ignoreCase = true))
        assertFalse(nativeQuadFarPlanePatch.default)
        assertEquals(PatchCategories.IMAGE_QUALITY, nativeQuadFarPlanePatch.category)
        assertEquals(1, nativeQuadFarPlanePatch.dependencies.size)
        assertTrue(nativeQuadCompositionPatch in nativeQuadFarPlanePatch.dependencies)
        val targets = nativeQuadFarPlanePatch.compatibility.orEmpty().flatMap { compatibility ->
            assertEquals(steamLink, compatibility.packageName)
            compatibility.targets.flatMap { target ->
                target.versionCodes.orEmpty().values.map { target.version to it.toString() }
            }
        }.toSet()
        assertEquals(exactBases, targets)
        assertPatchDescriptionContract(nativeQuadFarPlanePatch)
    }

    @Suppress("DEPRECATION")
    @Test
    fun `quad distance option maps labels to stored values one to one`() {
        val option = nativeQuadFarPlanePatch.options["distance"]
        assertEquals("Quad distance", option.title)
        assertEquals("64", option.default)
        assertEquals(
            mapOf("16 m" to "16", "64 m" to "64", "250 m" to "250"),
            option.values,
        )
        assertEquals(
            NATIVE_QUAD_FAR_DISTANCES.map { it.toString() }.toSet(),
            option.values.orEmpty().values.toSet(),
        )
        assertEquals(NATIVE_QUAD_FAR_DEFAULT_DISTANCE, 64)
    }

    @Test
    fun `bundled far payload matches its pin and slot layout`() {
        val payload = nativeQuadFarResource()
        validateNativeQuadFarPayload(payload)
        assertEquals(NATIVE_QUAD_FAR_HELPER_SIZE, payload.size)
        assertEquals(NATIVE_QUAD_FAR_HELPER_SHA256, sha256(payload))
        assertContentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46), payload.copyOfRange(0, 4))
        assertTrue(payload.toString(Charsets.ISO_8859_1).contains(NATIVE_QUAD_FAR_BUILD_ID))
        assertFalse(payload.toString(Charsets.ISO_8859_1).contains("GXR-NATIVE-QUAD-1.2-ABI1"))
        val marker = NATIVE_QUAD_FAR_MARKER.toByteArray(Charsets.ISO_8859_1)
        val markerOffset = NATIVE_QUAD_FAR_SLOT_OFFSET - marker.size - 1
        assertContentEquals(marker, payload.copyOfRange(markerOffset, markerOffset + marker.size))
        assertEquals(0, payload[NATIVE_QUAD_FAR_SLOT_OFFSET - 1].toInt())
        assertContentEquals(
            NATIVE_QUAD_FAR_DEFAULT_SLOT,
            payload.copyOfRange(NATIVE_QUAD_FAR_SLOT_OFFSET, NATIVE_QUAD_FAR_SLOT_OFFSET + 4),
        )
        // The default slot is the stock distance, not an offered option: it is only the pin.
        assertEquals(null, readNativeQuadFarDistance(payload))
        assertFalse(isNativeQuadFarPayload(payload))
    }

    @Test
    fun `every option value writes exactly the four slot bytes`() {
        val payload = nativeQuadFarResource()
        val helper = nativeQuadResource(NATIVE_QUAD_LIBRARY)
        NATIVE_QUAD_FAR_DISTANCES.forEach { meters ->
            val result = applyNativeQuadFarDistance(helper, meters, payload)
            assertEquals(payload.size, result.size)
            val changed = result.indices.filter { result[it] != payload[it] }
            assertEquals(
                listOf(
                    NATIVE_QUAD_FAR_SLOT_OFFSET, NATIVE_QUAD_FAR_SLOT_OFFSET + 1,
                    NATIVE_QUAD_FAR_SLOT_OFFSET + 2, NATIVE_QUAD_FAR_SLOT_OFFSET + 3,
                ),
                changed,
                meters.toString(),
            )
            assertEquals(meters, readNativeQuadFarDistance(result))
            assertTrue(isNativeQuadFarPayload(result))
        }
    }

    @Test
    fun `reapply is idempotent and distance transitions converge`() {
        val payload = nativeQuadFarResource()
        val helper = nativeQuadResource(NATIVE_QUAD_LIBRARY)
        NATIVE_QUAD_FAR_DISTANCES.forEach { meters ->
            val once = applyNativeQuadFarDistance(helper, meters, payload)
            assertContentEquals(once, applyNativeQuadFarDistance(once, meters, payload), meters.toString())
            assertContentEquals(once, applyNativeQuadFarDistance(helper, meters, payload))
        }
        val sixteen = applyNativeQuadFarDistance(helper, 16, payload)
        val twoFifty = applyNativeQuadFarDistance(sixteen, 250, payload)
        val sixtyFour = applyNativeQuadFarDistance(twoFifty, 64, payload)
        assertContentEquals(applyNativeQuadFarDistance(helper, 64, payload), sixtyFour)
        assertEquals(64, readNativeQuadFarDistance(sixtyFour))
    }

    @Test
    fun `installer writes the chosen distance over the dependency helper`() = withSyntheticBase { root ->
        val helper = nativeQuadResource(NATIVE_QUAD_LIBRARY)
        helperFile(root).writeBytes(helper)
        assertTrue(installNativeQuadFarPlane(root, steamLink, "2.0.20", "5001712", 250))
        val installed = helperFile(root)
        assertEquals(250, readNativeQuadFarDistance(installed.readBytes()))
        assertTrue(isNativeQuadFarPayload(installed.readBytes()))
        val installedTime = installed.lastModified()
        assertTrue(installNativeQuadFarPlane(root, steamLink, "2.0.20", "5001712", 250))
        assertEquals(installedTime, installed.lastModified())
        assertTrue(installNativeQuadFarPlane(root, steamLink, "2.0.20", "5001812", 16))
        assertEquals(16, readNativeQuadFarDistance(helperFile(root).readBytes()))
        assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })
    }

    @Test
    fun `far to off restores the bundled helper through the native quad installer`() = withSyntheticBase { root ->
        val helper = nativeQuadResource(NATIVE_QUAD_LIBRARY)
        assertTrue(
            installNativeQuadResources(
                root, steamLink, "2.0.20", "5001712", helper,
                applyNativeQuad = ::idempotentFakeApply,
            ),
        )
        assertTrue(installNativeQuadFarPlane(root, steamLink, "2.0.20", "5001712", 16))
        assertTrue(isNativeQuadFarPayload(helperFile(root).readBytes()))
        assertTrue(
            installNativeQuadResources(
                root, steamLink, "2.0.20", "5001712", helper,
                applyNativeQuad = ::idempotentFakeApply,
            ),
        )
        assertContentEquals(helper, helperFile(root).readBytes())
        assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })
    }

    @Test
    fun `foreign truncated and wrong-hash helpers fail closed with zero writes`() = withSyntheticBase { root ->
        val helper = nativeQuadResource(NATIVE_QUAD_LIBRARY)
        val payload = nativeQuadFarResource()
        helperFile(root).writeBytes("foreign helper".toByteArray())
        assertFailsWith<PatchException> {
            installNativeQuadFarPlane(root, steamLink, "2.0.20", "5001712", 64)
        }
        assertEquals("foreign helper", helperFile(root).readText())
        assertFailsWith<PatchException> {
            applyNativeQuadFarDistance("foreign helper".toByteArray(), 64, payload)
        }

        helperFile(root).writeBytes(helper)
        val truncated = payload.copyOf(payload.size - 1)
        assertFailsWith<PatchException> {
            installNativeQuadFarPlane(root, steamLink, "2.0.20", "5001712", 64, farPayload = truncated)
        }
        assertContentEquals(helper, helperFile(root).readBytes())

        val wrongHash = payload.copyOf().apply { this[NATIVE_QUAD_FAR_SLOT_OFFSET + 64] =
            (this[NATIVE_QUAD_FAR_SLOT_OFFSET + 64].toInt() xor 1).toByte() }
        assertFailsWith<PatchException> {
            installNativeQuadFarPlane(root, steamLink, "2.0.20", "5001712", 64, farPayload = wrongHash)
        }
        assertContentEquals(helper, helperFile(root).readBytes())

        val badSlot = payload.copyOf().apply {
            this[NATIVE_QUAD_FAR_SLOT_OFFSET] = 0
            this[NATIVE_QUAD_FAR_SLOT_OFFSET + 1] = 0
            this[NATIVE_QUAD_FAR_SLOT_OFFSET + 2] = 0x80.toByte()
            this[NATIVE_QUAD_FAR_SLOT_OFFSET + 3] = 0x3f
        }
        assertFalse(isNativeQuadFarPayload(badSlot))
        assertFailsWith<PatchException> { applyNativeQuadFarDistance(badSlot, 64, payload) }
        assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })
    }

    @Test
    fun `excluded builds packages and distances return unchanged before file access`() = withTemporaryRoot { root ->
        val missing = File(root, "must-stay-absent")
        listOf("2.0.20" to "5001713", "2.0.21" to "5001968", "2.0.22" to "5002244",
            "2.0.23" to "5002363", "2.0.24" to "5002395").forEach { (version, code) ->
            assertFalse(installNativeQuadFarPlane(missing, steamLink, version, code, 64))
        }
        assertFalse(installNativeQuadFarPlane(missing, "cn.vr4p.oculus4xvrplayerov", "2.0.20", "5001712", 64))
        assertFalse(missing.exists())
        listOf(0, 1, 32, 65, 1000, -16).forEach { meters ->
            assertFailsWith<PatchException> {
                installNativeQuadFarPlane(missing, steamLink, "2.0.20", "5001712", meters)
            }
        }
        assertFalse(missing.exists())
    }

    @Test
    fun `move failure leaves the helper untouched`() = withSyntheticBase { root ->
        val helper = nativeQuadResource(NATIVE_QUAD_LIBRARY)
        helperFile(root).writeBytes(helper)
        assertFailsWith<PatchException> {
            installNativeQuadFarPlane(
                root, steamLink, "2.0.20", "5001712", 16,
                moveFile = { _, _ -> throw PatchException("commit failed") },
            )
        }
        assertContentEquals(helper, helperFile(root).readBytes())
        assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })
    }

    private fun sha256(bytes: ByteArray) =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    private fun idempotentFakeApply(bytes: ByteArray, versionName: String, versionCode: String): ByteArray {
        require(bytes.size > 0x1000 + 1) { "synthetic scene too small" }
        return bytes.copyOf().apply { this[0x1000] = 0x42 }
    }

    private fun libDirectory(root: File) = File(root, "lib/arm64-v8a")
    private fun helperFile(root: File) = File(libDirectory(root), NATIVE_QUAD_LIBRARY)
    private fun sceneFile(root: File) = File(libDirectory(root), "libvrlink_scene.so")

    private fun withSyntheticBase(block: (File) -> Unit) = withTemporaryRoot { root ->
        sceneFile(root).apply { parentFile.mkdirs(); writeBytes(ByteArray(SYNTHETIC_SCENE_SIZE)) }
        block(root)
    }

    private fun withTemporaryRoot(block: (File) -> Unit) {
        val root = Files.createTempDirectory("native-quad-far-test-").toFile()
        try { block(root) } finally { root.deleteRecursively() }
    }

    private companion object {
        const val SYNTHETIC_SCENE_SIZE = 65_536
    }
}
