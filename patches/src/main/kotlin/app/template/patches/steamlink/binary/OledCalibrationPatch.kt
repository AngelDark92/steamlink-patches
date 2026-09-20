package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.booleanOption
import app.morphe.patcher.patch.floatSliderOption
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK
import java.security.MessageDigest
import java.util.Locale

private val SHADER_EXTENSION =
    "#extension GL_OES_EGL_image_external_essl3 : enable".toByteArray(Charsets.US_ASCII)
private val SHADER_VERSION = "#version 300 es\n".toByteArray(Charsets.US_ASCII)
internal const val VIDEO_SHADER_SIZE = 1087
internal const val VIDEO_LIBRARY_SIZE_5001712 = 2_221_072
internal const val VIDEO_LIBRARY_SIZE_5001740 = 2_220_528
internal const val VIDEO_LIBRARY_SIZE_5002244 = 2_251_920
internal const val VIDEO_LIBRARY_SIZE_5002313 = 2_276_872
internal const val VIDEO_LIBRARY_SIZE_5002318 = 2_277_488
internal const val VIDEO_LIBRARY_SIZE_5002322 = 2_283_400
internal const val VIDEO_LIBRARY_SIZE_5002363 = 2_292_008
private const val VIDEO_LIBRARY_SHA256_5001712 =
    "80b62797c7e26d6b67b0cca00693b076a336bdb48ebc1383a16cccb1616ed495"
private const val VIDEO_LIBRARY_SHA256_5001740 =
    "5fbb76c06c9fc0e3e5c5825752aa17e040462c8551b69d3492265f620244f443"
private const val VIDEO_LIBRARY_SHA256_5002244 =
    "4b2fa5e1b5d9d5c938873f692b0e5e18159e1199dee1253dd6eccc8fa43dfa12"
private const val VIDEO_LIBRARY_SHA256_5002313 =
    "e4d3575a130dc013e4c8fe4fb965217028229f89b13ba821c01b492e457398bb"
private const val VIDEO_LIBRARY_SHA256_5002318 =
    "3c8d1ce13fd61edff5ce65efe6eedcc8565c89b66bab371550986a5c75407e56"
private const val VIDEO_LIBRARY_SHA256_5002322 =
    "e61baf34dfc4749d92561bab5fee47891d271607a0ce44824ff61c3e6a450c3f"
private const val VIDEO_LIBRARY_SHA256_5002363 =
    "628821feab199d7712be8a51273eb9a21ec440a7c91aa6a768cc7307a4fe22f0"

private val SRGB8_INSTRUCTION = byteArrayOf(0x69, 0x88.toByte(), 0x91.toByte(), 0x52)
private val RGB10_A2_INSTRUCTION = byteArrayOf(0x29, 0x0b, 0x90.toByte(), 0x52)
// MOV W9, #GL_RGBA16F (34842). Same guarded create-info field as the other formats.
private val RGBA16F_INSTRUCTION = byteArrayOf(0x49, 0x03, 0x91.toByte(), 0x52)
private val SWAPCHAIN_CONTEXT_BEFORE = byteArrayOf(
    0xe1.toByte(), 0xa3.toByte(), 0x00, 0x91.toByte(),
    0xe0.toByte(), 0x03, 0x14, 0xaa.toByte(),
    0xe2.toByte(), 0x03, 0x1c, 0xaa.toByte(),
    0xe8.toByte(), 0x22, 0x09, 0x9b.toByte(),
)
private val SWAPCHAIN_CONTEXT_AFTER = byteArrayOf(
    0xe9.toByte(), 0x1b, 0x00, 0xf9.toByte(),
    0x08, 0x21, 0x40, 0xb9.toByte(),
    0xe8.toByte(), 0x3b, 0x00, 0xb9.toByte(),
)
internal val SWAPCHAIN_FORMAT_OFFSETS_5001712 = intArrayOf(0x10a9c4, 0x10aa34)
internal val SWAPCHAIN_FORMAT_OFFSETS_5001740 = intArrayOf(0x10a854, 0x10a8c4)
internal val SWAPCHAIN_FORMAT_OFFSETS_5002244 = intArrayOf(0x10826c, 0x1082dc, 0x10834c)
internal val SWAPCHAIN_FORMAT_OFFSETS_5002313 = intArrayOf(0x10b2d4, 0x10b344, 0x10b3b4)
internal val SWAPCHAIN_FORMAT_OFFSETS_5002318 = intArrayOf(0x10b430, 0x10b4a0, 0x10b510)
internal val SWAPCHAIN_FORMAT_OFFSETS_5002322 = intArrayOf(0x10ba78, 0x10bae8, 0x10bb58)
internal val SWAPCHAIN_FORMAT_OFFSETS_5002363 = intArrayOf(0x10c840, 0x10c8b0, 0x10c920)

