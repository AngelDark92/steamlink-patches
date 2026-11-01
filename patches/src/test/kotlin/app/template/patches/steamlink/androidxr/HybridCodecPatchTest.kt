package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import app.template.patches.shared.PatchCategories
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.junit.Assume.assumeTrue

class HybridCodecPatchTest {
    @Test
    fun `hybrid helper is experimental default off and depends on codecs support`() {
        assertFalse(hybridCodecPatch.default)
        assertEquals("Hybrid codec helper (experimental)", hybridCodecPatch.name)
        assertEquals(PatchCategories.STREAMING_AND_NETWORK, hybridCodecPatch.category)
        assertTrue(hybridCodecPatch.options.isEmpty())
        assertEquals(setOf(xrCodecsSupportPatch), hybridCodecPatch.dependencies)

        val description = hybridCodecPatch.description.orEmpty()
        listOf(
            "experimental", "ASTC/PyroWave", "decode-only", "compositing",
            "Codecs Support", "GXR_DISABLE_HYBRID_CODEC",
        ).forEach { phrase ->
            assertTrue(description.contains(phrase, ignoreCase = true), phrase)
        }
        assertTrue(description.contains("Builds:"))

        val targets = hybridCodecPatch.compatibility.orEmpty().flatMap { compatibility ->
            assertEquals("com.valvesoftware.steamlinkvr", compatibility.packageName)
            assertEquals(EXPERIMENTAL_COMPATIBILITY_NAME, compatibility.name)
            compatibility.targets.flatMap { target ->
                target.versionCodes.orEmpty().values.map { target.version to it }
            }
        }.toSet()
        assertEquals(
            setOf(
                "2.0.20" to 5001712, "2.0.20" to 5001812,
                "2.0.21" to 5001968, "2.0.22" to 5002244,
                "2.0.23" to 5002363, "2.0.24" to 5002395,
            ),
            targets,
        )

        // Selecting the helper without the layer must not silently install a
        // floating library: the dependency is the declared applicability edge.
        val layerTargets = xrCodecsSupportPatch.compatibility.orEmpty().flatMap { compatibility ->
            compatibility.targets.flatMap { target ->
                target.versionCodes.orEmpty().values.map { target.version to it }
            }
        }.toSet()
        assertEquals(layerTargets, targets)
    }

