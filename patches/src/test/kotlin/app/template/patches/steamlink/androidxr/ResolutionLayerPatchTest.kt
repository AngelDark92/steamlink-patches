package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import app.template.patches.shared.PatchCategories
import java.io.File
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.junit.Assume.assumeTrue

class ResolutionLayerPatchTest {
    @Test
    fun `resolution layers are exact 5001712 and 5002395 experimental and default off`() {
        listOf(swapchainEnlargePatch, atlasSurfacePatch).forEach { patch ->
            assertFalse(patch.default, patch.name)
            assertEquals(PatchCategories.IMAGE_QUALITY, patch.category, patch.name)
            assertTrue(patch.dependencies.isEmpty(), patch.name)
            assertTrue(patch.options.isEmpty(), patch.name)
            val targets = patch.compatibility.orEmpty().flatMap { compatibility ->
                assertEquals("com.valvesoftware.steamlinkvr", compatibility.packageName)
                assertEquals(EXPERIMENTAL_COMPATIBILITY_NAME, compatibility.name)
                compatibility.targets.flatMap { target ->
                    target.versionCodes.orEmpty().values.map { target.version to it }
                }
            }.toSet()
            assertEquals(setOf("2.0.20" to 5001712, "2.0.24" to 5002395), targets, patch.name)
            assertTrue(patch.description.orEmpty().contains("Builds:"), patch.name)
            assertTrue(patch.description.orEmpty().contains("Needs:"), patch.name)
        }
        assertEquals("Swapchain enlarge (experimental)", swapchainEnlargePatch.name)
        assertEquals("Atlas surface (experimental)", atlasSurfacePatch.name)
        assertTrue(isResolutionLayerBuild("2.0.20", "5001712"))
        assertTrue(isResolutionLayerBuild("2.0.24", "5002395"))
        listOf(
            "2.0.20" to "5001812",
            "2.0.21" to "5001968",
            "2.0.22" to "5002244",
            "2.0.23" to "5002363",
            "2.0.20" to "5001713",
        ).forEach { (version, code) ->
            assertFalse(isResolutionLayerBuild(version, code), "$version/$code")
        }
    }

