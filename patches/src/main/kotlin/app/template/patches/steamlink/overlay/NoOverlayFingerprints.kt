package app.template.patches.steamlink.overlay

import app.morphe.patcher.Fingerprint

object EnsureCompositorOverlayFingerprint : Fingerprint(
    definingClass = "Lcom/valvesoftware/steamlink/SteamLink;",
    name = "ensureCompositorOverlay",
)

object RequestOverlayPermissionFingerprint : Fingerprint(
    definingClass = "Lcom/valvesoftware/steamlink/SteamLink;",
    name = "requestOverlayPermission",
)