    @Test
    fun `bundled helper matches its size and SHA-256 pin and rejects tampering`() {
        val helper = projectionModeResource(HYBRID_CODEC_LIBRARY)
        assertEquals(494_432, helper.size)
        assertEquals(HYBRID_CODEC_LIBRARY_SIZE, helper.size)
        assertEquals(
            "995da8563c4216095534ad8bc7866990e1137987d1f9158438d723772bec12fb",
            HYBRID_CODEC_LIBRARY_SHA256,
        )
        assertEquals(HYBRID_CODEC_LIBRARY_SHA256, sha256(helper))
        assertContentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46), helper.copyOfRange(0, 4))
        validateHybridCodecPayload(helper)

        val tampered = helper.copyOf().apply { this[lastIndex] = (last().toInt() xor 1).toByte() }
        assertFailsWith<PatchException> { validateHybridCodecPayload(tampered) }
        assertFailsWith<PatchException> { validateHybridCodecPayload(helper.copyOf(helper.size - 1)) }
        assertFailsWith<PatchException> { validateHybridCodecPayload(ByteArray(helper.size)) }
    }

    @Test
    fun `scene guards are present for every codecs support base`() {
        assertEquals(
            mapOf(
                "2.0.20/5001712" to listOf(
                    Triple(0xfd304, 0x80, "6e5dcb4f5094763e948388ca35c6132af242159fc3c22e812f0fe2f4ab4f2649"),
                    Triple(0xfdca8, 0x80, "86ceb824e4a972fbac71d7cb6b88a99d5decde4b42fbd1b80cb5d84361a94b67"),
                    Triple(0xfd2f8, 0x40, "25174852802b65e709401364232a3dd9490617cfe9983f8bfc948a27a90e11f4"),
                ),
                "2.0.20/5001812" to listOf(
                    Triple(0xfd1c0, 0x80, "b1bfbf75b91c7f522411c68d30283c414517ac0ef4f948cb73fbe6cde55385ed"),
                    Triple(0xfdb64, 0x80, "2d79c63e6001f5b6fd708027a3241c7004612b55c5158ee773870825c3b875e1"),
                    Triple(0xfd1b4, 0x40, "8be10423034e42a7d6c398c853c0d91085420210c0178247dd3ab1960d1e51ee"),
                ),
                "2.0.21/5001968" to listOf(
                    Triple(0xf9168, 0x80, "0206212ba391e8486f3d67c41a7373103c02fd41c3ce19432645ad411fa3b52b"),
                    Triple(0xf9b0c, 0x80, "faeea4728e0047b25bae83bd24a14b51bd4972c21530180d73332c82b26e1db3"),
                    Triple(0xf915c, 0x40, "2f9ccf5df080c89ddee7bdc010c7eeda87f536fac5d164729acbdb82b8f74ad0"),
                ),
                "2.0.22/5002244" to listOf(
                    Triple(0xfa4a8, 0x80, "f2efc6f172f7dd0724a79a4fd921bca68fc180bff33182c6f51ce55a4b1c0643"),
                    Triple(0xfae4c, 0x80, "3342b1a89b49d2c038ec58e0f7f76e4871f73efcb7cb0cfbebd87212e1de7e2c"),
                    Triple(0xfa49c, 0x40, "d7c2ad75cd4e123d937828007c802055a17675cdc9445af8b90b9dc84c7258ac"),
                ),
                "2.0.23/5002363" to listOf(
                    Triple(0xfd6fc, 0x80, "f5c43c6aacb69e61c3f929f361612e1b91bb509803d2c14824b9601b3dd8c795"),
                    Triple(0xfe0a0, 0x80, "e08a664f0fd116e2735f68ff8d10a0fcbc4eac8e216fd986beec191a078bddb4"),
                    Triple(0xfd6f0, 0x40, "3a38bb855e0af4c544b8568414a9e022803a8c47e5a128b20126c84fb40c7eb3"),
                ),
                "2.0.24/5002395" to listOf(
                    Triple(0xfe954, 0x80, "ef7f310bbd65499404aa89a18f873c42a87282a16799b84119c75377654003a3"),
                    Triple(0xff2f8, 0x80, "49570c2a54e39d80d2f8bb61c190b9d8b821675a79a4d4c4bf5f94bfb7acde3a"),
                    Triple(0xfe948, 0x40, "51b57ce6a5c20fab3266923460c046f1247823cb95526252c61a92020f052464"),
                ),
            ),
            hybridCodecScenePreconditions,
        )

        hybridCodecScenePreconditions.keys.forEach { key ->
            val (version, code) = key.split('/')
            assertTrue(isHybridCodecBuild(version, code), key)
            assertFailsWith<PatchException> {
                validateHybridCodecScene(ByteArray(0x1000), version, code)
            }
            assertFailsWith<PatchException> {
                validateHybridCodecScene(ByteArray(0x200000), version, code)
            }
        }
        listOf(
            "2.0.20" to "5001713", "2.0.20" to "5002363",
            "2.0.23" to "5002322", "2.0.23" to "5002364",
            "2.0.24" to "5002363", "2.0.24" to "5002394",
        ).forEach { (version, code) ->
            assertFalse(isHybridCodecBuild(version, code), "$version/$code")
            assertFailsWith<PatchException> {
                validateHybridCodecScene(ByteArray(0), version, code)
            }
        }
    }

    @Test
    fun `excluded builds return unchanged before file access`() {
        val missing = File(Files.createTempDirectory("hybrid-codec-excluded").toFile(),
            "must-stay-absent")
        try {
            listOf("2.0.20" to "5001713", "2.0.23" to "5002322")
                .forEach { (version, code) ->
                    assertFalse(installHybridCodecResources(missing, version, code))
                    assertFalse(missing.exists())
                }
        } finally {
            missing.parentFile?.deleteRecursively()
        }
    }

    @Test
    fun `install is idempotent and fails closed on stale or tampered payloads`() =
        withTemporaryRoot { root ->
            val helper = projectionModeResource(HYBRID_CODEC_LIBRARY)
            val neighbour = File(root, "libgxr_codecs_support.so").apply { writeText("layer") }
            val neighbourBytes = neighbour.readBytes()

            assertTrue(installHybridCodecFiles(root, helper))
            val installed = File(root, HYBRID_CODEC_LIBRARY)
            assertContentEquals(helper, installed.readBytes())
            val installedTime = installed.lastModified()
            assertTrue(installHybridCodecFiles(root, helper))
            assertEquals(installedTime, installed.lastModified())
            assertContentEquals(neighbourBytes, neighbour.readBytes())
            assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })

            val tampered = helper.copyOf().apply {
                this[lastIndex] = (last().toInt() xor 1).toByte()
            }
            assertFailsWith<PatchException> { installHybridCodecFiles(root, tampered) }
            assertContentEquals(helper, installed.readBytes())

            installed.writeText("stale")
            assertFailsWith<PatchException> {
                installHybridCodecFiles(root, helper)
            }
            assertEquals("stale", installed.readText())
        }

    @Test
    fun `install needs the codecs support layer beside the exact scene`() {
        val scene = originalScene("2.0.20", "5001712")
        withTemporaryRoot { root ->
            val sceneFile = File(root, "lib/arm64-v8a/libvrlink_scene.so")
            sceneFile.parentFile.mkdirs()
            sceneFile.writeBytes(scene)

            assertFailsWith<PatchException> {
                installHybridCodecResources(root, "2.0.20", "5001712")
            }
            assertFalse(File(sceneFile.parentFile, HYBRID_CODEC_LIBRARY).exists())

            File(sceneFile.parentFile, CODECS_SUPPORT_LIBRARY)
                .writeBytes(projectionModeResource(CODECS_SUPPORT_LIBRARY))
            assertTrue(installHybridCodecResources(root, "2.0.20", "5001712"))
            assertTrue(File(sceneFile.parentFile, HYBRID_CODEC_LIBRARY).isFile)
        }
    }

    private fun originalScene(version: String, code: String): ByteArray {
        val relative =
            "android-steamlinkvr-release-base-$version-$code/lib/arm64-v8a/libvrlink_scene.so"
        val scene = listOf(File(relative), File("../$relative")).firstOrNull(File::isFile)
        // Fresh checkouts do not contain the ignored proprietary decoded APK.
        assumeTrue(
            "Hybrid helper install audit BLOCKED: missing exact $version/$code scene",
            scene != null,
        )
        return requireNotNull(scene).readBytes()
    }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes).joinToString("") { "%02x".format(it) }

    private fun withTemporaryRoot(block: (File) -> Unit) {
        val root = Files.createTempDirectory("hybrid-codec-test-").toFile()
        try {
            block(root)
        } finally {
            root.deleteRecursively()
        }
    }
}
