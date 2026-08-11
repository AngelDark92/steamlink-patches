.class final Lcom/valvesoftware/steamlink/GxrResolutionProbe$RemovalRunnable;
.super Ljava/lang/Object;
.implements Ljava/lang/Runnable;

.field private final latch:Ljava/util/concurrent/CountDownLatch;
.field private final result:[Z

.method constructor <init>([ZLjava/util/concurrent/CountDownLatch;)V
    .locals 0
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    iput-object p1, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$RemovalRunnable;->result:[Z
    iput-object p2, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$RemovalRunnable;->latch:Ljava/util/concurrent/CountDownLatch;
    return-void
.end method

.method public run()V
    .locals 3
    invoke-static {}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->removeOverlay()Z
    move-result v0
    iget-object v1, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$RemovalRunnable;->result:[Z
    const/4 v2, 0x0
    aput-boolean v0, v1, v2
    iget-object v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$RemovalRunnable;->latch:Ljava/util/concurrent/CountDownLatch;
    invoke-virtual {v0}, Ljava/util/concurrent/CountDownLatch;->countDown()V
    return-void
.end method
