package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.bytecodePatch

// Merges extension.mpe DEX into the base APK. New classes added:
// GalaxyXRPermissionActivity, GxrOverlayBridge, GxrSdlBridge, GxrSurfaceCallback.
// Existing classes extended (new methods/fields merged): SDLSurface, SDLControllerManager, SDLGenericMotionListener_API14.
internal val androidXrUiExtensionPatch = bytecodePatch {
    extendWith("extensions/extension.mpe")
}
