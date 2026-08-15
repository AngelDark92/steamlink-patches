package app.template.patches.steamlink.identity

import app.morphe.patcher.patch.PatchException
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class DeviceIdentityPatchTest {
    private val stock5002318 =
        """
        {
          "requestedExtensions": ["XR_EXT_eye_gaze_interaction", "XR_PICO_eye_tracking"],
          "staticProps": {
            "hollywood": {"sModelNumber": "Oculus Quest2", "sControllerType": "rift"},
            "unknown": {
              "sTrackingSystem": "oculus",
              "sModelNumber": "oculus_quest_hmd",
              "sControllerType": "rift",
              "sInputProfilePath": "{oculus}/input/rift_profile.json"
            }
          }
        }
        """.trimIndent()

    @Test
    fun `5002318 identity changes only fallback model and preserves native profiles`() {
        val patched = patchHmdModelIdentity(stock5002318, "meta-quest-pro")
        val expected = stock5002318.replaceFirst(
            "\"sModelNumber\": \"oculus_quest_hmd\"",
            "\"sModelNumber\": \"Oculus Quest Pro\"",
        )

        assertEquals(expected, patched)
        assertTrue(patched.contains("\"XR_PICO_eye_tracking\""))
        assertEquals(
            stock5002318.substringBefore("\"unknown\""),
            patched.substringBefore("\"unknown\""),
        )
    }

    @Test
    fun `legacy identity keeps routing fields while updating all Galaxy XR models`() {
        val legacy = requireNotNull(
            javaClass.getResource("/steamlink/androidxr/hmd_config.json"),
        ).readText()
        val patched = patchHmdModelIdentity(legacy, "meta-quest-pro")

        assertEquals(3, Regex("\"sModelNumber\": \"Oculus Quest Pro\"").findAll(patched).count())
        assertEquals(3, Regex("\"sTrackingSystem\": \"SamsungVST\"").findAll(patched).count())
        assertEquals(3, Regex("\"sControllerType\": \"galaxy_xr_hmd\"").findAll(patched).count())
    }

    @Test
    fun `default identity is byte identical and invalid profile fails closed`() {
        assertEquals(stock5002318, patchHmdModelIdentity(stock5002318, "samsung-default"))
        assertFailsWith<PatchException> { patchHmdModelIdentity(stock5002318, "invalid") }
    }
}
