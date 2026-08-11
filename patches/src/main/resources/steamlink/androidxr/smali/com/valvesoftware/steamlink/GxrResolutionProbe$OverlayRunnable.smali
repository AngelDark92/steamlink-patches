.class final Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;
.super Ljava/lang/Object;
.implements Ljava/lang/Runnable;

.field private final context:Landroid/content/Context;

.method constructor <init>(Landroid/content/Context;)V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    invoke-virtual {p1}, Landroid/content/Context;->getApplicationContext()Landroid/content/Context;
    move-result-object p1
    iput-object p1, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->context:Landroid/content/Context;

    return-void
.end method

.method public run()V
    .locals 6

    iget-object v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$OverlayRunnable;->context:Landroid/content/Context;

    invoke-static {v0}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->ensureOverlay(Landroid/content/Context;)Z
    move-result v1

    const-string v2, "after_vr_delay"
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
