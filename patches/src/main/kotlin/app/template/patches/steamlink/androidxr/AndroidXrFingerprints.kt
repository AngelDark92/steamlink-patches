package app.template.patches.steamlink.androidxr

import app.morphe.patcher.Fingerprint

object SDLSurfaceOnTouchFingerprint : Fingerprint(
    definingClass = "Lorg/libsdl/app/SDLSurface;",
    name = "onTouch",
)

object SDLGenericMotionListenerOnGenericMotionEventFingerprint : Fingerprint(
    definingClass = "Lorg/libsdl/app/SDLGenericMotionListener_API14;",
    name = "onGenericMotionEvent",
)
