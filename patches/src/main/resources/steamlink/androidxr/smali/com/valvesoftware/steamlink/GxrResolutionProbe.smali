.class public Lcom/valvesoftware/steamlink/GxrResolutionProbe;
.super Ljava/lang/Object;

.field private static final META_MODE:Ljava/lang/String; = "com.valvesoftware.steamlink.GXR_RESOLUTION_MODE"
.field private static sAfterVrScheduled:Z
.field private static sWindow:Landroid/view/View;
.field private static sWindowManager:Landroid/view/WindowManager;

.method public constructor <init>()V
    .locals 0
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method private static native hasOpenXrFrameNative()Z
.end method

.method public static isOpenXrFrameReady()Z
    .locals 1
    :try_start_frame_ready
    const-string v0, "gxr_resolution_trace"
    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
    invoke-static {}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->hasOpenXrFrameNative()Z
    move-result v0
    return v0
    :try_end_frame_ready
    .catch Ljava/lang/UnsatisfiedLinkError; {:try_start_frame_ready .. :try_end_frame_ready} :frame_not_ready
    :frame_not_ready
    move-exception v0
    const/4 v0, 0x0
    return v0
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
    .catch Ljava/lang/Exception; {:try_start .. :try_end} :fallback_exception

    :fallback_exception
    move-exception v0
    :fallback
    const-string v0, "legacy"
    return-object v0
.end method

.method public static shouldRequestOverlay(Landroid/content/Context;)Z
    .locals 2
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->getMode(Landroid/content/Context;)Ljava/lang/String;
    move-result-object v0
    const-string v1, "granted_no_window"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :enabled
    const-string v1, "overlay_live_before_vr"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :enabled
    const-string v1, "overlay_remove_before_vr"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :enabled
    const-string v1, "overlay_after_vr"
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

.method public static onOverlayPermissionReady(Landroid/content/Context;)Z
    .locals 5
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->getMode(Landroid/content/Context;)Ljava/lang/String;
    move-result-object v0
    const-string v1, "overlay_live_before_vr"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :install
    const-string v1, "overlay_remove_before_vr"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :install
    const-string v1, "legacy"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :install
    const-string v1, "permission_ready"
    const-string v2, "no_window"
    const/4 v3, 0x0
    const/4 v4, 0x0
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
    const/4 v0, 0x1
    return v0
    :install
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->ensureOverlay(Landroid/content/Context;)Z
    move-result v0
    const-string v1, "permission_ready"
    const-string v2, "overlay_add"
    const/16 v3, 0x7f6
    move v4, v0
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
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

