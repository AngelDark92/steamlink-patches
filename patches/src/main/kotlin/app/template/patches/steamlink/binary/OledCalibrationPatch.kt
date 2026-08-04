package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK

// Unique prefix present in VRLink's embedded video fragment shader.
// Used to locate the 1087-byte shader block in the .so without a fixed offset.
private val SHADER_ANCHOR = "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : enable\n".toByteArray(Charsets.US_ASCII)
private const val SHADER_SIZE = 1087

// Initial calibration: gamma 1.06, saturation 1.12, zero-centred dither.
private val INITIAL_SHADER = """#version 300 es
#extension GL_OES_EGL_image_external_essl3 : enable
precision mediump float;

in vec2 uvmask;
in vec2 uv;
out vec4 color;

layout(location=2) uniform samplerExternalOES tex0;
layout(location=3) uniform float fFadeAmount;
layout(location=4) uniform vec3 UniReserved1;
layout(location=5) uniform vec4 UniReserved2;
layout(location=6) uniform vec4 UniDitherOffsets;

void main()
{
    color = texture(tex0, uv);
    mat3 _valve1_d2020d709 = mat3(
        1.04988847,  0.05442289,  0.00393458,
       -0.04433306,  0.96052738,  0.01122383,
       -0.005557,   -0.01509698,  0.98628952);
    vec3 c = _valve1_d2020d709 * color.rgb;
    c = pow(clamp(c, 0.0, 1.0), vec3(1.06));
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(y), c, 1.12);
    float n = (fract(UniDitherOffsets.a * .43 + UniDitherOffsets.r +
        gl_FragCoord.x * 1.67 + gl_FragCoord.y * 1.127) - .5) * .00292;
    n *= smoothstep(.01, .04, max(c.r, max(c.g, c.b)));
    color.rgb = clamp(c + n, 0.0, 1.0) * fFadeAmount;
""".trimStart('\n')

// Final calibration: gamma 1.20, saturation 1.45 (stronger correction).
private val FINAL_SHADER = INITIAL_SHADER
    .replace("vec3(1.06)", "vec3(1.20)")
    .replace("c, 1.12",   "c, 1.45")

private fun paddedShader(profile: String): ByteArray {
    val src = when (profile) {
        "initial"       -> INITIAL_SHADER
        "final-balanced" -> FINAL_SHADER
        else -> throw PatchException("Unknown OLED calibration profile: $profile")
    }.toByteArray(Charsets.US_ASCII)
    if (src.size > SHADER_SIZE) throw PatchException("Calibration shader exceeds $SHADER_SIZE bytes (${src.size})")
    return src.copyOf(SHADER_SIZE).apply {
        for (i in src.size until SHADER_SIZE) this[i] = ' '.code.toByte()
    }
}

private fun ByteArray.indexOfSubarray(pattern: ByteArray): Int {
    outer@ for (i in 0..size - pattern.size) {
        for (j in pattern.indices) { if (this[i + j] != pattern[j]) continue@outer }
        return i
    }
    return -1
}

@Suppress("unused")
val oledCalibrationPatch = rawResourcePatch(
    name = "OLED color calibration",
    description = "Replaces VRLink's embedded GLSL fragment shader with a Galaxy XR OLED-tuned version. Profile 'initial': gamma 1.06/sat 1.12. Profile 'final-balanced': gamma 1.20/sat 1.45.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    val profile by stringOption(
        key = "profile",
        default = "initial",
        values = mapOf("initial" to "initial", "final-balanced" to "final-balanced"),
        title = "Calibration profile",
        description = "'initial' applies conservative correction; 'final-balanced' applies full correction.",
        required = true,
    )

    execute {
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        val bytes = file.readBytes()
        val shaderPos = bytes.indexOfSubarray(SHADER_ANCHOR)
        if (shaderPos < 0) throw PatchException("GLSL shader header not found in libvrlink_scene.so")
        val result = bytes.copyOf()
        paddedShader(profile!!).copyInto(result, shaderPos)
        file.writeBytes(result)
    }
}
