package app.template.patches.steamlink.androidxr

import app.morphe.patcher.extensions.InstructionExtensions.addInstruction
import app.morphe.patcher.patch.bytecodePatch
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK

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
// mutableClassDefBy()+addInstruction() (single typed instruction insertion,
// not the banned multi-instruction smali-string addInstructions() overload
// and not a Fingerprint(definingClass, name) object). This is the same
// single-instruction-string technique already used safely elsewhere in this
// project (see LowLatDecoderPatch's replaceInstruction call).
// ---------------------------------------------------------------------------

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
                    "invoke-static {p0, p3, p4}, " +
                        "Lorg/libsdl/app/GxrSurfaceCallback;->applyManagedPanelMetrics(" +
                        "Lorg/libsdl/app/SDLSurface;II)V",
                )
                addInstruction(1, "return-void")
            }

        // Forward XR ray-cast/hand pointer events to SDL mouse/gamepad input.
        // Non-invasive: the original touch handling below still runs unchanged.
        mutableClassDefBy("Lorg/libsdl/app/SDLSurface;").methods
            .first { it.name == "onTouch" }
            .addInstruction(
                0,
                "invoke-static {p2}, " +
                    "Lorg/libsdl/app/GxrSdlBridge;->routeXrPointerAsMouse(Landroid/view/MotionEvent;)V",
            )

        mutableClassDefBy("Lorg/libsdl/app/SDLGenericMotionListener_API14;").methods
            .first { it.name == "onGenericMotion" }
            .addInstruction(
                0,
                "invoke-static {p2}, " +
                    "Lorg/libsdl/app/GxrSdlBridge;->routeXrPointerAsMouseGeneric(Landroid/view/MotionEvent;)V",
            )
    }
}
