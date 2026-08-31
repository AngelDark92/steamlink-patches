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
import kotlin.test.assertTrue

class NativeSingleProjectionRendererTest {
    @Test
    fun `exact 5002322 native hook is atomic and idempotent`() {
        val stock = stockSceneFixture()
        val patched = patchNativeSingleProjectionRenderer(stock)

        assertContentEquals(paddedAscii("libgxr_nsp.so", 20), patched.sliceArray(0x69635 until 0x69649))
        assertContentEquals(paddedAscii("gxrEndFrame", 21), patched.sliceArray(0x3EB5E until 0x3EB73))
        assertContentEquals(
            byteArrayOf(0x55, 0xFE.toByte(), 0x03, 0x94.toByte()),
            patched.sliceArray(0x11AA7C until 0x11AA80),
        )
        assertContentEquals(patched, patchNativeSingleProjectionRenderer(patched))
    }

    @Test
    fun `exact 5002322 quad hook is atomic idempotent and mutually exclusive`() {
        val stock = stockSceneFixture()
        val quad = patchNativeSingleProjectionRenderer(stock, NATIVE_QUAD_VIEW_LIBRARY)

        assertContentEquals(paddedAscii("libgxr_nqv.so", 20), quad.sliceArray(0x69635 until 0x69649))
        assertContentEquals(quad, patchNativeSingleProjectionRenderer(quad, NATIVE_QUAD_VIEW_LIBRARY))
        assertFailsWith<PatchException> { patchNativeSingleProjectionRenderer(quad) }

        val reconstruction = patchNativeSingleProjectionRenderer(stock)
        assertFailsWith<PatchException> {
            patchNativeSingleProjectionRenderer(reconstruction, NATIVE_QUAD_VIEW_LIBRARY)
        }
        assertFailsWith<PatchException> {
            patchNativeSingleProjectionRenderer(stock, "libgxr_unknown.so")
        }
    }

    @Test
    fun `all four native helpers are atomic idempotent and mutually exclusive`() {
        val libraries = listOf(
            NATIVE_SINGLE_PROJECTION_LIBRARY,
            NATIVE_QUAD_VIEW_LIBRARY,
            NATIVE_DUAL_SINGLE_PROJECTION_LIBRARY,
            NATIVE_DUAL_QUAD_VIEW_LIBRARY,
        )
        libraries.forEach { library ->
            val patched = patchNativeSingleProjectionRenderer(stockSceneFixture(), library)
            assertContentEquals(paddedAscii(library, 20), patched.sliceArray(0x69635 until 0x69649))
            assertContentEquals(patched, patchNativeSingleProjectionRenderer(patched, library))
            libraries.filterNot { it == library }.forEach { sibling ->
                assertFailsWith<PatchException>("$library versus $sibling") {
                    patchNativeSingleProjectionRenderer(patched, sibling)
                }
            }
        }
    }

    @Test
    fun `mixed or unknown native hook layouts fail closed`() {
        val mixed = stockSceneFixture().also {
            paddedAscii("libgxr_nsp.so", 20).copyInto(it, 0x69635)
        }
        assertFailsWith<PatchException> { patchNativeSingleProjectionRenderer(mixed) }
        assertFailsWith<PatchException> { patchNativeSingleProjectionRenderer(ByteArray(128)) }
        assertFailsWith<PatchException> {
            patchNativeSingleProjectionRenderer(stockSceneFixture().also { it[0x300] = 0 })
        }
    }

    @Test
    fun `every native ELF relationship guard fails closed independently`() {
        listOf(0x227D38, 0x1198, 0x8C880, 0x22A5D8, 0x21A3D0, 0x11AA70, 0x11AA80, 0x14205C)
            .forEach { offset ->
                assertFailsWith<PatchException>("offset=0x${offset.toString(16)}") {
                    patchNativeSingleProjectionRenderer(stockSceneFixture().also { it[offset] =
                        (it[offset].toInt() xor 0x01).toByte() })
                }
            }
    }

