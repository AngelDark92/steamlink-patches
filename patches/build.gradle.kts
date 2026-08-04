group = "app.template"

patches {
    // Disable the Morphe extension project integration.
    // The extension DEX is assembled from smali sources by the assembleExtension task
    // and included in the patches JAR as a pre-built resource.
    extensionsProjectPath = null

    about {
        name = "Steam Link GalaxyXR Patches"
        description = "Patches for Steam Link to support Samsung Galaxy XR hardware"
        source = "https://github.com/AngelDark92/steamlink-patches"
        author = "AngelDark92"
        contact = "na"
        website = "na"
        license = "GPLv3"
    }
}

kotlin {
    compilerOptions {
        freeCompilerArgs.add("-Xcontext-parameters")
    }
}

// ---------------------------------------------------------------------------
// Smali assembler: build extension.mpe from smali sources without Android SDK.
// The smali library is a transitive dependency of morphe-patcher (already on
// the runtime classpath via the Morphe plugin). We declare it explicitly here
// so it is available on the buildscript classpath for the assembleExtension task.
// ---------------------------------------------------------------------------

val smaliAssembler: Configuration = configurations.create("smaliAssembler") {
    isTransitive = true
}

dependencies {
    smaliAssembler("com.github.MorpheApp.smali:smali:${libs.versions.smali.get()}")

    // Separate configuration so gson is available at runtime for the
    // generatePatchesList task but never bundled into the APK.
    compileOnly(libs.gson)
}

val patchListGeneratorClasspath: Configuration =
    configurations.create("patchListGeneratorClasspath")

dependencies {
    patchListGeneratorClasspath(libs.gson)
}

// Output directory for the assembled extension DEX, included in the JAR.
val extensionOutputDir = layout.buildDirectory.dir("generated/extension-resources")

// Assemble GxrSdlBridge + GalaxyXRPermissionActivity smali files into extension.mpe.
val assembleExtension by tasks.registering(JavaExec::class) {
    group = "build"
    description = "Assemble extension smali files to extension.mpe (no Android SDK required)"

    val smaliSrcDir = file("src/main/resources/steamlink/androidxr/smali")
    val outputFile = extensionOutputDir.map { it.file("extensions/extension.mpe") }

    inputs.dir(smaliSrcDir)
    outputs.file(outputFile)

    classpath = smaliAssembler
    mainClass.set("com.android.tools.smali.smali.Main")
    doFirst {
        val out = outputFile.get().asFile
        out.parentFile.mkdirs()
        args(
            "a",
            "-a", "35",
            "-o", out.absolutePath,
            smaliSrcDir.absolutePath,
        )
    }
}

// Include the assembled extension.mpe in the patches JAR.
sourceSets.main {
    resources.srcDir(extensionOutputDir)
}

tasks.named("processResources") {
    dependsOn(assembleExtension)
}

tasks.named("sourcesJar") {
    dependsOn(assembleExtension)
}

tasks {
    register<JavaExec>("generatePatchesList") {
        description = "Build patch with patch list"

        dependsOn(build)

        classpath = sourceSets["main"].runtimeClasspath + patchListGeneratorClasspath
        mainClass.set("util.PatchListGeneratorKt")
    }

    // Used by gradle-semantic-release-plugin.
    publish {
        dependsOn("generatePatchesList")
    }
}
