package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertFailsWith
import kotlin.test.assertNull
import kotlin.test.assertTrue

class NativeSingleProjectionRendererTest {
    @Test
    fun `exact 5002322 probe hook is atomic and idempotent`() {
        val stock = stockSceneFixture()
        val patched = patchNativeSingleProjectionRenderer(
            stock,
            NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY,
        )

        assertContentEquals(paddedAscii("libgxr_nspp.so", 20), patched.sliceArray(0x69635 until 0x69649))
        assertContentEquals(paddedAscii("gxrEndFrame", 21), patched.sliceArray(0x3EB5E until 0x3EB73))
        assertContentEquals(
            byteArrayOf(0x55, 0xFE.toByte(), 0x03, 0x94.toByte()),
            patched.sliceArray(0x11AA7C until 0x11AA80),
        )
        assertContentEquals(
            patched,
            patchNativeSingleProjectionRenderer(patched, NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY),
        )
    }

    @Test
    fun `retired helpers remain target-ineligible and stale layouts fail closed`() {
        val probe = patchNativeSingleProjectionRenderer(
            stockSceneFixture(),
            NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY,
        )
        listOf("libgxr_nsp.so", "libgxr_nspd.so", NATIVE_QUAD_VIEW_LIBRARY, NATIVE_DUAL_QUAD_VIEW_LIBRARY)
            .forEach { retiredLibrary ->
                assertFailsWith<PatchException>(retiredLibrary) {
                    patchNativeSingleProjectionRenderer(stockSceneFixture(), retiredLibrary)
                }
                val stale = probe.copyOf().also {
                    paddedAscii(retiredLibrary, 20).copyInto(it, 0x69635)
                }
                assertFailsWith<PatchException>(retiredLibrary) {
                    patchNativeSingleProjectionRenderer(stale, NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY)
                }
            }
        assertFailsWith<PatchException> {
            patchNativeSingleProjectionRenderer(stockSceneFixture(), "libgxr_unknown.so")
        }
    }

    @Test
    fun `mixed or unknown native hook layouts fail closed`() {
        val mixed = stockSceneFixture().also {
            paddedAscii("libgxr_nsp.so", 20).copyInto(it, 0x69635)
        }
        assertFailsWith<PatchException> {
            patchNativeSingleProjectionRenderer(mixed, NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY)
        }
        assertFailsWith<PatchException> {
            patchNativeSingleProjectionRenderer(ByteArray(128), NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY)
        }
        assertFailsWith<PatchException> {
            patchNativeSingleProjectionRenderer(
                stockSceneFixture().also { it[0x300] = 0 },
                NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY,
            )
        }
    }

    @Test
    fun `every native ELF relationship guard fails closed independently`() {
        listOf(0x227D38, 0x1198, 0x8C880, 0x22A5D8, 0x21A3D0, 0x11AA70, 0x11AA80, 0x14205C)
            .forEach { offset ->
                assertFailsWith<PatchException>("offset=0x${offset.toString(16)}") {
                    patchNativeSingleProjectionRenderer(
                        stockSceneFixture().also { it[offset] =
                            (it[offset].toInt() xor 0x01).toByte() },
                        NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY,
                    )
                }
            }
    }

