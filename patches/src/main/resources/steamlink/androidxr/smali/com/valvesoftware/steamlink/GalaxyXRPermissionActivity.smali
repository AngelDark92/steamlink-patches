.class public Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;
.super Landroid/app/Activity;
.source "GalaxyXRPermissionActivity.java"

.method public constructor <init>()V
    .locals 0

    invoke-direct {p0}, Landroid/app/Activity;-><init>()V

    return-void
.end method

.method private launchSteamLink()V
    .locals 2

    new-instance v0, Landroid/content/Intent;

    const-class v1, Lcom/valvesoftware/steamlink/SteamLink;

    invoke-direct {v0, p0, v1}, Landroid/content/Intent;-><init>(Landroid/content/Context;Ljava/lang/Class;)V

    const/high16 v1, 0x4000000

    invoke-virtual {v0, v1}, Landroid/content/Intent;->addFlags(I)Landroid/content/Intent;

    invoke-virtual {p0, v0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->startActivity(Landroid/content/Intent;)V

    invoke-virtual {p0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->finish()V

    return-void
.end method

.method protected onCreate(Landroid/os/Bundle;)V
    .locals 3

    invoke-super {p0, p1}, Landroid/app/Activity;->onCreate(Landroid/os/Bundle;)V

    const-string v0, "android.permission.HAND_TRACKING"

    invoke-virtual {p0, v0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->checkSelfPermission(Ljava/lang/String;)I

    move-result v1

    if-nez v1, :request_permissions

    const-string v0, "android.permission.EYE_TRACKING_FINE"

    invoke-virtual {p0, v0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->checkSelfPermission(Ljava/lang/String;)I

    move-result v1

    if-nez v1, :request_permissions

    const-string v0, "android.permission.RECORD_AUDIO"

    invoke-virtual {p0, v0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->checkSelfPermission(Ljava/lang/String;)I

    move-result v1

    if-nez v1, :request_permissions

    const-string v0, "android.permission.BLUETOOTH_CONNECT"

    invoke-virtual {p0, v0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->checkSelfPermission(Ljava/lang/String;)I

    move-result v1

    if-nez v1, :request_permissions

    invoke-direct {p0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->launchSteamLink()V

    return-void

    :request_permissions
    const/4 v0, 0x4

    new-array v0, v0, [Ljava/lang/String;

    const/4 v1, 0x0

    const-string v2, "android.permission.HAND_TRACKING"

    aput-object v2, v0, v1

    const/4 v1, 0x1

    const-string v2, "android.permission.EYE_TRACKING_FINE"

    aput-object v2, v0, v1

    const/4 v1, 0x2

    const-string v2, "android.permission.RECORD_AUDIO"

    aput-object v2, v0, v1

    const/4 v1, 0x3

    const-string v2, "android.permission.BLUETOOTH_CONNECT"

    aput-object v2, v0, v1

    const/16 v1, 0x4758

    invoke-virtual {p0, v0, v1}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->requestPermissions([Ljava/lang/String;I)V

    return-void
.end method

.method public onRequestPermissionsResult(I[Ljava/lang/String;[I)V
    .locals 0

    invoke-super {p0, p1, p2, p3}, Landroid/app/Activity;->onRequestPermissionsResult(I[Ljava/lang/String;[I)V

    invoke-direct {p0}, Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;->launchSteamLink()V

    return-void
.end method
