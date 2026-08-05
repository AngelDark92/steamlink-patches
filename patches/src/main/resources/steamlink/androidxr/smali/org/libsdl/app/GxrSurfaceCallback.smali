.class public Lorg/libsdl/app/GxrSurfaceCallback;
.super Ljava/lang/Object;
.implements Landroid/view/SurfaceHolder$Callback;

.field private final mOwner:Lorg/libsdl/app/SDLSurface;

.method public constructor <init>(Lorg/libsdl/app/SDLSurface;)V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    iput-object p1, p0, Lorg/libsdl/app/GxrSurfaceCallback;->mOwner:Lorg/libsdl/app/SDLSurface;

    return-void
.end method

.method public surfaceChanged(Landroid/view/SurfaceHolder;III)V
    .locals 6

    iget-object v0, p0, Lorg/libsdl/app/GxrSurfaceCallback;->mOwner:Lorg/libsdl/app/SDLSurface;

    iget-object v0, v0, Lorg/libsdl/app/SDLSurface;->mDisplay:Landroid/view/Display;

    invoke-virtual {v0}, Landroid/view/Display;->getRefreshRate()F

    move-result v5

    move v0, p3

    move v1, p4

    move v2, p3

    move v3, p4

    const/high16 v4, 0x3f800000    # 1.0f

    invoke-static/range {v0 .. v5}, Lorg/libsdl/app/SDLActivity;->nativeSetScreenResolution(IIIIFF)V

    invoke-static {}, Lorg/libsdl/app/SDLActivity;->onNativeResize()V

    const-string v0, "SteamLinkGXR"

    const-string v1, "Applied managed-panel surface metrics"

    invoke-static {v0, v1}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I

    return-void
.end method

.method public surfaceCreated(Landroid/view/SurfaceHolder;)V
    .locals 0

    return-void
.end method

.method public surfaceDestroyed(Landroid/view/SurfaceHolder;)V
    .locals 0

    return-void
.end method
