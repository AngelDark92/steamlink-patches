.class public Lcom/valvesoftware/steamlink/VRLink;
.super Landroid/app/NativeActivity;

.method protected onResume()V
    .locals 1

    invoke-super {p0}, Landroid/app/NativeActivity;->onResume()V

    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->onVrLinkResume(Landroid/app/Activity;)Z

    move-result v0

    return-void
.end method