private data class VideoLibraryLayout(
    val versionName: String,
    val versionCode: Int,
    val fileSize: Int,
    val stockSha256: String,
    val swapchainFormatOffsets: IntArray,
)

private val VIDEO_LIBRARY_LAYOUTS = listOf(
    VideoLibraryLayout(
        "2.0.20",
        5001712,
        VIDEO_LIBRARY_SIZE_5001712,
        VIDEO_LIBRARY_SHA256_5001712,
        SWAPCHAIN_FORMAT_OFFSETS_5001712,
    ),
    VideoLibraryLayout(
        "2.0.20",
        5001740,
        VIDEO_LIBRARY_SIZE_5001740,
        VIDEO_LIBRARY_SHA256_5001740,
        SWAPCHAIN_FORMAT_OFFSETS_5001740,
    ),
    VideoLibraryLayout(
        "2.0.22",
        5002244,
        VIDEO_LIBRARY_SIZE_5002244,
        VIDEO_LIBRARY_SHA256_5002244,
        SWAPCHAIN_FORMAT_OFFSETS_5002244,
    ),
    VideoLibraryLayout(
        "2.0.22",
        5002313,
        VIDEO_LIBRARY_SIZE_5002313,
        VIDEO_LIBRARY_SHA256_5002313,
        SWAPCHAIN_FORMAT_OFFSETS_5002313,
    ),
    VideoLibraryLayout(
        "2.0.22",
        5002318,
        VIDEO_LIBRARY_SIZE_5002318,
        VIDEO_LIBRARY_SHA256_5002318,
        SWAPCHAIN_FORMAT_OFFSETS_5002318,
    ),
    VideoLibraryLayout(
        "2.0.22",
        5002322,
        VIDEO_LIBRARY_SIZE_5002322,
        VIDEO_LIBRARY_SHA256_5002322,
        SWAPCHAIN_FORMAT_OFFSETS_5002322,
    ),
    VideoLibraryLayout(
        "2.0.23",
        5002363,
        VIDEO_LIBRARY_SIZE_5002363,
        VIDEO_LIBRARY_SHA256_5002363,
        SWAPCHAIN_FORMAT_OFFSETS_5002363,
    ),
)

internal fun isSupportedVideoLibrarySize(size: Int): Boolean =
    VIDEO_LIBRARY_LAYOUTS.any { it.fileSize == size }

internal enum class VideoOutputPrecision(val optionValue: String) {
    SRGB8_HIGHP("srgb8-highp"),
    RGB10_A2_EXPERIMENTAL("rgb10-a2-experimental"),
    RGBA16F_EXPERIMENTAL("rgba16f-experimental");

    companion object {
        fun fromOption(value: String?): VideoOutputPrecision =
            entries.singleOrNull { it.optionValue == value }
                ?: throw PatchException("Unknown video output precision: $value")
    }
}

internal enum class VideoDitherMode(val optionValue: String) {
    OFF("off"), LOW("low"), STANDARD("standard");

    companion object {
        fun fromOption(value: String?): VideoDitherMode =
            entries.singleOrNull { it.optionValue == value }
                ?: throw PatchException("Unknown OLED dithering mode: $value")
    }
}

internal fun resolveVideoOutputPrecision(
    selected: VideoOutputPrecision,
    dither: VideoDitherMode,
    use8BitOutputWhenDithering: Boolean,
): VideoOutputPrecision =
    if (use8BitOutputWhenDithering && dither != VideoDitherMode.OFF)
        VideoOutputPrecision.SRGB8_HIGHP else selected

// Fovea VD-Like modes: two mutually exclusive input-depth declarations. The fovea gate is a
// compact per-pixel weight derived from uvmask (the same 4-section geometry as Valve's
// masked alpha suffix) that bounds the dithered 10->8 pass to the high-acuity region, so the
// periphery is left untouched (lighter on the SoC than VD's full-frame dither). OFF keeps the
// legacy calibrated path byte-for-byte (dither disabled, no fovea weight). INPUT_10BIT applies
// the fovea-gated 10->8 dither; INPUT_8BIT keeps the gate but leaves the dither disabled
// (neutral), since an already-8-bit input needs no 10->8 quantize. Both always emit 8-bit sRGB.
internal enum class FoveaMode(val optionValue: String) {
    OFF("off"),
    INPUT_10BIT("input-10bit"),
    INPUT_8BIT("input-8bit");
}

