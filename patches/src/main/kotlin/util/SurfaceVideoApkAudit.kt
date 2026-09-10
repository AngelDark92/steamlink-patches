package util

import app.morphe.patcher.Patcher
import app.morphe.patcher.PatcherConfig
import app.morphe.patcher.apk.ApkUtils.applyTo
import app.template.patches.steamlink.androidxr.*
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import java.io.File
import java.util.zip.ZipFile

/** Run each case in a fresh JVM: the Morphe DSL keeps execution state on patch objects. */
object SurfaceVideoApkAudit {
    @JvmStatic fun main(args: Array<String>) = runBlocking {
        val repo=File(args[0]).canonicalFile
        val base=surfaceVideoBases.single { it.code==args[1] }
        val precision=args[2]
        val fixture=File(repo,"build/decoded-fixture-apks/decoded-apk-android-steamlinkvr-release-base-${base.version}-${base.code}.apk")
        val work=File(repo,"build/surface-video-apk-audit/${base.code}-$precision-${System.nanoTime()}").apply { mkdirs() }
        val input=fixture.copyTo(File(work,"input.apk"))
        val output=input.copyTo(File(work,"patched-unsigned.apk"))
        xrAndroidSurfaceVideoPatch.options["surfacePrecision"]=precision
        Patcher(PatcherConfig(input,File(work,"temporary"))).use { patcher ->
            patcher += setOf(xrAndroidSurfaceVideoPatch)
            val failed=patcher().toList().filter { it.exception!=null }
            check(failed.isEmpty()) { failed.joinToString("\n") { it.exception!!.stackTraceToString() } }
            patcher.get().applyTo(output)
        }
        ZipFile(fixture).use { before -> ZipFile(output).use { after ->
            fun bytes(zip: ZipFile,name: String)=zip.getInputStream(requireNotNull(zip.getEntry(name))).readBytes()
            for (entry in before.entries().asSequence().filter { !it.isDirectory &&
                (it.name.startsWith("lib/") || it.name.endsWith(".dex") || it.name.startsWith("assets/config/")) }) {
                check(bytes(before,entry.name).contentEquals(bytes(after,entry.name))) { "Changed ${entry.name}" }
            }
            check(bytes(after,"lib/arm64-v8a/$SURFACE_VIDEO_LIBRARY").contentEquals(
                projectionModeResource(surfaceVideoResource(base,precision))))
            check(bytes(after,"assets/openxr/1/api_layers/implicit.d/$SURFACE_VIDEO_MANIFEST").contentEquals(
                projectionModeResource(SURFACE_VIDEO_MANIFEST)))
            val manifest=bytes(after,"AndroidManifest.xml")
            check(manifest.toString(Charsets.UTF_16LE).contains(SURFACE_VIDEO_MODE) ||
                manifest.toString(Charsets.UTF_8).contains(SURFACE_VIDEO_MODE))
            check(after.getEntry("lib/arm64-v8a/$ANDROID_SURFACE_TRIGGER_LIBRARY")==null)
        } }
        println("PASS ${base.version}/${base.code} $precision: actual Morphe DSL, unsigned decoded-fixture output, original native/DEX/config bytes preserved: $output")
    }
}
