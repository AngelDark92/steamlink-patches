# Phase 4: Overlay Test Variant (Alternate Window Type: TYPE_APPLICATION)
#
# This variant creates a window with TYPE_APPLICATION (0x2) instead of
# TYPE_APPLICATION_OVERLAY (0x7f6). TYPE_APPLICATION does not require
# SYSTEM_ALERT_WINDOW permission.
#
# Window spec:
# - Type: TYPE_APPLICATION (0x2) — does not require SYSTEM_ALERT_WINDOW
# - Size: 2×2 pixels (same as original overlay)
# - Background: transparent (same as original)
# - Position: gravity=center, offset=(0,0) (same as original)
#
# To use: Replace GxrOverlayBridge.smali with this version
#
# Expected outcome: If resolution equals baseline, then TYPE_APPLICATION works
# for compositor signaling. If resolution drops, TYPE_APPLICATION_OVERLAY is
# specifically required, or resolution is independent of window type.

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
    .locals 8

    sget-object v0, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->sOverlay:Landroid/view/View;

    if-eqz v0, :create

    const/4 v0, 0x1

    return v0

    :create
    new-instance v1, Landroid/view/View;

    invoke-direct {v1, p0}, Landroid/view/View;-><init>(Landroid/content/Context;)V

    const/high16 v0, 0x1000000

    invoke-virtual {v1, v0}, Landroid/view/View;->setBackgroundColor(I)V

    const-string v0, "window"

    invoke-virtual {p0, v0}, Landroid/content/Context;->getSystemService(Ljava/lang/String;)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Landroid/view/WindowManager;

    new-instance v2, Landroid/view/WindowManager$LayoutParams;

    const/4 v3, 0x2

    const/4 v4, 0x2

    const/4 v5, 0x2

    # VARIANT: Use TYPE_APPLICATION (0x2) instead of TYPE_APPLICATION_OVERLAY (0x7f6)
    # TYPE_APPLICATION does not require SYSTEM_ALERT_WINDOW permission

    const v6, 0x1000318

    const/4 v7, -0x3

    invoke-direct/range {v2 .. v7}, Landroid/view/WindowManager$LayoutParams;-><init>(IIIII)V

    const/16 v3, 0x33

    iput v3, v2, Landroid/view/WindowManager$LayoutParams;->gravity:I

    const/4 v3, 0x0

    iput v3, v2, Landroid/view/WindowManager$LayoutParams;->x:I

    iput v3, v2, Landroid/view/WindowManager$LayoutParams;->y:I

    const-string v3, "SteamLinkOverlay"

    invoke-virtual {v2, v3}, Landroid/view/WindowManager$LayoutParams;->setTitle(Ljava/lang/CharSequence;)V

    :try_start
    invoke-interface {v0, v1, v2}, Landroid/view/WindowManager;->addView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V

    sput-object v0, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->sWindowManager:Landroid/view/WindowManager;

    sput-object v1, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->sOverlay:Landroid/view/View;

    const-string v0, "SteamLinkGXR"

    const-string v1, "Installed compositor window (TYPE_APPLICATION 0x2 variant, no overlay permission required)"

    invoke-static {v0, v1}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I
    :try_end
    .catch Ljava/lang/Exception; {:try_start .. :try_end} :catch_error

    const/4 v0, 0x1

    return v0

    :catch_error
    move-exception v0

    const-string v1, "SteamLinkGXR"
    
    const-string v2, "Failed to create TYPE_APPLICATION window (variant)"
    
    invoke-static {v1, v2, v0}, Landroid/util/Log;->e(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I

    const/4 v0, 0x0

    return v0
.end method
