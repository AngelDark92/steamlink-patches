package app.template.patches.steamlink.androidxr

import app.morphe.patcher.extensions.InstructionExtensions.addInstruction
import app.morphe.patcher.patch.bytecodePatch
import app.morphe.patcher.patch.resourcePatch
import app.morphe.patcher.util.proxy.mutableTypes.MutableMethod
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import com.android.tools.smali.dexlib2.AccessFlags
import com.android.tools.smali.dexlib2.Opcode
import com.android.tools.smali.dexlib2.builder.BuilderInstruction
import com.android.tools.smali.dexlib2.builder.instruction.BuilderInstruction10x
import com.android.tools.smali.dexlib2.builder.instruction.BuilderInstruction3rc
import com.android.tools.smali.dexlib2.immutable.reference.ImmutableMethodReference
import org.w3c.dom.Element

// v-register for smali "p<index>" register, assuming no wide (J/D) parameters.
private fun MutableMethod.pRegister(index: Int): Int {
    val registerCount = implementation!!.registerCount
    val paramWords = parameterTypes.size + if (AccessFlags.STATIC.isSet(accessFlags)) 0 else 1
    return registerCount - paramWords + index
}

private fun invokeStaticRange(
    definingClass: String,
    methodName: String,
    parameterTypes: List<String>,
    returnType: String,
    startRegister: Int,
    registerCount: Int,
): BuilderInstruction = BuilderInstruction3rc(
    Opcode.INVOKE_STATIC_RANGE,
    startRegister,
    registerCount,
    ImmutableMethodReference(definingClass, methodName, parameterTypes, returnType),
)

private val appearOnTopManifestPatch = resourcePatch {
    finalize {
        document("AndroidManifest.xml").use { document ->
            val manifest = document.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
            val permission = "android.permission.SYSTEM_ALERT_WINDOW"
            val permissions = document.getElementsByTagName("uses-permission")
            val exists = (0 until permissions.length)
                .mapNotNull { permissions.item(it) as? Element }
                .any { it.getAttribute("android:name") == permission }
            if (!exists) {
                val element = document.createElement("uses-permission")
                element.setAttribute("android:name", permission)
                manifest.insertBefore(element, app)
            }
        }
    }
}

@Suppress("unused")
val appearOnTopPatch = bytecodePatch(
    name = "Appear on top",
    description = "Adds SYSTEM_ALERT_WINDOW to the manifest so GalaxyXRPermissionActivity can request overlay permission at startup.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(xrLauncherBootstrapPatch, appearOnTopManifestPatch)
}

@Suppress("unused")
val xrDirectInputAndPanelMetricsPatch = bytecodePatch(
    name = "XR direct input and panel metrics",
    description = "Experimental: directly patches SDL surface/motion methods for managed-panel metrics and XR pointer routing. Disable if startup turns black.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(xrCoreRuntimePatch)

    execute {
        mutableClassDefBy("Lorg/libsdl/app/SDLSurface;").methods
            .first { it.name == "surfaceChanged" && it.parameterTypes.size == 4 }
            .apply {
                addInstruction(
                    0,
                    invokeStaticRange(
                        "Lorg/libsdl/app/GxrSurfaceCallback;",
                        "applyManagedPanelMetrics",
                        listOf(
                            "Lorg/libsdl/app/SDLSurface;",
                            "Landroid/view/SurfaceHolder;",
                            "I", "I", "I",
                        ),
                        "V",
                        pRegister(0),
                        5,
                    ),
                )
                addInstruction(1, BuilderInstruction10x(Opcode.RETURN_VOID))
            }

        mutableClassDefBy("Lorg/libsdl/app/SDLSurface;").methods
            .first { it.name == "onTouch" && it.parameterTypes.size == 2 }
            .apply {
                addInstruction(
                    0,
                    invokeStaticRange(
                        "Lorg/libsdl/app/GxrSdlBridge;",
                        "routeXrPointerAsMouse",
                        listOf("Landroid/view/MotionEvent;"),
                        "V",
                        pRegister(2),
                        1,
                    ),
                )
            }

        mutableClassDefBy("Lorg/libsdl/app/SDLGenericMotionListener_API14;").methods
            .first { it.name == "onGenericMotion" && it.parameterTypes.size == 2 }
            .apply {
                addInstruction(
                    0,
                    invokeStaticRange(
                        "Lorg/libsdl/app/GxrSdlBridge;",
                        "routeXrPointerAsMouseGeneric",
                        listOf("Landroid/view/MotionEvent;"),
                        "V",
                        pRegister(2),
                        1,
                    ),
                )
            }
    }
}
