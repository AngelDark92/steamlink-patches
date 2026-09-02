package app.template.patches.steamlink

import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_5001712
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_5002318
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_5002322
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_LEGACY_RECOMMENDED
import app.template.patches.steamlink.androidxr.gxrFacebridgePatch
import app.template.patches.steamlink.androidxr.unrestrictedBatteryUsagePatch
import app.template.patches.steamlink.androidxr.xrCoreRuntimePatch
import app.template.patches.steamlink.androidxr.xrDeviceConfigBaselinePatch
import app.template.patches.steamlink.androidxr.xrGalaxyXrHighResolutionPatch
import app.template.patches.steamlink.androidxr.xrInputRoutingConfigPatch
import app.template.patches.steamlink.androidxr.xrLauncherBootstrapPatch
import app.template.patches.steamlink.androidxr.xrManifestCapabilityPackPatch
import app.template.patches.steamlink.binary.androidXrNativePermissionNamesPatch
import app.template.patches.steamlink.binary.hmdOnlyPatch
import app.template.patches.steamlink.binary.microphoneInputPresetPatch
import app.template.patches.steamlink.binary.oledCalibrationPatch
import app.template.patches.steamlink.binary.videoDitherPatch
import app.template.patches.steamlink.identity.deviceIdentityPatch

// Patch.default belongs to the patch object rather than to an AppTarget. These exact-build
// bundles keep every individual patch available in Expert mode while giving Simple mode a
// deterministic recommendation set for the selected Steam Link version and build code.

@Suppress("unused")
val galaxyXrRecommended5001712Patch = rawResourcePatch(
    name = "Galaxy XR recommended set (2.0.20/5001712)",
    description = "Applies the exact-build Galaxy XR conversion and permission-free high-resolution set for Steam Link 2.0.20 build 5001712, including the Final balanced tested OLED profile.",
    default = true,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_5001712.toTypedArray())
    dependsOn(
        androidXrNativePermissionNamesPatch,
        deviceIdentityPatch,
        xrCoreRuntimePatch,
        xrDeviceConfigBaselinePatch,
        xrManifestCapabilityPackPatch,
        xrLauncherBootstrapPatch,
        xrInputRoutingConfigPatch,
        xrGalaxyXrHighResolutionPatch,
        gxrFacebridgePatch,
        microphoneInputPresetPatch,
        unrestrictedBatteryUsagePatch,
        videoDitherPatch,
        hmdOnlyPatch,
        oledCalibrationPatch,
    )
}

@Suppress("unused")
val galaxyXrRecommended5002322Patch = rawResourcePatch(
    name = "Galaxy XR recommended set (2.0.22/5002322)",
    description = "Applies the validated permission-free Galaxy XR patch set for exact Steam Link 2.0.22 build 5002322, including the Final balanced tested OLED profile.",
    default = true,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_5002322.toTypedArray())
    dependsOn(
        xrGalaxyXrHighResolutionPatch,
        gxrFacebridgePatch,
        microphoneInputPresetPatch,
        unrestrictedBatteryUsagePatch,
        videoDitherPatch,
        hmdOnlyPatch,
        oledCalibrationPatch,
    )
}

@Suppress("unused")
val galaxyXrRecommended5002318Patch = rawResourcePatch(
    name = "Galaxy XR recommended set (2.0.22/5002318)",
    description = "Applies the existing native-Android-XR-safe Galaxy XR patch set for exact Steam Link 2.0.22 build 5002318.",
    default = true,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_5002318.toTypedArray())
    dependsOn(
        xrGalaxyXrHighResolutionPatch,
        gxrFacebridgePatch,
        microphoneInputPresetPatch,
        unrestrictedBatteryUsagePatch,
        videoDitherPatch,
        hmdOnlyPatch,
        oledCalibrationPatch,
        deviceIdentityPatch,
    )
}

@Suppress("unused")
val galaxyXrLegacyFoundationPatch = rawResourcePatch(
    name = "Galaxy XR legacy foundation (through 2.0.22/5002244)",
    description = "Applies the Galaxy XR conversion foundation to the exact supported Steam Link builds through 2.0.22 build 5002244. Diagnostic force gates and controller tuning remain optional.",
    default = true,
) {
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_LEGACY_RECOMMENDED.toTypedArray())
    dependsOn(
        androidXrNativePermissionNamesPatch,
        deviceIdentityPatch,
        xrCoreRuntimePatch,
        xrDeviceConfigBaselinePatch,
        xrManifestCapabilityPackPatch,
        xrLauncherBootstrapPatch,
        xrInputRoutingConfigPatch,
    )
}
