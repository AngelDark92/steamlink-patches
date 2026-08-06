package app.template.patches.steamlink.androidxr

import app.morphe.patcher.extensions.InstructionExtensions.addInstruction
import app.morphe.patcher.patch.bytecodePatch
import app.morphe.patcher.util.proxy.mutableTypes.MutableMethod
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import com.android.tools.smali.dexlib2.AccessFlags
import com.android.tools.smali.dexlib2.Opcode
import com.android.tools.smali.dexlib2.builder.BuilderInstruction
import com.android.tools.smali.dexlib2.builder.instruction.BuilderInstruction10x
import com.android.tools.smali.dexlib2.builder.instruction.BuilderInstruction3rc
import com.android.tools.smali.dexlib2.immutable.reference.ImmutableMethodReference

// ---------------------------------------------------------------------------
// Why this patch exists
//
// morphe-patcher's extendWith() merge (ClassMerger) is ADDITIVE ONLY: for a
// class that already exists in both the original APK and the bundled
// extension DEX, it only adds methods that are *missing* by signature
// (ClassMerger.addMissingMethods). It never replaces the body of a method
// that already exists in both copies. That means bundling a hand-modified
// full copy of an existing SDL class (SDLSurface, SDLGenericMotionListener_
// API14) via smali/ silently does nothing for any method that already
// existed in the original -- the original body wins and the modification is
// discarded with no error. Confirmed by decompiling
// app.morphe.patcher.util.ClassMerger in morphe-patcher 1.7.0.
//
// SDLSurface.surfaceChanged() and onTouch(), and SDLGenericMotionListener_
// API14.onGenericMotion(), all pre-exist in the stock SDL classes bundled in
// Steam Link, so the edited copies in smali/ never take effect: the managed
// panel keeps stretching to the full physical display metrics, and pointer
// routing added there never runs.
//
// The fix is to edit the REAL method bodies directly, in-place, via
// mutableClassDefBy(). Instructions are built as raw dexlib2
// BuilderInstruction objects (BuilderInstruction3rc/10x) instead of smali
// strings: morphe-patcher's InlineSmaliCompiler (used by the
// addInstruction(index, "smali string") overload) can silently return a
// ClassDef with zero methods for some inline invoke-static bodies -- no
// syntax error is raised, but `classDef.getMethods().first()` then throws
// "Collection is empty" (NoSuchElementException). Confirmed by decompiling
// InlineSmaliCompiler in morphe-patcher 1.7.0. Building instructions
// directly bypasses that ANTLR-based compiler entirely.
//
// invoke-static (format 35c) also only encodes registers in a 4-bit nibble
// (v0..v15); real methods with many locals routinely have parameter
// registers above v15 (e.g. v22), which throws "Invalid register" at build
// time. invoke-static/range (format 3rc) has no such limit but requires a
// CONTIGUOUS register block, so surfaceChanged's call passes its full
// p0..p4 parameter range (owner instead of `this`) rather than cherry-
// picking non-contiguous p0/p3/p4.
// ---------------------------------------------------------------------------

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

@Suppress("unused")
internal val xrDirectInputFixPatch = bytecodePatch(
    name = "XR direct input & metrics fix",
    description = "Directly patches the real SDLSurface.surfaceChanged()/onTouch() and " +
        "SDLGenericMotionListener_API14.onGenericMotion() method bodies in the original app " +
        "classes, since the extension DEX merge cannot override methods that already exist " +
        "there. Applies the managed-panel aspect fix and Galaxy XR pointer routing to the " +
        "code paths that actually execute.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)
    dependsOn(androidXrUiExtensionPatch)

    execute {
        // Bypass SDLSurface's original surfaceChanged() body entirely: unpatched, it
        // feeds the full physical combined-display metrics (7104x3840) into
        // nativeSetScreenResolution(), stretching the managed-panel surface to that
        // aspect ratio instead of its own. Call the corrected static helper and
        // return immediately so the original (stretching) code below never runs.
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

        // Forward XR ray-cast/hand pointer events to SDL mouse/gamepad input.
        // Non-invasive: the original touch handling below still runs unchanged.
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