    @Test
    fun `bundled native helper has guarded identity and real loader dependency`() {
        val helper = checkNotNull(javaClass.getResourceAsStream("/steamlink/androidxr/libgxr_nsp.so"))
            .use { it.readBytes() }
        assertContentEquals(byteArrayOf(0x7F, 0x45, 0x4C, 0x46), helper.copyOfRange(0, 4))
        assertEquals(
            "dbdf7e1a25dd088853abf7bf72d3d03afec2513925194bfeb586720ceaa530f7",
            MessageDigest.getInstance("SHA-256").digest(helper).joinToString("") { "%02x".format(it) },
        )
        val nativeStrings = helper.toString(Charsets.ISO_8859_1)
        listOf(
            "single-projection-native-renderer-v1.2-20260831",
            "single_projection_native_renderer_v1",
            "libopenxr_loader.so",
            "libgxr_nsp.so",
            "gxrEndFrame",
            "xrCreateInstance",
            "xrCreateSwapchain",
            "xrAcquireSwapchainImage",
            "xrReleaseSwapchainImage",
            "linear_center_1tap",
            "fastFingerprintCount",
        ).forEach { identity -> assertTrue(nativeStrings.contains(identity), identity) }

        val elf = Elf64DynamicView(helper)
        assertTrue("libopenxr_loader.so" in elf.neededLibraries)
        assertEquals("libgxr_nsp.so", elf.soname)
        listOf(
            "gxrEndFrame",
            "xrCreateInstance",
            "xrDestroyInstance",
            "xrEnumerateViewConfigurationViews",
            "xrCreateSession",
            "xrDestroySession",
            "xrCreateSwapchain",
            "xrDestroySwapchain",
            "xrEnumerateSwapchainImages",
            "xrAcquireSwapchainImage",
            "xrWaitSwapchainImage",
            "xrReleaseSwapchainImage",
        ).forEach { symbol -> assertTrue(symbol in elf.exportedSymbols, symbol) }
    }

    @Test
    fun `bundled quad helper is zero-copy and exports guarded virtualization`() {
        val helper = checkNotNull(javaClass.getResourceAsStream("/steamlink/androidxr/libgxr_nqv.so"))
            .use { it.readBytes() }
        assertContentEquals(byteArrayOf(0x7F, 0x45, 0x4C, 0x46), helper.copyOfRange(0, 4))
        assertEquals(
            "d7cc22af0127ddd4a5a176d741afee193a598a64ebd509c858a273dc9869c8fd",
            MessageDigest.getInstance("SHA-256").digest(helper)
                .joinToString("") { "%02x".format(it) },
        )
        val nativeStrings = helper.toString(Charsets.ISO_8859_1)
        listOf(
            "single-projection-native-quad-zero-copy-v1.0-20260831",
            "single_projection_native_quad_zero_copy_v1",
            "libopenxr_loader.so",
            "libgxr_nqv.so",
            "native_quad_capability",
            "native_quad_summary",
            "reconstructionPasses\":0",
            "fastFingerprintCount",
        ).forEach { identity -> assertTrue(nativeStrings.contains(identity), identity) }

        val elf = Elf64DynamicView(helper)
        assertTrue("libopenxr_loader.so" in elf.neededLibraries)
        assertFalse("libEGL.so" in elf.neededLibraries)
        assertFalse("libGLESv3.so" in elf.neededLibraries)
        assertEquals("libgxr_nqv.so", elf.soname)
        listOf(
            "gxrEndFrame",
            "xrCreateInstance",
            "xrGetInstanceProcAddr",
            "xrDestroyInstance",
            "xrGetSystem",
            "xrEnumerateViewConfigurations",
            "xrGetViewConfigurationProperties",
            "xrEnumerateViewConfigurationViews",
            "xrCreateSession",
            "xrDestroySession",
            "xrBeginSession",
            "xrEndSession",
            "xrLocateViews",
            "xrCreateSwapchain",
            "xrDestroySwapchain",
            "xrAcquireSwapchainImage",
            "xrWaitSwapchainImage",
            "xrReleaseSwapchainImage",
            "xrRequestExitSession",
        ).forEach { symbol -> assertTrue(symbol in elf.exportedSymbols, symbol) }
        assertFalse("xrEndFrame" in elf.exportedSymbols)
    }