internal fun resolveFoveaMode(input10Bit: Boolean, input8Bit: Boolean): FoveaMode = when {
    input10Bit && input8Bit -> throw PatchException(
        "Fovea VD-Like Input 10 bit and Fovea VD-Like Input 8 bit are mutually exclusive; " +
            "select at most one",
    )
    input10Bit -> FoveaMode.INPUT_10BIT
    input8Bit -> FoveaMode.INPUT_8BIT
    else -> FoveaMode.OFF
}

// Defaults remain noise-free. Optional comparison dither is applied in calibrated
// sRGB code space before EOTF, independently of the projection storage precision.
private val HIGHP_SHADER_TEMPLATE = """#version 300 es
#extension GL_OES_EGL_image_external_essl3 : enable
precision highp float;
in vec2 uvmask;
in vec2 uv;
out vec4 color;
layout(location=2) uniform highp samplerExternalOES tex0;
layout(location=3) uniform float fFadeAmount;
layout(location=4) uniform vec3 UniReserved1;
layout(location=5) uniform vec4 UniReserved2;
layout(location=6) uniform vec4 UniDitherOffsets;
const float DITHER_SCALE=DITHER_SCALE_VALUE;
const float DITHER_ENABLE=0.;
void main()
{
color=texture(tex0,uv);
mat3 _valve1_d2020d709=mat3(
1.04988847,.05442289,.00393458,
-.04433306,.96052738,.01122383,
-.005557,-.01509698,.98628952);
vec3 c=_valve1_d2020d709*color.rgb;
c=pow(clamp(c,0.,1.),vec3(GAMMA_VALUE));
float y=dot(c,vec3(.2126,.7152,.0722));
c=clamp(mix(vec3(y),c,SATURATION_VALUE),0.,1.);
vec3 q=OUTPUT_CONVERSION;
vec3 n=(fract(UniDitherOffsets.a*.43+UniDitherOffsets.rgb+
vec3(gl_FragCoord.x*1.67+gl_FragCoord.y*1.127))-.5)*DITHER_SCALE*DITHER_ENABLE;
n*=smoothstep(0.,DITHER_GUARD_VALUE,q)*smoothstep(0.,DITHER_GUARD_VALUE,1.-q);
color.rgb=clamp(q+n,0.,1.)*fFadeAmount;
""".trimStart('\n')

// Compact foveal weight from uvmask, using the same 4-section geometry as Valve's masked
// alpha suffix. 1.0 at a section centre (the fovea), 0.0 at the section edges (periphery).
// Well-defined everywhere: d components are in [0, .5] so dot(d,d) is in [0, .5] and
// clamp(1. - dot(d,d)*4., 0., 1.) stays in [0, 1]; unlike the exact masked-suffix pow curve it
// never produces a negative argument (which would yield NaN at the section edges).
private val FOVEA_WEIGHT_LINES =
    "vec2 d=abs(fract(uvmask*vec2(1.,4.))-.5);\n" +
    "float f=clamp(1.-dot(d,d)*4.,0.,1.);\n"

