# Phase 2: Overlay Test Variant (No Overlay Window)
# 
# This file provides a no-op implementation of GxrOverlayBridge.ensureOverlay()
# to test whether the overlay window is required for full-resolution rendering.
# 
# To use: Replace the original GxrOverlayBridge.smali with this variant
# and rebuild the patch.
#
# Expected outcome: If swapchain dimensions remain unchanged, overlay is not
# required for resolution control. If they drop, overlay triggers undocumented
# compositor behavior that must be preserved.

.class public Lcom/valvesoftware/steamlink/GxrOverlayBridge;
.super Ljava/lang/Object;
.source "GxrOverlayBridge.smali"


# static fields
.field public static sOverlay:Landroid/view/View; = null

.field public static sWindowManager:Landroid/view/WindowManager; = null


# direct methods
.method public constructor <init>()V
    .registers 1

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method

.method public static isEnabled(Landroid/content/Context;)Z
    .locals 3

    if-nez p0, :check

    const/4 v0, 0x0

    return v0

    :check
    const-string v0, "android.permission.SYSTEM_ALERT_WINDOW"

    invoke-virtual {p0}, Landroid/content/Context;->getPackageManager()Landroid/content/pm/PackageManager;

    move-result-object v1

    invoke-virtual {p0}, Landroid/content/Context;->getPackageName()Ljava/lang/String;

    move-result-object v2

    invoke-virtual {v1, v0, v2}, Landroid/content/pm/PackageManager;->checkPermission(Ljava/lang/String;Ljava/lang/String;)I

    move-result v0

    if-nez v0, :success

    const/4 v0, 0x0

    return v0

    :success
    const/4 v0, 0x1

    return v0
.end method

.method public static requestPermission(Landroid/content/Context;)V
    .registers 3

    const-string v0, "android.app.action.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS"

    const-string v1, "package:com.valvesoftware.steamlinkvr"

    invoke-static {v0, v1}, Landroid/net/Uri;->parse(Ljava/lang/String;)Landroid/net/Uri;

    move-result-object v0

    new-instance v1, Landroid/content/Intent;

    const-string v0, "android.settings.action.MANAGE_OVERLAY_PERMISSION"

    invoke-direct {v1, v0}, Landroid/content/Intent;-><init>(Ljava/lang/String;)V

    const-string v0, "package"

    invoke-virtual {p0}, Landroid/content/Context;->getPackageName()Ljava/lang/String;

    move-result-object v2

    invoke-static {v0, v2}, Landroid/net/Uri;->fromParts(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;

    move-result-object v0

    invoke-virtual {v1, v0}, Landroid/content/Intent;->setData(Landroid/net/Uri;)Landroid/content/Intent;

    invoke-virtual {p0, v1}, Landroid/content/Context;->startActivity(Landroid/content/Intent;)V

    return-void
.end method

.method public static ensureOverlay(Landroid/content/Context;)Z
    .locals 2
    
    # VARIANT: No-op implementation for testing
    # Checks if overlay permission is available (for logging) but does not create window
    
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->isEnabled(Landroid/content/Context;)Z
    move-result v0
    
    if-eqz v0, :not_enabled
    
    invoke-static {p0}, Landroid/provider/Settings;->canDrawOverlays(Landroid/content/Context;)Z
    move-result v0
    
    if-eqz v0, :not_granted
    
    # Permission granted but NOT creating overlay (this is the variant behavior)
    const-string v0, "SteamLinkGXR"
    const-string v1, "ensureOverlay (VARIANT): Skip overlay creation, permission granted"
    invoke-static {v0, v1}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I
    
    const/4 v0, 0x1
    return v0
    
    :not_granted
    const-string v0, "SteamLinkGXR"
    const-string v1, "ensureOverlay (VARIANT): Permission not granted"
    invoke-static {v0, v1}, Landroid/util/Log;->w(Ljava/lang/String;Ljava/lang/String;)I
    
    const/4 v0, 0x0
    return v0
    
    :not_enabled
    const-string v0, "SteamLinkGXR"
    const-string v1, "ensureOverlay (VARIANT): Overlay not enabled in manifest"
    invoke-static {v0, v1}, Landroid/util/Log;->w(Ljava/lang/String;Ljava/lang/String;)I
    
    const/4 v0, 0x0
    return v0
.end method