    @Test
    fun `bundled probe helper exposes resolution decoder and precision evidence`() {
        val helper = checkNotNull(javaClass.getResourceAsStream("/steamlink/androidxr/libgxr_nspp.so"))
            .use { it.readBytes() }
        assertContentEquals(byteArrayOf(0x7F, 0x45, 0x4C, 0x46), helper.copyOfRange(0, 4))
        assertEquals(
            "cf99fe864766318d4ddccd15d166f8d6bb873edcd78a046e475dfc73d2447b93",
            MessageDigest.getInstance("SHA-256").digest(helper)
                .joinToString("") { "%02x".format(it) },
        )
        val nativeStrings = helper.toString(Charsets.ISO_8859_1)
        listOf(
            NATIVE_SINGLE_PROJECTION_PROBE_BUILD_ID,
            NATIVE_SINGLE_PROJECTION_PROBE_MODE,
            "libopenxr_loader.so",
            NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY,
            "view_configuration_eye",
            "resolution_probe_capabilities",
            "resolution_probe_candidate",
            "reconstruction_attachment_precision",
            "probe_source_swapchain_contract",
            "decoder_probe_initialized",
            "decoder_hardware_buffer_descriptor",
            "linear_center_1tap",
            "rgb10a2",
        ).forEach { identity -> assertTrue(nativeStrings.contains(identity), identity) }
        listOf(
            "single_projection_reconstruction_efficient_v1",
            "single_projection_native_renderer_v1",
            "single_projection_native_renderer_dual_v1",
            "libgxr_nsp.so",
            "libgxr_nspd.so",
        ).forEach { retiredIdentity ->
            assertFalse(nativeStrings.contains(retiredIdentity), retiredIdentity)
        }

        val elf = Elf64DynamicView(helper)
        assertTrue("libopenxr_loader.so" in elf.neededLibraries)
        assertTrue("libEGL.so" in elf.neededLibraries)
        assertTrue("libGLESv3.so" in elf.neededLibraries)
        assertTrue("libmediandk.so" in elf.neededLibraries)
        assertTrue("libandroid.so" in elf.neededLibraries)
        assertEquals(NATIVE_SINGLE_PROJECTION_PROBE_LIBRARY, elf.soname)
        listOf(
            "gxrEndFrame",
            "xrCreateInstance",
            "xrDestroyInstance",
            "xrEnumerateViewConfigurationViews",
            "xrCreateSession",
            "xrDestroySession",
            "xrCreateSwapchain",
            "xrDestroySwapchain",
            "xrAcquireSwapchainImage",
            "xrWaitSwapchainImage",
            "xrReleaseSwapchainImage",
            "AMediaCodec_configure",
            "AMediaCodec_dequeueOutputBuffer",
            "AImage_getHardwareBuffer",
        ).forEach { symbol -> assertTrue(symbol in elf.exportedSymbols, symbol) }
        assertNull(javaClass.getResourceAsStream("/steamlink/androidxr/libgxr_nqv.so"))
        assertNull(javaClass.getResourceAsStream("/steamlink/androidxr/libgxr_nqvd.so"))
    }

    @Test
    fun `native source contracts retain cpu fast paths and isolate diagnostic work`() {
        fun source(name: String): String = listOf(
            File("extensions/resolution-trace-layer/src/$name"),
            File("../extensions/resolution-trace-layer/src/$name"),
        ).firstOrNull(File::isFile)?.readText() ?: error("Missing native source: $name")

        val reconstruction = source("single_projection_reconstruction_efficient_layer.cpp")
        listOf(
            "constexpr uint64_t kSuccessSummaryInterval = 900",
            "inspectNativeFast",
            "NativeSourceBinding",
            "nativeCallsiteCacheMissCount",
            "nativeDispatchReady.load(std::memory_order_acquire)",
            "nativeStreamCallsite.load(std::memory_order_relaxed)",
            "GXR_NATIVE_DUAL_FORMAT",
            "GL_RGB10_A2",
            "mixed_source_format",
            "ci.format=s.sourceFormat",
            "glTexStorage2D(GL_TEXTURE_2D,1,static_cast<GLenum>(s.sourceFormat)",
            "uniform highp sampler2D underTex",
            "xrEnumerateSwapchainFormats",
            "GXR_DIAGNOSTIC_PROBE",
            "view_configuration_eye",
            "resolution_probe_capabilities",
            "resolution_probe_candidate",
            "reconstruction_attachment_precision",
            "probe_source_swapchain_contract",
            "probe_submitted_frame",
        ).forEach { invariant -> assertTrue(reconstruction.contains(invariant), invariant) }

        val decoderProbe = source("native_probe_media_interpose.cpp")
        assertTrue(decoderProbe.contains(NATIVE_SINGLE_PROJECTION_PROBE_BUILD_ID))
        assertFalse(decoderProbe.contains("single-projection-native-probe-v1.0-20260831"))
        listOf(
            "dlopen(library, RTLD_NOW | RTLD_LOCAL)",
            "AMediaCodec_configure",
            "AMediaCodec_dequeueOutputBuffer",
            "AMediaCodec_getOutputFormat",
            "AImage_getHardwareBuffer",
            "AHardwareBuffer_describe",
            "AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM",
            "AHARDWAREBUFFER_FORMAT_YCbCr_P010",
            "decoder_probe_initialized",
            "decoder_output_format_changed",
            "decoder_hardware_buffer_descriptor",
            "precisionHint",
        ).forEach { invariant -> assertTrue(decoderProbe.contains(invariant), invariant) }
    }

