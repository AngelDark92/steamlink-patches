package app.template.patches.steamlink

import app.morphe.patcher.patch.Patch
import app.template.patches.steamlink.androidxr.appearOnTopPatch
import app.template.patches.steamlink.androidxr.androidXrUiExtensionPatch
import app.template.patches.steamlink.androidxr.controllerVelocityPatch
import app.template.patches.steamlink.androidxr.gxrFacebridgePatch
import app.template.patches.steamlink.androidxr.unrestrictedBatteryUsagePatch
import app.template.patches.steamlink.androidxr.xrDirectInputFixPatch
import app.template.patches.steamlink.androidxr.xrCoreRuntimePatch
import app.template.patches.steamlink.androidxr.xrDeviceConfigBaselinePatch
import app.template.patches.steamlink.androidxr.xrInputRoutingConfigPatch
import app.template.patches.steamlink.androidxr.xrLauncherBootstrapPatch
import app.template.patches.steamlink.androidxr.xrManifestCapabilityPackPatch
import app.template.patches.steamlink.androidxr.xrProjectionSettingsQualityPatch
import app.template.patches.steamlink.androidxr.xrProjectionSettingsStrippedPatch
import app.template.patches.steamlink.androidxr.xrProjectionTraceControlPatch
import app.template.patches.steamlink.binary.androidXrNativePermissionNamesPatch
import app.template.patches.steamlink.binary.forceHmdInitializationGatesPatch
import app.template.patches.steamlink.binary.forceLobbyPermissionStateGatePatch
import app.template.patches.steamlink.binary.forceStreamXrGatesPatch
import app.template.patches.steamlink.binary.hmdOnlyPatch
import app.template.patches.steamlink.binary.microphoneInputPresetPatch
import app.template.patches.steamlink.binary.oledCalibrationPatch
import app.template.patches.steamlink.binary.videoDitherPatch
import app.template.patches.steamlink.identity.changePackageNamePatch
import app.template.patches.steamlink.identity.deviceIdentityPatch
import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class PatchCompatibilityMatrixTest {
    @Test
    fun `5002318 exposes only the supported stable and experimental patches`() {
        allowed5002318.forEach { patch ->
            assertTrue(patch.supports(5002318), patch.name)
        }
        excluded5002318.forEach { patch ->
            assertFalse(patch.supports(5002318), patch.name)
        }
        legacyRecommended.forEach { patch ->
            assertTrue(patch.default, "${patch.name} must remain recommended on its older compatible builds")
        }
    }

    @Test
    fun `previous verified build compatibility is preserved`() {
        (allowed5002318 + excluded5002318).forEach { patch ->
            assertTrue(patch.supports(5002313), patch.name)
        }
    }

    @Test
    fun `legacy automatic dependency behavior remains wired for older builds`() {
        val identityClosure = deviceIdentityPatch.dependencyClosure()
        assertTrue(xrDeviceConfigBaselinePatch in identityClosure)
        assertTrue(xrCoreRuntimePatch in identityClosure)
        assertTrue(androidXrUiExtensionPatch in identityClosure)
        assertTrue(xrDirectInputFixPatch in identityClosure)

        listOf(
            appearOnTopPatch,
            unrestrictedBatteryUsagePatch,
            xrProjectionTraceControlPatch,
            xrProjectionSettingsQualityPatch,
            xrProjectionSettingsStrippedPatch,
        ).forEach { patch ->
            assertTrue(xrLauncherBootstrapPatch in patch.dependencyClosure(), patch.name)
        }
    }

    private fun Patch<*>.supports(versionCode: Int): Boolean =
        compatibility.orEmpty().any { compatibility ->
            compatibility.targets.any { target ->
                target.version == "2.0.22" && target.versionCodes?.values?.contains(versionCode) == true
            }
        }

    private fun Patch<*>.dependencyClosure(visited: MutableSet<Patch<*>> = mutableSetOf()): Set<Patch<*>> {
        dependencies.forEach { dependency ->
            if (visited.add(dependency)) dependency.dependencyClosure(visited)
        }
        return visited
    }

    private companion object {
        val allowed5002318 = listOf(
            deviceIdentityPatch,
            oledCalibrationPatch,
            appearOnTopPatch,
            gxrFacebridgePatch,
            hmdOnlyPatch,
            unrestrictedBatteryUsagePatch,
            videoDitherPatch,
            xrProjectionTraceControlPatch,
            xrProjectionSettingsQualityPatch,
            xrProjectionSettingsStrippedPatch,
        )

        val excluded5002318 = listOf(
            changePackageNamePatch,
            microphoneInputPresetPatch,
            androidXrNativePermissionNamesPatch,
            forceHmdInitializationGatesPatch,
            forceLobbyPermissionStateGatePatch,
            forceStreamXrGatesPatch,
            controllerVelocityPatch,
            xrCoreRuntimePatch,
            xrDeviceConfigBaselinePatch,
            xrManifestCapabilityPackPatch,
            xrLauncherBootstrapPatch,
            xrInputRoutingConfigPatch,
        )

        val legacyRecommended = excluded5002318 - listOf(
            changePackageNamePatch,
            controllerVelocityPatch,
        )
    }
}