.method public static onVrLinkResume(Landroid/app/Activity;)Z
    .locals 6
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->getMode(Landroid/content/Context;)Ljava/lang/String;
    move-result-object v0

    invoke-static {}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->isApplicationWindowAttached()Z
    move-result v4
    const-string v1, "vrlink_resume"
    const-string v2, "lifecycle"
    const/4 v3, 0x0
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V

    const-string v1, "application_window_vrlink_live"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-eqz v1, :not_live_application_window
    const/4 v1, 0x2
    invoke-static {p0, v1}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->addWindowOnUiThread(Landroid/app/Activity;I)Z
    move-result v0
    return v0

    :not_live_application_window
    const-string v1, "overlay_after_vr"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v0
    if-eqz v0, :disabled
    sget-boolean v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sAfterVrScheduled:Z
    if-nez v0, :enabled
    const/4 v0, 0x1
    sput-boolean v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sAfterVrScheduled:Z
    new-instance v1, Landroid/os/Handler;
    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;
    move-result-object v2
    invoke-direct {v1, v2}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V
    new-instance v2, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;
    invoke-direct {v2, p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;-><init>(Landroid/content/Context;)V
    const-wide/16 v3, 0x64
    invoke-virtual {v1, v2, v3, v4}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z
    move-result v0
    const-string v1, "vrlink_resume"
    const-string v2, "overlay_scheduled_after_first_frame"
    const/16 v3, 0x7f6
    const/4 v4, 0x0
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
    return v0
    :enabled
    const/4 v0, 0x1
    return v0
    :disabled
    const/4 v0, 0x0
    return v0
.end method

.method public static onVrLinkFocusChanged(Landroid/app/Activity;Z)V
    .locals 5
    invoke-static {}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->isApplicationWindowAttached()Z
    move-result v4
    const-string v1, "vrlink_focus"
    if-eqz p1, :focus_lost
    const-string v2, "focus_gained"
    goto :focus_ready
    :focus_lost
    const-string v2, "focus_lost"
    :focus_ready
    const/4 v3, 0x2
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
    return-void
.end method

.method public static onVrLinkDestroy(Landroid/app/Activity;)Z
    .locals 5
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->getMode(Landroid/content/Context;)Ljava/lang/String;
    move-result-object v0
    const-string v1, "application_window_vrlink_live"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :remove_application_window
    const-string v1, "application_window_direct_vr"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-nez v1, :remove_application_window
    invoke-static {}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->isApplicationWindowAttached()Z
    move-result v4
    const-string v1, "vrlink_destroy"
    const-string v2, "lifecycle"
    const/4 v3, 0x0
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
    const/4 v0, 0x1
    return v0
    :remove_application_window
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->removeApplicationWindow(Landroid/content/Context;)Z
    move-result v0
    return v0
.end method

.method public static ensure(Landroid/app/Activity;Ljava/lang/String;)Z
    .locals 5
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->getMode(Landroid/content/Context;)Ljava/lang/String;
    move-result-object v0
    const-string v1, "lifecycle"
    const/4 v2, 0x0
    const/4 v3, 0x0
    invoke-static {p0, p1, v1, v2, v3}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V

    const-string v1, "application_window_direct_vr"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-eqz v1, :not_direct
    const-string v1, "before_vr"
    invoke-virtual {v1, p1}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-eqz v1, :disabled
    const/4 v1, 0x2
    invoke-static {p0, v1}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->addWindowOnUiThread(Landroid/app/Activity;I)Z
    move-result v0
    return v0

    :not_direct
    const-string v1, "overlay_remove_before_vr"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-eqz v1, :not_remove
    const-string v1, "before_vr"
    invoke-virtual {v1, p1}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v1
    if-eqz v1, :disabled
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->removeOverlayOnUiThread(Landroid/app/Activity;)Z
    move-result v0
    invoke-static {}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->isOverlayAttached()Z
    move-result v4
    const-string v1, "overlay_remove"
    const/16 v2, 0x7f6
    invoke-static {p0, p1, v1, v2, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
    return v0

    :not_remove
    const-string v1, "legacy"
    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v0
    if-eqz v0, :disabled
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->onOverlayPermissionReady(Landroid/content/Context;)Z
    move-result v0
    return v0

    :disabled
    const/4 v0, 0x0
    return v0
.end method

.method public static removeOverlayOnUiThread(Landroid/app/Activity;)Z
    .locals 6
    invoke-static {}, Landroid/os/Looper;->myLooper()Landroid/os/Looper;
    move-result-object v0
    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;
    move-result-object v1
    if-ne v0, v1, :post
    invoke-static {}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->removeOverlay()Z
    move-result v0
    return v0
    :post
    const/4 v0, 0x1
    new-array v1, v0, [Z
    new-instance v2, Ljava/util/concurrent/CountDownLatch;
    invoke-direct {v2, v0}, Ljava/util/concurrent/CountDownLatch;-><init>(I)V
    new-instance v3, Lcom/valvesoftware/steamlink/GxrResolutionProbe$RemovalRunnable;
    invoke-direct {v3, v1, v2}, Lcom/valvesoftware/steamlink/GxrResolutionProbe$RemovalRunnable;-><init>([ZLjava/util/concurrent/CountDownLatch;)V
    invoke-virtual {p0, v3}, Landroid/app/Activity;->runOnUiThread(Ljava/lang/Runnable;)V
    :try_start_remove
    const-wide/16 v3, 0x2
    sget-object v5, Ljava/util/concurrent/TimeUnit;->SECONDS:Ljava/util/concurrent/TimeUnit;
    invoke-virtual {v2, v3, v4, v5}, Ljava/util/concurrent/CountDownLatch;->await(JLjava/util/concurrent/TimeUnit;)Z
    move-result v0
    :try_end_remove
    .catch Ljava/lang/InterruptedException; {:try_start_remove .. :try_end_remove} :interrupted_remove
    if-eqz v0, :timeout_remove
    const/4 v0, 0x0
    aget-boolean v0, v1, v0
    return v0
    :interrupted_remove
    invoke-static {}, Ljava/lang/Thread;->currentThread()Ljava/lang/Thread;
    move-result-object v0
    invoke-virtual {v0}, Ljava/lang/Thread;->interrupt()V
    :timeout_remove
    const-string v0, "SteamLinkGXR"
    const-string v1, "probe UI-thread removeView timed out or was interrupted"
    invoke-static {v0, v1}, Landroid/util/Log;->w(Ljava/lang/String;Ljava/lang/String;)I
    const/4 v0, 0x0
    return v0
.end method

.method public static addWindowOnUiThread(Landroid/app/Activity;I)Z
    .locals 6
    invoke-static {}, Landroid/os/Looper;->myLooper()Landroid/os/Looper;
    move-result-object v0
    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;
    move-result-object v1
    if-ne v0, v1, :post
    invoke-static {p0, p1}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->addWindow(Landroid/app/Activity;I)Z
    move-result v0
    return v0
    :post
    const/4 v0, 0x1
    new-array v1, v0, [Z
    new-instance v2, Ljava/util/concurrent/CountDownLatch;
    invoke-direct {v2, v0}, Ljava/util/concurrent/CountDownLatch;-><init>(I)V
    new-instance v3, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;
    invoke-direct {v3, p0, p1, v1, v2}, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;-><init>(Landroid/app/Activity;I[ZLjava/util/concurrent/CountDownLatch;)V
    invoke-virtual {p0, v3}, Landroid/app/Activity;->runOnUiThread(Ljava/lang/Runnable;)V
    :try_start
    const-wide/16 v3, 0x2
    sget-object v5, Ljava/util/concurrent/TimeUnit;->SECONDS:Ljava/util/concurrent/TimeUnit;
    invoke-virtual {v2, v3, v4, v5}, Ljava/util/concurrent/CountDownLatch;->await(JLjava/util/concurrent/TimeUnit;)Z
    move-result v0
    :try_end
    .catch Ljava/lang/InterruptedException; {:try_start .. :try_end} :interrupted
    if-eqz v0, :timeout
    const/4 v0, 0x0
    aget-boolean v0, v1, v0
    return v0
    :interrupted
    invoke-static {}, Ljava/lang/Thread;->currentThread()Ljava/lang/Thread;
    move-result-object v0
    invoke-virtual {v0}, Ljava/lang/Thread;->interrupt()V
    :timeout
    const-string v0, "SteamLinkGXR"
    const-string v1, "probe UI-thread addView timed out or was interrupted"
    invoke-static {v0, v1}, Landroid/util/Log;->w(Ljava/lang/String;Ljava/lang/String;)I
    const/4 v0, 0x0
    return v0
.end method

.method public static addWindow(Landroid/app/Activity;I)Z
    .locals 9
    sget-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindow:Landroid/view/View;
    if-eqz v0, :create
    invoke-virtual {v0}, Landroid/view/View;->isAttachedToWindow()Z
    move-result v0
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
    invoke-virtual {v1}, Landroid/view/View;->isAttachedToWindow()Z
    move-result v4
    invoke-static {p0}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->getMode(Landroid/content/Context;)Ljava/lang/String;
    move-result-object v2
    const-string v3, "application_window_vrlink_live"
    invoke-virtual {v3, v2}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z
    move-result v2
    if-eqz v2, :window_phase_before_vr
    const-string v2, "vrlink_resume"
    goto :window_phase_ready
    :window_phase_before_vr
    const-string v2, "before_vr"
    :window_phase_ready
    const-string v3, "application_window_add"
    move v5, p1
    invoke-static {p0, v2, v3, v5, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
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

.method public static isApplicationWindowAttached()Z
    .locals 1
    sget-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindow:Landroid/view/View;
    if-eqz v0, :window_not_attached
    invoke-virtual {v0}, Landroid/view/View;->isAttachedToWindow()Z
    move-result v0
    return v0
    :window_not_attached
    const/4 v0, 0x0
    return v0
.end method

.method public static removeApplicationWindow(Landroid/content/Context;)Z
    .locals 6
    sget-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindow:Landroid/view/View;
    if-eqz v0, :application_window_absent
    :try_start_application_window_remove
    invoke-virtual {v0}, Landroid/view/View;->isAttachedToWindow()Z
    move-result v1
    if-eqz v1, :clear_application_window
    sget-object v1, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindowManager:Landroid/view/WindowManager;
    if-eqz v1, :clear_application_window
    invoke-interface {v1, v0}, Landroid/view/WindowManager;->removeViewImmediate(Landroid/view/View;)V
    :clear_application_window
    const/4 v0, 0x0
    sput-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindow:Landroid/view/View;
    sput-object v0, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindowManager:Landroid/view/WindowManager;
    const-string v1, "vrlink_destroy"
    const-string v2, "application_window_remove"
    const/4 v3, 0x2
    const/4 v4, 0x0
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
    :try_end_application_window_remove
    .catch Ljava/lang/RuntimeException; {:try_start_application_window_remove .. :try_end_application_window_remove} :application_window_remove_failed
    const/4 v0, 0x1
    return v0
    :application_window_remove_failed
    move-exception v0
    const/4 v1, 0x0
    sput-object v1, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindow:Landroid/view/View;
    sput-object v1, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->sWindowManager:Landroid/view/WindowManager;
    const-string v1, "SteamLinkGXR"
    const-string v2, "probe application window remove failed"
    invoke-static {v1, v2, v0}, Landroid/util/Log;->w(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I
    const-string v1, "vrlink_destroy"
    const-string v2, "application_window_remove_failed"
    const/4 v3, 0x2
    const/4 v4, 0x0
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
    const/4 v0, 0x0
    return v0
    :application_window_absent
    const-string v1, "vrlink_destroy"
    const-string v2, "application_window_remove_absent"
    const/4 v3, 0x2
    const/4 v4, 0x0
    invoke-static {p0, v1, v2, v3, v4}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V
    const/4 v0, 0x1
    return v0
.end method
