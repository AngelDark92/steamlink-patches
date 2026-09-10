package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import java.io.File
import java.nio.file.Files
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.test.*

class SurfaceVideoPatchTest {
    @Test fun `only both exact bases are admitted and patch remains opt in`() {
        assertNotNull(surfaceVideoBase("2.0.20", "5001712"))
        assertNotNull(surfaceVideoBase("2.0.22", "5002322"))
        for ((v,c) in listOf("2.0.22" to "5001712", "2.0.20" to "5002322",
            "2.0.22" to "5002318", "2.0.20" to "5001740")) {
            assertNull(surfaceVideoBase(v,c))
            assertFalse(installSurfaceVideo(File("MUST-NOT-BE-READ"),v,c,"invalid"))
        }
        assertFalse(xrAndroidSurfaceVideoPatch.default)
        val targets = xrAndroidSurfaceVideoPatch.compatibility!!.flatMap { it.targets }
        assertEquals(setOf("2.0.20" to 5001712, "2.0.22" to 5002322),
            targets.flatMap { t -> t.versionCodes!!.values.map { t.version to it } }.toSet())
    }
    @Test fun `both precision resources are distinct arm64 helpers for both bases`() {
        val hashes = mutableSetOf<Int>()
        for (base in surfaceVideoBases) for (precision in listOf("srgb8","fp16-linear")) {
            val bytes = projectionModeResource(surfaceVideoResource(base,precision))
            assertContentEquals(byteArrayOf(127,69,76,70,2),bytes.copyOfRange(0,5))
            assertEquals(0xb7.toByte(),bytes[18])
            assertTrue(bytes.toString(Charsets.ISO_8859_1).contains("GXRSurfaceVideo"))
            hashes += bytes.contentHashCode()
        }
        assertEquals(4,hashes.size)
        assertFailsWith<PatchException> { surfaceVideoResource(surfaceVideoBases.first(),"pq") }
        assertFailsWith<PatchException> { validateSurfaceVideoScene(surfaceVideoBases.first(),ByteArray(8)) }
    }
    @Test fun `mutually exclusive modes fail before overwriting sibling resources in either order`() {
        val root = Files.createTempDirectory("surface-conflict").toFile()
        try {
            val lib = File(root,"lib").apply { mkdirs() }
            val layers = File(root,"layers").apply { mkdirs() }
            val video = ProjectionModeResources(SURFACE_VIDEO_MODE,SURFACE_VIDEO_LIBRARY,SURFACE_VIDEO_MANIFEST)
            val trigger = ProjectionModeResources(ANDROID_SURFACE_TRIGGER_MODE,ANDROID_SURFACE_TRIGGER_LIBRARY,ANDROID_SURFACE_TRIGGER_MANIFEST)
            for ((existing,requested) in listOf(video to trigger,trigger to video)) {
                val marker=File(lib,existing.library).apply { writeText("unchanged") }
                assertFailsWith<PatchException> { installProjectionModeResources(lib,layers,requested,byteArrayOf(1)) }
                assertEquals("unchanged",marker.readText())
                assertFalse(File(lib,requested.library).exists())
                marker.delete()
            }
        } finally { root.deleteRecursively() }
    }
    @Test fun `manifest conflicts preserve permissions before failure and own mode is idempotent`() {
        val factory=DocumentBuilderFactory.newInstance()
        val xml="""<manifest xmlns:android="http://schemas.android.com/apk/res/android"><uses-permission android:name="android.permission.SYSTEM_ALERT_WINDOW"/><application><meta-data android:name="com.valvesoftware.steamlink.GXR_RESOLUTION_MODE" android:value="$ANDROID_SURFACE_TRIGGER_MODE"/></application></manifest>"""
        val doc=factory.newDocumentBuilder().parse(xml.byteInputStream())
        assertFailsWith<PatchException> { configurePermissionFreeProjectionMode(doc,SURFACE_VIDEO_MODE) }
        assertEquals(1,doc.getElementsByTagName("uses-permission").length)
        configurePermissionFreeProjectionMode(doc,ANDROID_SURFACE_TRIGGER_MODE)
        configurePermissionFreeProjectionMode(doc,ANDROID_SURFACE_TRIGGER_MODE)
        assertEquals(0,doc.getElementsByTagName("uses-permission").length)
        assertEquals(1,doc.getElementsByTagName("meta-data").length)
        assertTrue(projectionModesConflict(SURFACE_VIDEO_MODE,ANDROID_SURFACE_TRIGGER_MODE))
        assertTrue(projectionModesConflict(ANDROID_SURFACE_TRIGGER_MODE,SURFACE_VIDEO_MODE))
    }
}