    private fun stockSceneFixture(): ByteArray = ByteArray(2_283_400).also { fixture ->
        "585d88d646a8c6efe94bdd9fc6c9dbbc68fc13ba".chunked(2)
            .map { it.toInt(16).toByte() }
            .toByteArray()
            .copyInto(fixture, 0x300)
        "libopenxr_loader.so\u0000".encodeToByteArray().copyInto(fixture, 0x69635)
        "xrRequestExitSession\u0000".encodeToByteArray().copyInto(fixture, 0x3EB5E)
        byteArrayOf(0xF1.toByte(), 0xF9.toByte(), 0x03, 0x94.toByte()).copyInto(fixture, 0x11AA7C)
        "01000000000000007d11040000000000".hexBytes().copyInto(fixture, 0x227D38)
        "a66601001200000000000000000000000000000000000000".hexBytes().copyInto(fixture, 0x1198)
        "d8e5220000000000020400009c0000000000000000000000".hexBytes().copyInto(fixture, 0x8C880)
        "8071210000000000".hexBytes().copyInto(fixture, 0x22A5D8)
        "b000009011ee42f91062179120021fd6".hexBytes().copyInto(fixture, 0x21A3D0)
        "e82702a9ec2b0629e1630091".hexBytes().copyInto(fixture, 0x11AA70)
        "e503002a0003f83733008052e02740f9".hexBytes().copyInto(fixture, 0x11AA80)
        "000440f9dc6003946001f83720008052".hexBytes().copyInto(fixture, 0x14205C)
    }

    private fun paddedAscii(value: String, size: Int): ByteArray =
        ByteArray(size).also { value.encodeToByteArray().copyInto(it) }

    private fun String.hexBytes(): ByteArray =
        chunked(2).map { it.toInt(16).toByte() }.toByteArray()

}

private class Elf64DynamicView(private val bytes: ByteArray) {
    private val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    private data class Section(val name: String, val offset: Int, val size: Int, val entrySize: Int)

    private val sections: Map<String, Section> = run {
        require(bytes.copyOfRange(0, 4).contentEquals(byteArrayOf(0x7F, 0x45, 0x4C, 0x46)))
        require(bytes[4].toInt() == 2 && bytes[5].toInt() == 1)
        val tableOffset = buffer.getLong(0x28).toInt()
        val entrySize = buffer.getShort(0x3A).toInt() and 0xFFFF
        val count = buffer.getShort(0x3C).toInt() and 0xFFFF
        val stringTableIndex = buffer.getShort(0x3E).toInt() and 0xFFFF
        val stringHeader = tableOffset + stringTableIndex * entrySize
        val stringsOffset = buffer.getLong(stringHeader + 0x18).toInt()
        (0 until count).associate { index ->
            val header = tableOffset + index * entrySize
            val name = cString(stringsOffset + buffer.getInt(header))
            name to Section(
                name,
                buffer.getLong(header + 0x18).toInt(),
                buffer.getLong(header + 0x20).toInt(),
                buffer.getLong(header + 0x38).toInt(),
            )
        }
    }

    private val dynstr = sections.getValue(".dynstr")
    val neededLibraries = mutableSetOf<String>()
    var soname: String? = null
        private set
    val exportedSymbols = mutableSetOf<String>()

    init {
        val dynamic = sections.getValue(".dynamic")
        for (offset in dynamic.offset until dynamic.offset + dynamic.size step 16) {
            val tag = buffer.getLong(offset)
            val value = buffer.getLong(offset + 8).toInt()
            if (tag == 1L) neededLibraries += cString(dynstr.offset + value)
            if (tag == 14L) soname = cString(dynstr.offset + value)
        }
        val dynsym = sections.getValue(".dynsym")
        for (offset in dynsym.offset until dynsym.offset + dynsym.size step dynsym.entrySize) {
            val nameOffset = buffer.getInt(offset)
            val info = bytes[offset + 4].toInt() and 0xFF
            val sectionIndex = buffer.getShort(offset + 6).toInt() and 0xFFFF
            if ((info ushr 4) != 0 && sectionIndex != 0 && nameOffset != 0) {
                exportedSymbols += cString(dynstr.offset + nameOffset)
            }
        }
    }

    private fun cString(offset: Int): String {
        var end = offset
        while (end < bytes.size && bytes[end] != 0.toByte()) end++
        return bytes.copyOfRange(offset, end).toString(Charsets.UTF_8)
    }
}