    @Test
    fun `bundled payloads validate and reject tampering without decoded inputs`() {
        ResolutionLayer.values().forEach { layer ->
            val helper = projectionModeResource(layer.library)
            val manifest = projectionModeResource(layer.manifest)
            validateResolutionLayerPayload(layer, helper, manifest)
            assertContentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46), helper.copyOfRange(0, 4))
            assertTrue(helper.toString(Charsets.ISO_8859_1).contains(layer.buildId))
            assertTrue(manifest.toString(Charsets.UTF_8).contains(
                "\"library_path\": \"${layer.library}\"",
            ))
            assertFailsWith<PatchException> {
                validateResolutionLayerPayload(
                    layer,
                    helper.copyOf().apply { this[lastIndex] = (last().toInt() xor 1).toByte() },
                    manifest,
                )
            }
            assertFailsWith<PatchException> {
                validateResolutionLayerPayload(
                    layer,
                    helper,
                    manifest.copyOf().apply { this[0] = (this[0].toInt() xor 1).toByte() },
                )
            }
        }
    }

    @Test
    fun `excluded exact pairs return unchanged before file access`() {
        val missing = File(Files.createTempDirectory("resolution-layer-excluded").toFile(),
            "must-stay-absent")
        try {
            listOf("2.0.20" to "5001812", "2.0.23" to "5002363", "2.0.20" to "5001713")
                .forEach { (version, code) ->
                    ResolutionLayer.values().forEach { layer ->
                        assertFalse(installResolutionLayerResources(
                            missing, version, code, layer,
                        ))
                    }
                    assertFalse(missing.exists())
                }
        } finally {
            missing.parentFile?.deleteRecursively()
        }
    }

    @Test
    fun `scene pins reject wrong size and wrong hash`() {
        assertFailsWith<PatchException> {
            validateResolutionScene(ByteArray(2_221_071), "2.0.20", "5001712")
        }
        assertFailsWith<PatchException> {
            validateResolutionScene(ByteArray(2_221_072), "2.0.20", "5001712")
        }
        assertFailsWith<PatchException> {
            validateResolutionScene(ByteArray(2_301_431), "2.0.24", "5002395")
        }
        assertFailsWith<PatchException> {
            validateResolutionScene(ByteArray(2_301_432), "2.0.24", "5002395")
        }
    }

    @Test
    fun `decoded scene pins accept the retained exact bases when present`() {
        listOf(
            Triple("2.0.20", "5001712", "android-steamlinkvr-release-base-2.0.20-5001712"),
            Triple("2.0.24", "5002395", "android-steamlinkvr-release-base-2.0.24-5002395"),
        ).forEach { (version, code, directory) ->
            val scene = listOf(
                File("../$directory/lib/arm64-v8a/libvrlink_scene.so"),
                File("$directory/lib/arm64-v8a/libvrlink_scene.so"),
            ).firstOrNull(File::isFile)
            assumeTrue("BLOCKED: retained $version/$code decoded scene is absent", scene != null)
            validateResolutionScene(scene!!.readBytes(), version, code)
        }
    }

    @Test
    fun `install is idempotent and preserves neighbours`() = withTemporaryRoot { root ->
        val lib = File(root, "lib/arm64-v8a").apply { mkdirs() }
        val layers = File(root, "assets/openxr/1/api_layers/implicit.d").apply { mkdirs() }
        val neighbour = File(layers, "XR_APILAYER_local_GalaxyXR_controller_extrapolation.json")
            .apply { writeText("existing neighbour layer manifest") }
        val neighbourBytes = neighbour.readBytes()
        val helper = projectionModeResource(ResolutionLayer.SWAPCHAIN_ENLARGE.library)
        val manifest = projectionModeResource(ResolutionLayer.SWAPCHAIN_ENLARGE.manifest)
        assertTrue(installResolutionLayerFiles(
            lib, layers, ResolutionLayer.SWAPCHAIN_ENLARGE, helper, manifest,
        ))
        val installedHelper = File(lib, ResolutionLayer.SWAPCHAIN_ENLARGE.library)
        val installedManifest = File(layers, ResolutionLayer.SWAPCHAIN_ENLARGE.manifest)
        assertContentEquals(helper, installedHelper.readBytes())
        assertContentEquals(manifest, installedManifest.readBytes())
        val helperTime = installedHelper.lastModified()
        val manifestTime = installedManifest.lastModified()
        assertTrue(installResolutionLayerFiles(
            lib, layers, ResolutionLayer.SWAPCHAIN_ENLARGE, helper, manifest,
        ))
        assertEquals(helperTime, installedHelper.lastModified())
        assertEquals(manifestTime, installedManifest.lastModified())
        assertContentEquals(neighbourBytes, neighbour.readBytes())
        assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })
    }

    @Test
    fun `alpha-0 sibling and retired resources are handled fail closed`() = withTemporaryRoot { root ->
        val lib = File(root, "lib/arm64-v8a").apply { mkdirs() }
        val layers = File(root, "assets/openxr/1/api_layers/implicit.d").apply { mkdirs() }
        val helper = projectionModeResource(ResolutionLayer.ATLAS_SURFACE.library)
        val manifest = projectionModeResource(ResolutionLayer.ATLAS_SURFACE.manifest)

        val alpha0 = File(lib, ANDROID_SURFACE_TRIGGER_LIBRARY).apply { writeText("alpha0") }
        assertFailsWith<PatchException> {
            installResolutionLayerFiles(lib, layers, ResolutionLayer.ATLAS_SURFACE, helper, manifest)
        }
        assertContentEquals("alpha0".toByteArray(), alpha0.readBytes())
        assertFalse(File(lib, ResolutionLayer.ATLAS_SURFACE.library).exists())
        alpha0.delete()

        val sibling = File(lib, ResolutionLayer.SWAPCHAIN_ENLARGE.library)
            .apply { writeText("sibling") }
        assertFailsWith<PatchException> {
            installResolutionLayerFiles(lib, layers, ResolutionLayer.ATLAS_SURFACE, helper, manifest)
        }
        assertContentEquals("sibling".toByteArray(), sibling.readBytes())
        assertFalse(File(lib, ResolutionLayer.ATLAS_SURFACE.library).exists())
        sibling.delete()

        val retiredLibrary = File(lib, "libgxr_surface_video_trigger.so")
            .apply { writeText("retired") }
        val retiredManifest = File(layers,
            "XR_APILAYER_local_GalaxyXR_surface_video_trigger_v1.json")
            .apply { writeText("retired") }
        assertTrue(installResolutionLayerFiles(
            lib, layers, ResolutionLayer.ATLAS_SURFACE, helper, manifest,
        ))
        assertFalse(retiredLibrary.exists())
        assertFalse(retiredManifest.exists())
        assertTrue(File(lib, ResolutionLayer.ATLAS_SURFACE.library).isFile)
        assertTrue(File(layers, ResolutionLayer.ATLAS_SURFACE.manifest).isFile)
    }

    @Test
    fun `stale and tampered resources fail before partial writes`() = withTemporaryRoot { root ->
        val lib = File(root, "lib/arm64-v8a").apply { mkdirs() }
        val layers = File(root, "assets/openxr/1/api_layers/implicit.d").apply { mkdirs() }
        val helper = projectionModeResource(ResolutionLayer.SWAPCHAIN_ENLARGE.library)
        val manifest = projectionModeResource(ResolutionLayer.SWAPCHAIN_ENLARGE.manifest)
        val stale = File(layers, ResolutionLayer.SWAPCHAIN_ENLARGE.manifest)
            .apply { writeText("stale") }
        assertFailsWith<PatchException> {
            installResolutionLayerFiles(
                lib, layers, ResolutionLayer.SWAPCHAIN_ENLARGE, helper, manifest,
            )
        }
        assertFalse(File(lib, ResolutionLayer.SWAPCHAIN_ENLARGE.library).exists())
        assertEquals("stale", stale.readText())
        stale.delete()

        assertFailsWith<PatchException> {
            installResolutionLayerFiles(
                lib, layers, ResolutionLayer.SWAPCHAIN_ENLARGE,
                helper.copyOf().apply { this[lastIndex] = (last().toInt() xor 1).toByte() },
                manifest,
            )
        }
        assertFalse(File(lib, ResolutionLayer.SWAPCHAIN_ENLARGE.library).exists())
        assertFalse(File(layers, ResolutionLayer.SWAPCHAIN_ENLARGE.manifest).exists())
        assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })
    }

    private fun withTemporaryRoot(block: (File) -> Unit) {
        val root = Files.createTempDirectory("resolution-layer-test-").toFile()
        try { block(root) } finally { root.deleteRecursively() }
    }
}
