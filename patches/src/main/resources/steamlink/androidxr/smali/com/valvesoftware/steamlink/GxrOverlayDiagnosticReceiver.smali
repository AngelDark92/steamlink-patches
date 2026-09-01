.class public Lcom/valvesoftware/steamlink/GxrOverlayDiagnosticReceiver;
.super Landroid/content/BroadcastReceiver;

.method public constructor <init>()V
    .locals 0

    invoke-direct {p0}, Landroid/content/BroadcastReceiver;-><init>()V

    return-void
.end method

.method public onReceive(Landroid/content/Context;Landroid/content/Intent;)V
    .locals 2

    if-eqz p2, :done

    invoke-virtual {p2}, Landroid/content/Intent;->getAction()Ljava/lang/String;

    move-result-object v0

    const-string v1, "com.valvesoftware.steamlinkvr.GXR_OVERLAY_DIAGNOSTIC"

    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v0

    if-eqz v0, :done

    const-string v0, "state"

    invoke-virtual {p2, v0}, Landroid/content/Intent;->getStringExtra(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v0

    invoke-static {p1, v0}, Lcom/valvesoftware/steamlink/GxrOverlayBridge;->setDiagnosticState(Landroid/content/Context;Ljava/lang/String;)Z

    move-result v0

    if-eqz v0, :failed

    const/4 v0, 0x0

    invoke-virtual {p0, v0}, Landroid/content/BroadcastReceiver;->setResultCode(I)V

    goto :done

    :failed
    const/4 v0, 0x1

    invoke-virtual {p0, v0}, Landroid/content/BroadcastReceiver;->setResultCode(I)V

    :done
    return-void
.end method
