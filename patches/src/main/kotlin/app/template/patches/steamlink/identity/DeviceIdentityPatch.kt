package app.template.patches.steamlink.identity

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK

private fun identityResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/identity/$name")
        ?: error("Missing bundled resource: steamlink/identity/$name"))
        .use { it.readBytes() }

@Suppress("unused")
val deviceIdentityPatch = rawResourcePatch(
    name = "Device identity",
    description = "Overrides the HMD manufacturer/model identity reported to SteamVR (hmd_config.json only; " +
        "controller identity is unaffected). 'samsung-default' leaves the file untouched.",
    default = false,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)

    val profile by stringOption(
        key = "profile",
        default = "samsung-default",
        values = mapOf(
            "samsung-default" to "Samsung Galaxy XR (default, no change)",
            "meta-quest-pro" to "Meta Quest Pro",
            "pico-4-pro" to "PICO 4 Pro",
        ),
        title = "HMD identity",
        description = "Which HMD identity to report to SteamVR.",
        required = true,
    )

    execute {
        // Tracking system, resource root and input/controller paths stay Samsung-based; only the
        // manufacturer/model/serial identity fields change.
        val fileName = when (profile) {
            "samsung-default" -> return@execute
            "meta-quest-pro" -> "hmd_config_meta_quest_pro.json"
            "pico-4-pro" -> "hmd_config_pico_4_pro.json"
            else -> throw PatchException("Unknown device identity profile: $profile")
        }
        get("assets/config/hmd_config.json").writeBytes(identityResource(fileName))
    }
}
