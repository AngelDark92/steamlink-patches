package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.bytecodePatch
import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.resourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_5002322_EXPERIMENTAL
import org.w3c.dom.Document
import org.w3c.dom.Element
import java.io.File

private val unrestrictedBatteryManifestPatch = resourcePatch {
    finalize {
        document("AndroidManifest.xml").use { document ->
            val manifest = document.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
            val permission = "android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS"
            val permissions = document.getElementsByTagName("uses-permission")
            val exists = (0 until permissions.length)
                .mapNotNull { permissions.item(it) as? Element }
                .any { it.getAttribute("android:name") == permission }
            if (!exists) {
                val element = document.createElement("uses-permission")
                element.setAttribute("android:name", permission)
                manifest.insertBefore(element, app)
            }
        }
    }
}

@Suppress("unused")
val unrestrictedBatteryUsagePatch = bytecodePatch(
    name = "Unrestricted battery usage",
    description = "Opens Android's per-app Battery usage page at startup so Unrestricted can be selected for XR streaming.",
    default = true,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK.toTypedArray())
    // Restore the legacy automatic foundation while its native-build guards make it a no-op.
    dependsOn(
        xrLauncherBootstrapPatch,
        xrPermissionSettingsBootstrapPatch,
        unrestrictedBatteryManifestPatch,
    )
}

private val appearOnTopManifestPatch = resourcePatch {
    finalize {
        document("AndroidManifest.xml").use { document ->
            val manifest = document.documentElement
            val app = manifest.getElementsByTagName("application").item(0) as Element
            // Required by GxrOverlayBridge to add a TYPE_APPLICATION_OVERLAY compositor signal window
            val permission = "android.permission.SYSTEM_ALERT_WINDOW"
            val permissions = document.getElementsByTagName("uses-permission")
            val exists = (0 until permissions.length)
                .mapNotNull { permissions.item(it) as? Element }
                .any { it.getAttribute("android:name") == permission }
            if (!exists) {
                val element = document.createElement("uses-permission")
                element.setAttribute("android:name", permission)
                manifest.insertBefore(element, app)
            }
        }
    }
}

@Suppress("unused")
val appearOnTopPatch = bytecodePatch(
    name = "Appear on top",
    description = "Adds SYSTEM_ALERT_WINDOW to the manifest so GalaxyXRPermissionActivity can request overlay permission at startup.",
    default = true,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK.toTypedArray())
    dependsOn(
        xrLauncherBootstrapPatch,
        xrPermissionSettingsBootstrapPatch,
        appearOnTopManifestPatch,
    )
}

private const val SINGLE_PROJECTION_MODE = "single_projection_reconstruction_v1"
private const val SINGLE_PROJECTION_LIBRARY = "libgxr_single_projection_reconstruction_v1.so"
private const val SINGLE_PROJECTION_MANIFEST =
    "XR_APILAYER_local_GalaxyXR_single_projection_reconstruction_v1.json"
private const val TWO_PROJECTION_MODE = "two_projection_drop_base_v1"
private const val TWO_PROJECTION_LIBRARY = "libgxr_two_projection_drop_base_v1.so"
private const val TWO_PROJECTION_MANIFEST =
    "XR_APILAYER_local_GalaxyXR_two_projection_drop_base_v1.json"
private const val THREE_PROJECTION_PROXY_MODE = "three_projection_sampler_proxy_v1"
private const val THREE_PROJECTION_PROXY_LIBRARY = "libgxr_three_projection_sampler_proxy_v1.so"
private const val THREE_PROJECTION_PROXY_MANIFEST =
    "XR_APILAYER_local_GalaxyXR_three_projection_sampler_proxy_v1.json"

private data class ProjectionModeResources(
    val mode: String,
    val library: String,
    val manifest: String,
)

private val activeProjectionModes = listOf(
    ProjectionModeResources(
        SINGLE_PROJECTION_MODE,
        SINGLE_PROJECTION_LIBRARY,
        SINGLE_PROJECTION_MANIFEST,
    ),
    ProjectionModeResources(
        TWO_PROJECTION_MODE,
        TWO_PROJECTION_LIBRARY,
        TWO_PROJECTION_MANIFEST,
    ),
    ProjectionModeResources(
        THREE_PROJECTION_PROXY_MODE,
        THREE_PROJECTION_PROXY_LIBRARY,
        THREE_PROJECTION_PROXY_MANIFEST,
    ),
)

internal fun projectionModesConflict(existingMode: String, requestedMode: String): Boolean =
    existingMode.isNotEmpty() && existingMode != requestedMode &&
        activeProjectionModes.any { it.mode == existingMode }

private val retiredProjectionModes = setOf(
    "projection_trace_control",
    "projection_settings_quality",
    "projection_settings_stripped",
    "vrlink_unmanaged_full_space",
    "projection_metadata_compat_v2",
)

private fun projectionModeResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/$name")
        ?: throw PatchException("Missing bundled XR projection-mode resource: $name"))
        .use { it.readBytes() }

private fun installProjectionModeResources(
    libDir: File,
    layerDir: File,
    requested: ProjectionModeResources,
) {
    activeProjectionModes.filterNot { it.mode == requested.mode }.forEach { sibling ->
        if (File(libDir, sibling.library).exists() || File(layerDir, sibling.manifest).exists()) {
            throw PatchException(
                "Experimental XR projection modes are mutually exclusive: " +
                    "${requested.mode} conflicts with ${sibling.mode}",
            )
        }
    }

    retiredProjectionModes.forEach { mode ->
        File(libDir, "libgxr_$mode.so").delete()
        File(layerDir, "XR_APILAYER_local_GalaxyXR_$mode.json").delete()
    }

    File(libDir, requested.library).writeBytes(projectionModeResource(requested.library))
    val layerManifest = File(layerDir, requested.manifest)
    layerManifest.parentFile!!.mkdirs()
    layerManifest.writeBytes(projectionModeResource(requested.manifest))
}

