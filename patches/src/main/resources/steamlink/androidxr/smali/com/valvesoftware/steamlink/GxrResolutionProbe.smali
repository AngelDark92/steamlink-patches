.class public Lcom/valvesoftware/steamlink/GxrResolutionProbe;
.super Ljava/lang/Object;

.field private static final META_MODE:Ljava/lang/String; = "com.valvesoftware.steamlink.GXR_RESOLUTION_MODE"

.field private static sDecorView:Landroid/view/View;

.field private static sWindow:Landroid/view/View;

.field private static sWindowManager:Landroid/view/WindowManager;

.method public constructor <init>()V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method

.method public static getMode(Landroid/content/Context;)Ljava/lang/String;
    .locals 4

    :try_start
    invoke-virtual {p0}, Landroid/content/Context;->getPackageManager()Landroid/content/pm/PackageManager;

    move-result-object v0

    invoke-virtual {p0}, Landroid/content/Context;->getPackageName()Ljava/lang/String;

    move-result-object v1

    const/16 v2, 0x80

    invoke-virtual {v0, v1, v2}, Landroid/content/pm/PackageManager;->getApplicationInfo(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;

    move-result-object v0

    iget-object v0, v0, Landroid/content/pm/ApplicationInfo;->metaData:Landroid/os/Bundle;

    if-eqz v0, :fallback

    const-string v1, "com.valvesoftware.steamlink.GXR_RESOLUTION_MODE"

    invoke-virtual {v0, v1}, Landroid/os/Bundle;->getString(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v0

    if-eqz v0, :fallback

    return-object v0
    :try_end
    .catch Ljava/lang/Exception; {:try_start .. :try_end} :catch_error

    :catch_error
    move-exception v0

    :fallback
    const-string v0, "legacy"

    return-object v0
.end method

.method public static shouldRequestOverlay(Landroid/content/Context;)Z
    .locals 2

    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->getMode(Landroid/content/Context;)Ljava/lang/String;

    move-result-object v0

    const-string v1, "overlay_granted_control"

    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v1

    if-nez v1, :enabled

    const-string v1, "legacy"

    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v0

    if-eqz v0, :disabled

    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->isEnabled(Landroid/content/Context;)Z

    move-result v0

    return v0

    :enabled
    const/4 v0, 0x1

    return v0

    :disabled
    const/4 v0, 0x0

    return v0
.end method

.method public static onSteamLinkCreate(Landroid/app/Activity;)Z
    .locals 1

    const-string v0, "on_create"

    invoke-static {p0, v0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->ensure(Landroid/app/Activity;Ljava/lang/String;)Z

    move-result v0

    return v0
.end method

.method public static onSteamLinkResume(Landroid/app/Activity;)Z
    .locals 1

    const-string v0, "on_resume"

    invoke-static {p0, v0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->ensure(Landroid/app/Activity;Ljava/lang/String;)Z

    move-result v0

    return v0
.end method

.method public static beforeVrLaunch(Landroid/app/Activity;)Z
    .locals 1

    const-string v0, "before_vr"

    invoke-static {p0, v0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->ensure(Landroid/app/Activity;Ljava/lang/String;)Z

    move-result v0

    return v0
.end method

.method public static ensure(Landroid/app/Activity;Ljava/lang/String;)Z
    .locals 6

    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->getMode(Landroid/content/Context;)Ljava/lang/String;

    move-result-object v0

    invoke-static {p0}, Landroid/provider/Settings;->canDrawOverlays(Landroid/content/Context;)Z

    move-result v1

    invoke-virtual {p0}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object v2

    invoke-virtual {v2}, Landroid/view/Window;->getDecorView()Landroid/view/View;

    move-result-object v2

    invoke-virtual {v2}, Landroid/view/View;->getWindowToken()Landroid/os/IBinder;

    move-result-object v2

    new-instance v3, Ljava/lang/StringBuilder;

    const-string v4, "probe mode="

    invoke-direct {v3, v4}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v3, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    const-string v4, " phase="

    invoke-virtual {v3, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    const-string v4, " canDraw="

    invoke-virtual {v3, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3, v1}, Ljava/lang/StringBuilder;->append(Z)Ljava/lang/StringBuilder;

    move-result-object v3

    const-string v4, " token="

    invoke-virtual {v3, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    if-eqz v2, :no_token

    const/4 v2, 0x1

    goto :append_token

    :no_token
    const/4 v2, 0x0

    :append_token
    invoke-virtual {v3, v2}, Ljava/lang/StringBuilder;->append(Z)Ljava/lang/StringBuilder;

    move-result-object v2

    const-string v3, " elapsedMs="

    invoke-virtual {v2, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-static {}, Landroid/os/SystemClock;->elapsedRealtime()J

    move-result-wide v3

    invoke-virtual {v2, v3, v4}, Ljava/lang/StringBuilder;->append(J)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    const-string v3, "SteamLinkGXR"

    invoke-static {v3, v2}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I

    const-string v2, "no_window_control"

    invoke-virtual {v2, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-nez v2, :disabled

    const-string v2, "decor_view"

    invoke-virtual {v2, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-nez v2, :decor

    const-string v2, "application_window_direct_vr"

    invoke-virtual {v2, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-eqz v2, :not_direct

    const-string v2, "before_vr"

    invoke-virtual {v2, p1}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-eqz v2, :disabled

    goto :application


    :not_direct
    const-string v2, "application_window"

    invoke-virtual {v2, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-nez v2, :application

    const-string v2, "overlay_denied_probe"

    invoke-virtual {v2, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-nez v2, :overlay_denied

    const-string v2, "overlay_granted_control"

    invoke-virtual {v2, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-nez v2, :overlay_granted

    const-string v2, "legacy"

    invoke-virtual {v2, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v2

    if-eqz v2, :disabled

    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->ensureOverlay(Landroid/content/Context;)Z

    move-result v0

    return v0

    :overlay_granted
    if-eqz v1, :disabled

    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->ensureOverlay(Landroid/content/Context;)Z

    move-result v0

    return v0

    :overlay_denied
    const/16 v0, 0x7f6

    invoke-static {p0, v0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->addWindow(Landroid/app/Activity;I)Z

    move-result v0

    return v0

    :application
    const/4 v0, 0x2

    invoke-static {p0, v0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->addWindow(Landroid/app/Activity;I)Z

    move-result v0

    return v0

    :decor
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->addDecorView(Landroid/app/Activity;)Z

    move-result v0

    return v0

    :disabled
    const/4 v0, 0x0

    return v0
.end method

.method private static addWindow(Landroid/app/Activity;I)Z
    .locals 9

    sget-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindow:Landroid/view/View;

    if-eqz v0, :create

    const-string v0, "SteamLinkGXR"

    const-string v1, "probe window already installed"

    invoke-static {v0, v1}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I

    const/4 v0, 0x1

    return v0

    :create
    :try_start
    new-instance v1, Landroid/view/View;

    invoke-direct {v1, p0}, Landroid/view/View;-><init>(Landroid/content/Context;)V

    const/high16 v0, 0x1000000

    invoke-virtual {v1, v0}, Landroid/view/View;->setBackgroundColor(I)V

    const-string v0, "window"

    invoke-virtual {p0, v0}, Landroid/app/Activity;->getSystemService(Ljava/lang/String;)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Landroid/view/WindowManager;

    new-instance v2, Landroid/view/WindowManager$LayoutParams;

    const/4 v3, 0x2

    const/4 v4, 0x2

    move v5, p1

    const v6, 0x1000318

    const/4 v7, -0x3

    invoke-direct/range {v2 .. v7}, Landroid/view/WindowManager$LayoutParams;-><init>(IIIII)V

    const/4 v3, 0x2

    if-ne p1, v3, :parameters_ready

    invoke-virtual {p0}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object v3

    invoke-virtual {v3}, Landroid/view/Window;->getDecorView()Landroid/view/View;

    move-result-object v3

    invoke-virtual {v3}, Landroid/view/View;->getWindowToken()Landroid/os/IBinder;

    move-result-object v3

    iput-object v3, v2, Landroid/view/WindowManager$LayoutParams;->token:Landroid/os/IBinder;

    :parameters_ready
    const/16 v3, 0x33

    iput v3, v2, Landroid/view/WindowManager$LayoutParams;->gravity:I

    const-string v3, "SteamLinkResolutionProbe"

    invoke-virtual {v2, v3}, Landroid/view/WindowManager$LayoutParams;->setTitle(Ljava/lang/CharSequence;)V

    invoke-interface {v0, v1, v2}, Landroid/view/WindowManager;->addView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V

    sput-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindowManager:Landroid/view/WindowManager;

    sput-object v1, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindow:Landroid/view/View;

    new-instance v2, Ljava/lang/StringBuilder;

    const-string v3, "probe addView success type="

    invoke-direct {v2, v3}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v2, p1}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    const-string v3, "SteamLinkGXR"

    invoke-static {v3, v2}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I
    :try_end
    .catch Ljava/lang/RuntimeException; {:try_start .. :try_end} :failed

    const/4 v0, 0x1

    return v0

    :failed
    move-exception v0

    const-string v1, "SteamLinkGXR"

    const-string v2, "probe addView failed"

    invoke-static {v1, v2, v0}, Landroid/util/Log;->w(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I

    const/4 v0, 0x0

    return v0
.end method

.method private static addDecorView(Landroid/app/Activity;)Z
    .locals 5

    sget-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sDecorView:Landroid/view/View;

    if-eqz v0, :create

    const/4 v0, 0x1

    return v0

    :create
    :try_start
    new-instance v0, Landroid/view/View;

    invoke-direct {v0, p0}, Landroid/view/View;-><init>(Landroid/content/Context;)V

    const/high16 v1, 0x1000000

    invoke-virtual {v0, v1}, Landroid/view/View;->setBackgroundColor(I)V

    invoke-virtual {p0}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object v1

    invoke-virtual {v1}, Landroid/view/Window;->getDecorView()Landroid/view/View;

    move-result-object v1

    check-cast v1, Landroid/view/ViewGroup;

    new-instance v2, Landroid/view/ViewGroup$LayoutParams;

    const/4 v3, 0x2

    invoke-direct {v2, v3, v3}, Landroid/view/ViewGroup$LayoutParams;-><init>(II)V

    invoke-virtual {v1, v0, v2}, Landroid/view/ViewGroup;->addView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V

    sput-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sDecorView:Landroid/view/View;

    const-string v0, "SteamLinkGXR"

    const-string v1, "probe decor view installed"

    invoke-static {v0, v1}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I
    :try_end
    .catch Ljava/lang/RuntimeException; {:try_start .. :try_end} :failed

    const/4 v0, 0x1

    return v0

    :failed
    move-exception v0

    const-string v1, "SteamLinkGXR"

    const-string v2, "probe decor view failed"

    invoke-static {v1, v2, v0}, Landroid/util/Log;->w(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I

    const/4 v0, 0x0

    return v0
.end method