internal fun paddedVideoShader(
    gamma: Float,
    saturation: Float,
    outputPrecision: VideoOutputPrecision,
    dither: VideoDitherMode = VideoDitherMode.OFF,
    foveaGate: Boolean = false,
): ByteArray {
    val gammaValue = String.format(Locale.US, "%.2f", gamma)
    val saturationValue = String.format(Locale.US, "%.2f", saturation)
    val (ditherScale, ditherGuard, outputConversion) = when (outputPrecision) {
        VideoOutputPrecision.SRGB8_HIGHP -> Triple(".00392", ".0157", "c")
        VideoOutputPrecision.RGB10_A2_EXPERIMENTAL, VideoOutputPrecision.RGBA16F_EXPERIMENTAL ->
            Triple(
                ".00073",
                ".00391",
                "mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c))",
            )
    }
    var template = if (dither == VideoDitherMode.OFF) HIGHP_SHADER_TEMPLATE else {
        val convertedQ = if (outputPrecision == VideoOutputPrecision.SRGB8_HIGHP) "q" else
            "mix(q/12.92,pow((q+.055)/1.055,vec3(2.4)),step(vec3(.04045),q))"
        HIGHP_SHADER_TEMPLATE
            .replace("const float DITHER_ENABLE=0.;", "const float DITHER_ENABLE=1.;")
            .replace("vec3 q=OUTPUT_CONVERSION;", "vec3 q=c;")
            .replace("color.rgb=clamp(q+n,0.,1.)*fFadeAmount;",
                "q=clamp(q+n,0.,1.);color.rgb=$convertedQ*fFadeAmount;")
    }
    if (foveaGate) {
        // Bound the dithered 10->8 pass to the fovea: insert the compact uvmask-derived weight
        // and scale the noise n by it. With dither OFF the weight multiplies zero, so the 8-bit
        // neutral path stays byte-exact to the calibrated output apart from the (unused) gate.
        val noiseAnchor = "vec3 n=(fract(UniDitherOffsets.a*.43+UniDitherOffsets.rgb+\n"
        require(noiseAnchor in template) {
            "Fovea gate anchor missing from the video shader template"
        }
        template = template
            .replace(noiseAnchor, FOVEA_WEIGHT_LINES + noiseAnchor)
            .replace("*DITHER_SCALE*DITHER_ENABLE;", "*DITHER_SCALE*DITHER_ENABLE*f;")
    }
    val src = template
        .replace("GAMMA_VALUE", gammaValue)
        .replace("SATURATION_VALUE", saturationValue)
        .replace("DITHER_SCALE_VALUE", when (dither) {
            VideoDitherMode.OFF -> ditherScale
            VideoDitherMode.LOW -> ".00196"
            VideoDitherMode.STANDARD -> ".00392"
        })
        .replace("DITHER_GUARD_VALUE", if (dither == VideoDitherMode.OFF) ditherGuard else ".0157")
        .replace("OUTPUT_CONVERSION", outputConversion)
        .toByteArray(Charsets.US_ASCII)
    if (src.size > VIDEO_SHADER_SIZE) {
        throw PatchException("Calibration shader exceeds $VIDEO_SHADER_SIZE bytes (${src.size})")
    }
    return src.copyOf(VIDEO_SHADER_SIZE).apply {
        for (i in src.size until VIDEO_SHADER_SIZE) this[i] = ' '.code.toByte()
    }
}

private fun ByteArray.indicesOfSubarray(pattern: ByteArray): List<Int> {
    if (pattern.isEmpty() || size < pattern.size) return emptyList()
    val matches = mutableListOf<Int>()
    outer@ for (i in 0..size - pattern.size) {
        for (j in pattern.indices) if (this[i + j] != pattern[j]) continue@outer
        matches += i
    }
    return matches
}

internal fun findVideoShader(bytes: ByteArray): Int {
    val extensionMatches = bytes.indicesOfSubarray(SHADER_EXTENSION)
    if (extensionMatches.size != 1) {
        throw PatchException("Expected one video GLSL extension marker, found ${extensionMatches.size}")
    }
    val extensionPos = extensionMatches.single()
    val searchStart = (extensionPos - 32).coerceAtLeast(0)
    val shaderPos = (searchStart..extensionPos).firstOrNull { pos ->
        pos + SHADER_VERSION.size <= bytes.size &&
            bytes.copyOfRange(pos, pos + SHADER_VERSION.size).contentEquals(SHADER_VERSION)
    } ?: throw PatchException("Video GLSL version directive not found before extension marker")
    if (shaderPos + VIDEO_SHADER_SIZE >= bytes.size || bytes[shaderPos + VIDEO_SHADER_SIZE] != 0.toByte()) {
        throw PatchException(
            "Video GLSL block at 0x${shaderPos.toString(16)} lacks expected " +
                "$VIDEO_SHADER_SIZE-byte NUL boundary",
        )
    }
    return shaderPos
}

private fun ByteArray.matchesAt(offset: Int, expected: ByteArray): Boolean =
    offset >= 0 && offset + expected.size <= size &&
        expected.indices.all { this[offset + it] == expected[it] }

private fun ByteArray.sha256(): String =
    MessageDigest.getInstance("SHA-256").digest(this).joinToString("") { "%02x".format(it) }

