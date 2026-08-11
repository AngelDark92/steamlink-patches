package app.template.patches.steamlink.androidxr

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class GalaxyXRPermissionActivityFlowTest {
    private val activitySmali = requireNotNull(
        javaClass.getResource(
            "/steamlink/androidxr/smali/com/valvesoftware/steamlink/" +
                "GalaxyXRPermissionActivity.smali",
        ),
    ).readText()

    @Test
    fun `battery settings follow the overlay permission request`() {
        val continuation = method("private continueAfterPermissions()V")
        val overlayRequest = continuation.indexOf("GxrOverlayBridge;->requestPermission")
        val batterySettings = continuation.indexOf("android.settings.VIEW_ADVANCED_POWER_USAGE_DETAIL")

        assertTrue(overlayRequest >= 0)
        assertTrue(batterySettings > overlayRequest)
    }

    @Test
    fun `returning from overlay settings continues to battery settings`() {
        val activityResult = method("protected onActivityResult(IILandroid/content/Intent;)V")
        val overlayResult = activityResult
            .substringAfter("if-ne p1, v0, :battery")
            .substringBefore("\n    :battery")

        assertTrue(overlayResult.contains("->continueAfterPermissions()V"))
        assertFalse(overlayResult.contains("->launchSteamLink()V"))
    }

    private fun method(signature: String): String = activitySmali
        .substringAfter(".method $signature")
        .substringBefore(".end method")
}