    @Test
    fun `dual-format helpers have isolated identities and expected GPU dependencies`() {
        val cpu = bundledHelper("libgxr_nspd.so")
        assertEquals(
            "851e80b27b884044235660c2be3fef1483d291ce02dd1191e826ea014bef772e",
            sha256(cpu),
        )
        val cpuStrings = cpu.toString(Charsets.ISO_8859_1)
        listOf(
            "single-projection-native-renderer-dual-v1.0-20260831",
            "single_projection_native_renderer_dual_v1",
            "libgxr_nspd.so",
            "rgb10a2",
            "mixed_source_format",
            "source_format_not_supported_for_output",
        ).forEach { assertTrue(cpuStrings.contains(it), it) }
        val cpuElf = Elf64DynamicView(cpu)
        assertEquals("libgxr_nspd.so", cpuElf.soname)
        assertTrue("libEGL.so" in cpuElf.neededLibraries)
        assertTrue("libGLESv3.so" in cpuElf.neededLibraries)
        assertTrue("libopenxr_loader.so" in cpuElf.neededLibraries)

        val gpu = bundledHelper("libgxr_nqvd.so")
        assertEquals(
            "0c81af731de716594c3b8fc3888018e7fc1a9aed7723298c761d585c45efc7e2",
            sha256(gpu),
        )
        val gpuStrings = gpu.toString(Charsets.ISO_8859_1)
        listOf(
            "single-projection-native-quad-zero-copy-dual-v1.0-20260831",
            "single_projection_native_quad_zero_copy_dual_v1",
            "libgxr_nqvd.so",
            "rgb10a2",
            "mixed_source_format",
            "reconstructionPasses\":0",
        ).forEach { assertTrue(gpuStrings.contains(it), it) }
        val gpuElf = Elf64DynamicView(gpu)
        assertEquals("libgxr_nqvd.so", gpuElf.soname)
        assertTrue("libopenxr_loader.so" in gpuElf.neededLibraries)
        assertFalse("libEGL.so" in gpuElf.neededLibraries)
        assertFalse("libGLESv3.so" in gpuElf.neededLibraries)
    }

    @Test
    fun `native source contracts retain cpu fast paths and legal quad fallback`() {
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
        ).forEach { invariant -> assertTrue(reconstruction.contains(invariant), invariant) }

        val quad = source("single_projection_native_quad_zero_copy.cpp")
        listOf(
            "XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO_WITH_FOVEATED_INSET",
            "viewCount != state.views.size()",
            "view.maxImageRectWidth < kSourceExtent",
            "view.maxSwapchainSampleCount < 2",
            "patched.primaryViewConfigurationType = kQuadViewConfiguration",
            "committed = found != sessions.end() && found->second.quadCommitted",
            "if (capacity == 0) return XR_SUCCESS",
            "views[0] = under->views[0]",
            "views[1] = under->views[1]",
            "views[2] = fovea->views[0]",
            "views[3] = fovea->views[1]",
            "projection.viewCount = static_cast<uint32_t>(views.size())",
            "output.layerCount = 1",
            "fingerprintReady",
            "binding.state->generation == binding.generation",
            "markUnsafeAndExit",
            "forceNextSessionStock",
            "\\\"reconstructionPasses\\\":0",
            "kRgb10A2Format = 0x8059",
            "mixed_source_format",
            "sourcePrecision",
        ).forEach { invariant -> assertTrue(quad.contains(invariant), invariant) }
        assertFalse(quad.contains("#include <EGL"))
        assertFalse(quad.contains("#include <GLES"))
        assertFalse(Regex("\\bgl[A-Z][A-Za-z0-9_]*\\(").containsMatchIn(quad))
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

    private fun bundledHelper(name: String): ByteArray =
        checkNotNull(javaClass.getResourceAsStream("/steamlink/androidxr/$name")).use { it.readBytes() }

    private fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes)
        .joinToString("") { "%02x".format(it) }
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