internal fun setProjectionSwapchainFormat(
    bytes: ByteArray,
    outputPrecision: VideoOutputPrecision,
    versionName: String,
    versionCode: String,
): ByteArray {
    val layout = VIDEO_LIBRARY_LAYOUTS.singleOrNull {
        it.versionName == versionName && it.versionCode.toString() == versionCode
    } ?: return bytes.copyOf()
    if (bytes.size != layout.fileSize) {
        throw PatchException(
            "Unsupported libvrlink_scene.so size=${bytes.size}, sha256=${bytes.sha256()} for " +
                "Steam Link $versionName/$versionCode; expected size=${layout.fileSize}, " +
                "stockSha256=${layout.stockSha256}",
        )
    }

    val states = layout.swapchainFormatOffsets.map { offset ->
        if (!bytes.matchesAt(offset - SWAPCHAIN_CONTEXT_BEFORE.size, SWAPCHAIN_CONTEXT_BEFORE) ||
            !bytes.matchesAt(offset + SRGB8_INSTRUCTION.size, SWAPCHAIN_CONTEXT_AFTER)
        ) {
            throw PatchException(
                "Swapchain format context precondition failed at 0x${offset.toString(16)}",
            )
        }
        when {
            bytes.matchesAt(offset, SRGB8_INSTRUCTION) -> VideoOutputPrecision.SRGB8_HIGHP
            bytes.matchesAt(offset, RGB10_A2_INSTRUCTION) -> VideoOutputPrecision.RGB10_A2_EXPERIMENTAL
            bytes.matchesAt(offset, RGBA16F_INSTRUCTION) -> VideoOutputPrecision.RGBA16F_EXPERIMENTAL
            else -> throw PatchException(
                "Unsupported swapchain format instruction at 0x${offset.toString(16)}",
            )
        }
    }
    val recognizedOffsets = (
        bytes.indicesOfSubarray(SWAPCHAIN_CONTEXT_BEFORE + SRGB8_INSTRUCTION + SWAPCHAIN_CONTEXT_AFTER) +
            bytes.indicesOfSubarray(SWAPCHAIN_CONTEXT_BEFORE + RGB10_A2_INSTRUCTION + SWAPCHAIN_CONTEXT_AFTER) +
            bytes.indicesOfSubarray(SWAPCHAIN_CONTEXT_BEFORE + RGBA16F_INSTRUCTION + SWAPCHAIN_CONTEXT_AFTER)
        ).map { it + SWAPCHAIN_CONTEXT_BEFORE.size }.sorted()
    if (recognizedOffsets != layout.swapchainFormatOffsets.sorted()) {
        throw PatchException(
            "Swapchain format context offsets for $versionName/$versionCode were " +
                recognizedOffsets.joinToString { "0x${it.toString(16)}" } +
                "; expected " + layout.swapchainFormatOffsets.joinToString { "0x${it.toString(16)}" },
        )
    }
    if (states.distinct().size != 1) {
        throw PatchException("Mixed projection swapchain format state: ${states.joinToString()}")
    }

    val replacement = when (outputPrecision) {
        VideoOutputPrecision.SRGB8_HIGHP -> SRGB8_INSTRUCTION
        VideoOutputPrecision.RGB10_A2_EXPERIMENTAL -> RGB10_A2_INSTRUCTION
        VideoOutputPrecision.RGBA16F_EXPERIMENTAL -> RGBA16F_INSTRUCTION
    }
    return bytes.copyOf().apply {
        layout.swapchainFormatOffsets.forEach { replacement.copyInto(this, it) }
    }
}

// Guarded A/B generator is pinned to its decoded 2.0.22/5002244 source.
internal fun setProjectionSwapchainFormat(
    bytes: ByteArray,
    outputPrecision: VideoOutputPrecision,
): ByteArray = setProjectionSwapchainFormat(bytes, outputPrecision, "2.0.22", "5002244")

