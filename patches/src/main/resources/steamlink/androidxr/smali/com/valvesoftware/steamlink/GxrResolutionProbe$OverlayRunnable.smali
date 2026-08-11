.class final Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;
.super Ljava/lang/Object;
.implements Ljava/lang/Runnable;

.field private final context:Landroid/content/Context;
.field private attempts:I

.method constructor <init>(Landroid/content/Context;)V
    .locals 1

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    invoke-virtual {p1}, Landroid/content/Context;->getApplicationContext()Landroid/content/Context;
    move-result-object p1
    iput-object p1, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->context:Landroid/content/Context;

    const/4 v0, 0x0
    iput v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->attempts:I

    return-void
.end method

.method public run()V
    .locals 6

    invoke-static {}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->isOpenXrFrameReady()Z
    move-result v0
    if-nez v0, :add_overlay

    iget v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->attempts:I
    add-int/lit8 v0, v0, 0x1
    iput v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->attempts:I
    const/16 v1, 0x78
    if-ge v0, v1, :wait_timeout

    new-instance v1, Landroid/os/Handler;
    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;
    move-result-object v2
    invoke-direct {v1, v2}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V
    const-wide/16 v3, 0xfa
    invoke-virtual {v1, p0, v3, v4}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z
    return-void

    :wait_timeout
    iget-object v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->context:Landroid/content/Context;
    const/4 v1, 0x0
    const-string v2, "after_vr_first_frame"
    const-string v3, "overlay_wait_timeout"
    const/16 v4, 0x7f6
    const/4 v5, 0x0
    invoke-static/range {v0 .. v5}, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->emitResult(Landroid/content/Context;ZLjava/lang/String;Ljava/lang/String;IZ)V
    return-void

    :add_overlay

    iget-object v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->context:Landroid/content/Context;

    invoke-static {v0}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->ensureOverlay(Landroid/content/Context;)Z
    move-result v1

    const-string v2, "after_vr_first_frame"
    const-string v3, "overlay_add"
    const/16 v4, 0x7f6
    move v5, v1
    invoke-static/range {v0 .. v5}, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->emitResult(Landroid/content/Context;ZLjava/lang/String;Ljava/lang/String;IZ)V

    return-void
.end method

.method private static emitResult(Landroid/content/Context;ZLjava/lang/String;Ljava/lang/String;IZ)V
    .locals 0

    invoke-static {p0, p2, p3, p4, p5}, Lcom/valvesoftware/steamlink/GxrResolutionTrace;->emit(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;IZ)V

    return-void
.end method
