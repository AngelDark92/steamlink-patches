package app.template.patches.steamlink.identity

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.stringOption
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK
import app.template.patches.steamlink.androidxr.xrDeviceConfigBaselinePatch

private fun identityResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/identity/$name")
        ?: error("Missing bundled resource: steamlink/identity/$name"))
        .use { it.readBytes() }

/**
 * Change only the outward model identity inside the runtime-selected HMD entries.
 *
 * Build 5002318 already ships working Android XR hand/controller routing. Replacing its complete
 * HMD config with the older Galaxy XR payload also discards Valve's newer vendor profiles and
 * requested extensions, so identity spoofing must preserve every unrelated byte of that config.
 */
internal fun patchHmdModelIdentity(json: String, profile: String): String {
    val model = when (profile) {
        "samsung-default", "Samsung Galaxy XR (default, no change)" -> return json
        "meta-quest-pro", "Meta Quest Pro" -> "Oculus Quest Pro"
        "pico-4-pro", "PICO 4 Pro" -> "PICO 4 Pro"
        else -> throw PatchException("Unknown device identity profile: $profile")
    }

    // Legacy baselines identify Galaxy XR explicitly; stock 5002318 falls back to "unknown".
    val targetKeys = if (Regex("\\\"xrvst2\\\"\\s*:").containsMatchIn(json)) {
        listOf("xrvst2", "xrvst2ue", "unknown")
    } else {
        listOf("unknown")
    }

    return targetKeys.fold(json) { current, key ->
        val entryAndModel = Regex(
            "(?s)(\\\"${Regex.escape(key)}\\\"\\s*:\\s*\\{.*?" +
                "\\\"sModelNumber\\\"\\s*:\\s*\\\")([^\\\"]*)(\\\")",
        )
        val match = entryAndModel.find(current)
            ?: throw PatchException("Missing HMD identity entry '$key' or sModelNumber")
        current.replaceRange(match.groups[2]!!.range, model)
    }
}

@Suppress("unused")
val deviceIdentityPatch = rawResourcePatch(
    name = "Device identity",
    description = "Overrides the HMD identity reported to SteamVR. Build 5002318 changes only the " +
        "model so its stock controller/hand profiles and OpenXR extensions remain intact.",
    default = true,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK.toTypedArray())
    // Morphe executes dependencies without checking their compatibility. The legacy foundation is
    // therefore build-aware and becomes a mutation no-op on 5002318, while older builds retain the
    // same automatic XR baseline that Device identity historically installed.
    dependsOn(xrDeviceConfigBaselinePatch)

    val profile by stringOption(
        key = "profile",
        default = "meta-quest-pro",
        values = mapOf(
            "Samsung Galaxy XR (default, no change)" to "samsung-default",
            "Meta Quest Pro" to "meta-quest-pro",
            "PICO 4 Pro" to "pico-4-pro",
        ),
        title = "HMD identity",
        description = "5002318: changes only sModelNumber. Legacy builds retain their full verified identity payload.",
        required = true,
    )

    execute {
        val file = get("assets/config/hmd_config.json")
        val selectedProfile = profile ?: throw PatchException("HMD identity profile is required")
        if (packageMetadata.versionCode != "5002318") {
            val fileName = when (selectedProfile) {
                "samsung-default", "Samsung Galaxy XR (default, no change)" -> return@execute
                "meta-quest-pro", "Meta Quest Pro" -> "hmd_config_meta_quest_pro.json"
                "pico-4-pro", "PICO 4 Pro" -> "hmd_config_pico_4_pro.json"
                else -> throw PatchException("Unknown device identity profile: $selectedProfile")
            }
            file.writeBytes(identityResource(fileName))
            return@execute
        }

        val original = file.readText()
        val patched = patchHmdModelIdentity(original, selectedProfile)
        if (patched != original) file.writeText(patched)
    }
}
