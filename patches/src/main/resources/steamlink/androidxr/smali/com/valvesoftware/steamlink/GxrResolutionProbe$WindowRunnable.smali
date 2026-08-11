.class final Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;
.super Ljava/lang/Object;
.implements Ljava/lang/Runnable;

.field private final activity:Landroid/app/Activity;
.field private final latch:Ljava/util/concurrent/CountDownLatch;
.field private final result:[Z
.field private final windowType:I

.method constructor <init>(Landroid/app/Activity;I[ZLjava/util/concurrent/CountDownLatch;)V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    iput-object p1, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;->activity:Landroid/app/Activity;
    iput p2, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;->windowType:I
    iput-object p3, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;->result:[Z
    iput-object p4, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;->latch:Ljava/util/concurrent/CountDownLatch;

    return-void
.end method

.method public run()V
    .locals 4

    iget-object v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;->activity:Landroid/app/Activity;
    iget v1, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;->windowType:I

    invoke-static {v0, v1}, Lcom/valvesoftware/steamlink/GxrResolutionProbe;->addWindow(Landroid/app/Activity;I)Z

    move-result v0

    iget-object v1, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;->result:[Z
    const/4 v2, 0x0
    aput-boolean v0, v1, v2

    iget-object v0, p0, Lcom/valvesoftware/steamlink/GxrResolutionProbe$WindowRunnable;->latch:Ljava/util/concurrent/CountDownLatch;
    invoke-virtual {v0}, Ljava/util/concurrent/CountDownLatch;->countDown()V

    return-void
.end method