private fun configurePermissionFreeProjectionMode(document: Document, requestedMode: String) {
    val manifest = document.documentElement
    val app = manifest.getElementsByTagName("application").item(0) as Element
    val permissionName = "android.permission.SYSTEM_ALERT_WINDOW"
    val permissionNodes = document.getElementsByTagName("uses-permission")
    val matchingPermissions = (0 until permissionNodes.length)
        .mapNotNull { permissionNodes.item(it) as? Element }
        .filter { it.getAttribute("android:name") == permissionName }

    matchingPermissions.forEach { manifest.removeChild(it) }

    if (!upsertVrLinkUnmanagedFullSpace(document, app)) {
        throw PatchException("Steam Link 5002322 VRLink activity was not found")
    }

    val metadataName = "com.valvesoftware.steamlink.GXR_RESOLUTION_MODE"
    val existingMetadata = app.getElementsByTagName("meta-data").let { nodes ->
        (0 until nodes.length)
            .mapNotNull { nodes.item(it) as? Element }
            .firstOrNull { it.getAttribute("android:name") == metadataName }
    }
    val existingMode = existingMetadata?.getAttribute("android:value").orEmpty()
    if (projectionModesConflict(existingMode, requestedMode)) {
        throw PatchException(
            "Experimental XR projection modes are mutually exclusive: " +
                "$requestedMode conflicts with $existingMode",
        )
    }
    val metadata = existingMetadata ?: document.createElement("meta-data").also(app::appendChild)
    metadata.setAttribute("android:name", metadataName)
    metadata.setAttribute("android:value", requestedMode)
}

private val singleProjectionReconstructionLayerPatch = rawResourcePatch {
    execute {
        val libDir = get("lib/arm64-v8a/libvrlink_scene.so").parentFile!!
        val layerDir = get(
            "assets/openxr/1/api_layers/implicit.d/$SINGLE_PROJECTION_MANIFEST",
        ).parentFile!!

        installProjectionModeResources(
            libDir,
            layerDir,
            activeProjectionModes.first { it.mode == SINGLE_PROJECTION_MODE },
        )
    }
}

private val twoProjectionDropBaseLayerPatch = rawResourcePatch {
    execute {
        val libDir = get("lib/arm64-v8a/libvrlink_scene.so").parentFile!!
        val layerDir = get(
            "assets/openxr/1/api_layers/implicit.d/$TWO_PROJECTION_MANIFEST",
        ).parentFile!!

        installProjectionModeResources(
            libDir,
            layerDir,
            activeProjectionModes.first { it.mode == TWO_PROJECTION_MODE },
        )
    }
}

private val threeProjectionSamplerProxyLayerPatch = rawResourcePatch {
    execute {
        val libDir = get("lib/arm64-v8a/libvrlink_scene.so").parentFile!!
        val layerDir = get(
            "assets/openxr/1/api_layers/implicit.d/$THREE_PROJECTION_PROXY_MANIFEST",
        ).parentFile!!

        installProjectionModeResources(
            libDir,
            layerDir,
            activeProjectionModes.first { it.mode == THREE_PROJECTION_PROXY_MODE },
        )
    }
}

@Suppress("unused")
val xrSingleProjectionReconstructionPatch = resourcePatch(
    name = "Experimental Single Projection Reconstruction",
    description = "5002322-only permission-free experiment. Reconstructs Steam Link's opaque full-FOV underside and alpha-foveated inset into one stereo projection before submission.",
    default = false,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_5002322_EXPERIMENTAL.toTypedArray())
    dependsOn(
        xrLauncherBootstrapPatch,
        xrPermissionSettingsBootstrapPatch,
        singleProjectionReconstructionLayerPatch,
    )

    finalize {
        document("AndroidManifest.xml").use { document ->
            configurePermissionFreeProjectionMode(document, SINGLE_PROJECTION_MODE)
        }
    }
}

@Suppress("unused")
val xrTwoProjectionDropBasePatch = resourcePatch(
    name = "Experimental Two Projection Drop Base",
    description = "5002322-only permission-free discriminator. Drops only the redundant first opaque full-FOV projection while forwarding Steam Link's original underside and alpha-foveated projections unchanged.",
    default = false,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_5002322_EXPERIMENTAL.toTypedArray())
    dependsOn(
        xrLauncherBootstrapPatch,
        xrPermissionSettingsBootstrapPatch,
        twoProjectionDropBaseLayerPatch,
    )

    finalize {
        document("AndroidManifest.xml").use { document ->
            configurePermissionFreeProjectionMode(document, TWO_PROJECTION_MODE)
        }
    }
}

@Suppress("unused")
val xrThreeProjectionSamplerProxyPatch = resourcePatch(
    name = "Experimental Three Projection Sampler Proxy",
    description = "5002322-only permission-free discriminator. Preserves Steam Link's three projection layers while resolving their source images into controlled sampleCount-1 proxy swapchains.",
    default = false,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_5002322_EXPERIMENTAL.toTypedArray())
    dependsOn(
        xrLauncherBootstrapPatch,
        xrPermissionSettingsBootstrapPatch,
        threeProjectionSamplerProxyLayerPatch,
    )

    finalize {
        document("AndroidManifest.xml").use { document ->
            configurePermissionFreeProjectionMode(document, THREE_PROJECTION_PROXY_MODE)
        }
    }
}
