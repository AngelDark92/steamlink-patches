# Phase 3: Overlay Test Variant (No Overlay + No Permission Request)
#
# This variant completely bypasses both overlay creation AND permission request flow.
# GalaxyXRPermissionActivity proceeds directly to launching VRLink without:
# - Checking for SYSTEM_ALERT_WINDOW permission
# - Requesting permission via Settings.ACTION_MANAGE_OVERLAY_PERMISSION
# - Creating any overlay window
#
# To use: Replace GalaxyXRPermissionActivity.smali with this version
#
# Expected outcome: Similar to Phase 2, but eliminates permission flow entirely.
# If app launches successfully and resolution is unchanged, overlay and permission
# are both optional for rendering.

.class public Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;
.super Landroid/app/Activity;
.source "GalaxyXRPermissionActivity.smali"


# direct methods
.method public constructor <init>()V
    .registers 1
    invoke-direct {p0}, Landroid/app/Activity;-><init>()V
    return-void
.end method

.method protected onCreate(Landroid/os/Bundle;)V
    .registers 3

    invoke-super {p0, p1}, Landroid/app/Activity;->onCreate(Landroid/os/Bundle;)V

    const-string v0, "SteamLinkGXR"
    const-string v1, "GalaxyXRPermissionActivity (VARIANT): onCreate - skipping overlay setup"
    invoke-static {v0, v1}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I

    # VARIANT: Skip all overlay and permission setup, proceed directly to VRLink
    invoke-virtual {p0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->launchVrLink()V

    invoke-virtual {p0}, Landroid/app/Activity;->finish()V

    return-void
.end method

.method private launchVrLink()V
    .registers 3

    new-instance v0, Landroid/content/Intent;

    const-string v1, "com.valvesoftware.steamlinkvr"

    const-string v1, "com.valvesoftware.steamlinkvr.VRLink"

    const/4 v2, 0x0

    invoke-static {v1}, Landroid/content/ComponentName;->unflattenFromString(Ljava/lang/String;)Landroid/content/ComponentName;

    move-result-object v1

    invoke-direct {v0, p0, v1}, Landroid/content/Intent;-><init>(Landroid/content/Context;Landroid/content/ComponentName;)V

    const/high16 v1, 0x10200000

    invoke-virtual {v0, v1}, Landroid/content/Intent;->setFlags(I)Landroid/content/Intent;

    invoke-virtual {p0, v0}, Landroid/app/Activity;->startActivity(Landroid/content/Intent;)V

    return-void
.end method