@Suppress("unused")
val oledCalibrationPatch = rawResourcePatch(
    name = "OLED color calibration",
    description = "A patch trying to emulate what VD does with the 10-bit info but only on the fovea and always outputs 8 bit. Steam Link builds 5001712, 5001740, 5002244, 5002313, 5002318, 5002322, and 5002363.",
    default = false,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK.toTypedArray())

    val profile by stringOption(
        key = "profile",
        default = "final-balanced",
        values = mapOf(
            "Initial tested (gamma 1.06, saturation 1.12)" to "initial",
            "Final balanced tested (gamma 1.20, saturation 1.45)" to "final-balanced",
            "Neutral comparison (gamma 1.00, saturation 1.00)" to "neutral",
            "Custom gamma and saturation" to "custom",
        ),
        title = "Calibration profile",
        description = "Selects the gamma and saturation pair used in the 1087-byte video shader.",
        required = true,
    )

    val gamma = floatSliderOption(
        key = "gamma",
        min = 0.50f,
        max = 2.50f,
        default = 1.20f,
        step = 0.01f,
        title = "Gamma",
        description = "Used by Custom profile. Allowed range: 0.50 to 2.50.",
        required = true,
    )

    val saturation = floatSliderOption(
        key = "saturation",
        min = 0.00f,
        max = 3.00f,
        default = 1.45f,
        step = 0.01f,
        title = "Saturation",
        description = "Used by Custom profile. Allowed range: 0.00 to 3.00.",
        required = true,
    )

    val foveaVdLike10Bit by booleanOption(
        key = "foveaVdLike10Bit",
        default = false,
        title = "Fovea VD-Like Input 10 bit",
        description = "Declares the decoded video texture as 10-bit (host negotiated Main10/P010) and applies a fovea-gated VD-Like 10->8 dithered quantize, bounded to the high-acuity region so the periphery is left untouched. The output is always 8-bit sRGB. Mutually exclusive with Fovea VD-Like Input 8 bit (the resolver enforces this). The toggle declares the assumed input depth; it does not force the host stream depth, which is host-negotiated.",
        required = true,
    )

    val foveaVdLike8Bit by booleanOption(
        key = "foveaVdLike8Bit",
        default = false,
        title = "Fovea VD-Like Input 8 bit",
        description = "Declares the decoded video texture as already 8-bit (host sent Main8) and applies the fovea-gated neutral path (no 10->8 dither is needed because the input is already 8-bit). The output is always 8-bit sRGB. Mutually exclusive with Fovea VD-Like Input 10 bit (the resolver enforces this). The toggle declares the assumed input depth; it does not force the host stream depth, which is host-negotiated.",
        required = true,
    )

    execute {
        // Shader and swapchain edits are coupled. On an unrecognized native layout, skip both
        // rather than aborting the complete APK experiment or writing fixed offsets blindly.
        val layout = VIDEO_LIBRARY_LAYOUTS.singleOrNull {
            it.versionName == packageMetadata.versionName &&
                it.versionCode.toString() == packageMetadata.versionCode
        } ?: return@execute
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        val bytes = file.readBytes()
        if (bytes.size != layout.fileSize) {
            throw PatchException(
                "Unsupported libvrlink_scene.so size=${bytes.size} for Steam Link " +
                    "${packageMetadata.versionName}/${packageMetadata.versionCode}; " +
                    "expected ${layout.fileSize}",
            )
        }
        val shaderPos = findVideoShader(bytes)
        val (selectedGamma, selectedSaturation) = when (profile) {
            "initial" -> 1.06f to 1.12f
            "final-balanced" -> 1.20f to 1.45f
            "neutral" -> 1.00f to 1.00f
            "custom" -> gamma.value!! to saturation.value!!
            else -> throw PatchException("Unknown OLED calibration profile: $profile")
        }
        // The two fovea toggles are mutually exclusive; the resolver enforces that server-side,
        // not just in the UI. Both always emit 8-bit sRGB projection storage; the toggle only
        // selects the assumed input depth, which chooses the fovea-gated dither scale (10-bit ->
        // fovea-gated dither, 8-bit -> fovea-gated neutral). OFF (both off) keeps the legacy
        // calibrated path byte-for-byte (dither disabled, no fovea weight).
        val foveaMode = resolveFoveaMode(foveaVdLike10Bit == true, foveaVdLike8Bit == true)
        val (dither, foveaGate) = when (foveaMode) {
            FoveaMode.OFF -> VideoDitherMode.OFF to false
            FoveaMode.INPUT_10BIT -> VideoDitherMode.STANDARD to true
            FoveaMode.INPUT_8BIT -> VideoDitherMode.OFF to true
        }
        val precision = VideoOutputPrecision.SRGB8_HIGHP
        val shaderPatched = bytes.copyOf().apply {
            paddedVideoShader(selectedGamma, selectedSaturation, precision,
                dither, foveaGate).copyInto(this, shaderPos)
        }
        file.writeBytes(
            setProjectionSwapchainFormat(
                shaderPatched,
                precision,
                packageMetadata.versionName,
                packageMetadata.versionCode,
            ),
        )
    }
}
