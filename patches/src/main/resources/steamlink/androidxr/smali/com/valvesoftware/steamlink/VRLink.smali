.class public Lcom/valvesoftware/steamlink/VRLink;
.super Landroid/app/NativeActivity;

.method protected onResume()V
    .locals 1

    invoke-super {p0}, Landroid/app/NativeActivity;->onResume()V

    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->onVrLinkResume(Landroid/app/Activity;)Z

    move-result v0

    return-void
.end method

.method public onWindowFocusChanged(Z)V
    .locals 0

    invoke-super {p0, p1}, Landroid/app/NativeActivity;->onWindowFocusChanged(Z)V

    invoke-static {p0, p1}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->onVrLinkFocusChanged(Landroid/app/Activity;Z)V

    return-void
.end method

.method protected onDestroy()V
    .locals 1

    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->onVrLinkDestroy(Landroid/app/Activity;)Z

    move-result v0

    invoke-super {p0}, Landroid/app/NativeActivity;->onDestroy()V

    return-void
.end method
