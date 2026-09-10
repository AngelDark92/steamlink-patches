package util

import app.template.patches.steamlink.androidxr.*
import app.template.patches.steamlink.binary.*
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest

/** Exercises the shipped installer with actual decoded ELF bytes. This is not an APK installation. */
object SurfaceVideoDecodedAudit {
    private fun ByteArray.hash()=MessageDigest.getInstance("SHA-256").digest(this).joinToString("") { "%02x".format(it) }
    @JvmStatic fun main(args: Array<String>) {
        val repo=File(args.single()).canonicalFile
        val stockHashes=listOf("80b62797c7e26d6b67b0cca00693b076a336bdb48ebc1383a16cccb1616ed495",
            "e61baf34dfc4749d92561bab5fee47891d271607a0ce44824ff61c3e6a450c3f")
        for ((i,base) in surfaceVideoBases.withIndex()) {
            val decoded=File(repo,"decoded-apk-android-steamlinkvr-release-base-${base.version}-${base.code}")
            val metadata=File(decoded,"apktool.yml").readText()
            for ((key,value) in listOf("versionName" to base.version,"versionCode" to base.code))
                check(Regex("(?m)^\\s*$key: ['\"]?${Regex.escape(value)}['\"]?\\s*$").containsMatchIn(metadata))
            val stock=File(decoded,"lib/arm64-v8a/libvrlink_scene.so").readBytes()
            check(stock.hash()==stockHashes[i])
            val work=Files.createTempDirectory("surface-video-${base.code}-").toFile()
            try {
                val scene=File(work,"lib/arm64-v8a/libvrlink_scene.so").apply { parentFile.mkdirs() }
                // All source precision/dither combinations must retain the exact creation contract.
                var cases=0
                for (sourcePrecision in VideoOutputPrecision.entries) for (dither in VideoDitherMode.entries) {
                    val input=stock.copyOf()
                    paddedVideoShader(1.06f,1.12f,sourcePrecision,dither).copyInto(input,findVideoShader(input))
                    val source=setProjectionSwapchainFormat(input,sourcePrecision,base.version,base.code)
                    validateSurfaceVideoScene(base,patchVisualDelay(source,60))
                    scene.writeBytes(source)
                    for (precision in listOf("srgb8","fp16-linear","srgb8")) {
                        check(installSurfaceVideo(work,base.version,base.code,precision))
                        check(scene.readBytes().contentEquals(source))
                        val expected=projectionModeResource(surfaceVideoResource(base,precision))
                        val helper=File(scene.parentFile,SURFACE_VIDEO_LIBRARY)
                        check(helper.readBytes().contentEquals(expected))
                        check(installSurfaceVideo(work,base.version,base.code,precision))
                        check(helper.readBytes().contentEquals(expected))
                        val files=work.walkTopDown().filter { it.isFile }.map { it.relativeTo(work).invariantSeparatorsPath }.toSet()
                        check(files==setOf("lib/arm64-v8a/libvrlink_scene.so","lib/arm64-v8a/$SURFACE_VIDEO_LIBRARY",
                            "assets/openxr/1/api_layers/implicit.d/$SURFACE_VIDEO_MANIFEST"))
                        cases++
                    }
                }
                val bad=stock.copyOf().also { it[base.wrapperOffset]=(it[base.wrapperOffset].toInt() xor 1).toByte() }
                check(runCatching { validateSurfaceVideoScene(base,bad) }.isFailure)
                println("PASS ${base.version}/${base.code}: $cases installer/transition cases, each reapplied; all 9 OLED precision/dither combinations; scene unchanged; unknown wrapper rejected.")
            } finally { work.deleteRecursively() }
            check(File(decoded,"lib/arm64-v8a/libvrlink_scene.so").readBytes().hash()==stockHashes[i])
        }
        println("No APK/device/kernel mutation. Runtime Surface/GPU acceptance unverified.")
    }
}
