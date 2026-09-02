package app.template.patches.steamlink

import app.morphe.patcher.patch.Patch
import app.template.patches.steamlink.androidxr.appearOnTopPatch
import app.template.patches.steamlink.androidxr.androidXrUiExtensionPatch
import app.template.patches.steamlink.androidxr.controllerVelocityPatch
import app.template.patches.steamlink.androidxr.gxrFacebridgePatch
import app.template.patches.steamlink.androidxr.unrestrictedBatteryUsagePatch
import app.template.patches.steamlink.androidxr.xrCoreRuntimePatch
import app.template.patches.steamlink.androidxr.xrDeviceConfigBaselinePatch
import app.template.patches.steamlink.androidxr.xrDirectInputFixPatch
import app.template.patches.steamlink.androidxr.xrGalaxyXrHighResolutionPatch
import app.template.patches.steamlink.androidxr.xrInputRoutingConfigPatch
import app.template.patches.steamlink.androidxr.xrLauncherBootstrapPatch
import app.template.patches.steamlink.androidxr.xrManifestCapabilityPackPatch
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
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class PatchCompatibilityMatrixTest {
    @Test
    fun individual_patches_are_not_globally_recommended() {
        allIndividualPatches.forEach { patch ->
            assertFalse(patch.default, patch.name)
        }
        recommendedBundles.forEach { patch ->
            assertTrue(patch.default, patch.name)
        }
        assertEquals("Appear on top (legacy)", appearOnTopPatch.name)
    }

    @Test
    fun exact_builds_select_one_recommendation_bundle() {
        assertEquals(
            listOf(galaxyXrRecommended5001712Patch),
            recommendedFor("2.0.20", 5001712),
        )
        assertEquals(
            listOf(galaxyXrRecommended5002322Patch),
            recommendedFor("2.0.22", 5002322),
        )
        assertEquals(
            listOf(galaxyXrRecommended5002318Patch),
            recommendedFor("2.0.22", 5002318),
        )
        listOf(
            "2.0.20" to 5001740,
            "2.0.22" to 5002172,
            "2.0.22" to 5002206,
            "2.0.22" to 5002244,
        ).forEach { (version, versionCode) ->
            assertEquals(
                listOf(galaxyXrLegacyFoundationPatch),
                recommendedFor(version, versionCode),
                "$version/$versionCode",
            )
        }
        listOf(5002296, 5002313).forEach { versionCode ->
            assertTrue(recommendedFor("2.0.22", versionCode).isEmpty(), "$versionCode")
        }
        assertTrue(recommendedFor("2.0.22", 5002243).isEmpty())
        assertTrue(recommendedFor("2.0.20", 5002322).isEmpty())
    }

    @Test
    fun latest_bundle_contains_the_final_seven_patches() {
        assertEquals(
            setOf(
                xrGalaxyXrHighResolutionPatch,
                gxrFacebridgePatch,
                microphoneInputPresetPatch,
                unrestrictedBatteryUsagePatch,
                videoDitherPatch,
                hmdOnlyPatch,
                oledCalibrationPatch,
            ),
            galaxyXrRecommended5002322Patch.dependencies.toSet(),
        )
        assertTrue(oledCalibrationPatch.supports("2.0.22", 5002322))
        assertTrue(oledCalibrationPatch in videoDitherPatch.dependencyClosure())
    }

    @Test
    fun previous_native_bundle_preserves_the_verified_safe_set() {
        assertEquals(
            setOf(
                xrGalaxyXrHighResolutionPatch,
                gxrFacebridgePatch,
                microphoneInputPresetPatch,
                unrestrictedBatteryUsagePatch,
                videoDitherPatch,
                hmdOnlyPatch,
                oledCalibrationPatch,
                deviceIdentityPatch,
            ),
            galaxyXrRecommended5002318Patch.dependencies.toSet(),
        )
        (legacyFoundationPatches - deviceIdentityPatch).forEach { patch ->
            assertFalse(patch.supports("2.0.22", 5002318), patch.name)
        }
        assertTrue(appearOnTopPatch.supports("2.0.22", 5002318))
        assertFalse(appearOnTopPatch.supports("2.0.22", 5002322))
    }

    @Test
    fun legacy_bundle_contains_only_the_conversion_foundation() {
        assertEquals(
            legacyFoundationPatches.toSet(),
            galaxyXrLegacyFoundationPatch.dependencies.toSet(),
        )
        assertFalse(forceHmdInitializationGatesPatch.default)
        assertFalse(forceLobbyPermissionStateGatePatch.default)
        assertFalse(forceStreamXrGatesPatch.default)
        assertFalse(controllerVelocityPatch.default)
        assertFalse(changePackageNamePatch.default)
        assertFalse(appearOnTopPatch.default)
    }

    @Test
    fun build_5001712_bundle_contains_the_conversion_and_final_feature_set() {
        assertEquals(
            (legacyFoundationPatches + listOf(
                xrGalaxyXrHighResolutionPatch,
                gxrFacebridgePatch,
                microphoneInputPresetPatch,
                unrestrictedBatteryUsagePatch,
                videoDitherPatch,
                hmdOnlyPatch,
                oledCalibrationPatch,
            )).toSet(),
            galaxyXrRecommended5001712Patch.dependencies.toSet(),
        )
        assertFalse(appearOnTopPatch in galaxyXrRecommended5001712Patch.dependencies)
        assertFalse(changePackageNamePatch in galaxyXrRecommended5001712Patch.dependencies)
    }

    @Test
    fun previous_verified_compatibility_is_preserved() {
        listOf(5002244, 5002296, 5002313, 5002318, 5002322).forEach { versionCode ->
            assertTrue(xrGalaxyXrHighResolutionPatch.supports("2.0.22", versionCode), "$versionCode")
        }
        assertTrue(xrGalaxyXrHighResolutionPatch.supports("2.0.20", 5001712))
        assertFalse(xrGalaxyXrHighResolutionPatch.supports("2.0.20", 5001740))
        assertFalse(xrGalaxyXrHighResolutionPatch.supports("2.0.22", 5001712))
        assertFalse(xrGalaxyXrHighResolutionPatch.supports("2.0.22", 5002243))

        val legacyCommon = listOf(
            deviceIdentityPatch,
            microphoneInputPresetPatch,
            oledCalibrationPatch,
            gxrFacebridgePatch,
            unrestrictedBatteryUsagePatch,
            videoDitherPatch,
        )
        listOf(5001712, 5001740).forEach { versionCode ->
            legacyCommon.forEach { patch ->
                assertTrue(patch.supports("2.0.20", versionCode), "${patch.name} on $versionCode")
            }
        }
        legacyCommon.forEach { patch ->
            assertTrue(patch.supports("2.0.22", 5002313), patch.name)
        }
        assertTrue(hmdOnlyPatch.supports("2.0.20", 5001712))
        assertTrue(hmdOnlyPatch.supports("2.0.20", 5001740))
        listOf(5002244, 5002313, 5002318, 5002322).forEach { versionCode ->
            assertTrue(hmdOnlyPatch.supports("2.0.22", versionCode), "$versionCode")
        }
    }

    @Test
    fun recursive_foundation_dependencies_remain_wired_and_guarded() {
        val identityClosure = deviceIdentityPatch.dependencyClosure()
        assertTrue(xrDeviceConfigBaselinePatch in identityClosure)
        assertTrue(xrCoreRuntimePatch in identityClosure)
        assertTrue(androidXrUiExtensionPatch in identityClosure)
        assertTrue(xrDirectInputFixPatch in identityClosure)

        val routingClosure = xrInputRoutingConfigPatch.dependencyClosure()
        assertTrue(xrLauncherBootstrapPatch in routingClosure)
        assertTrue(xrManifestCapabilityPackPatch in routingClosure)
        assertTrue(xrCoreRuntimePatch in routingClosure)

        assertTrue(xrLauncherBootstrapPatch in xrGalaxyXrHighResolutionPatch.dependencyClosure())
    }

    private fun recommendedFor(version: String, versionCode: Int): List<Patch<*>> =
        recommendedBundles.filter { patch ->
            patch.default && patch.supports(version, versionCode)
        }

    private fun Patch<*>.supports(version: String, versionCode: Int): Boolean =
        compatibility.orEmpty().any { compatibility ->
            compatibility.targets.any { target ->
                target.version == version && target.versionCodes?.values?.contains(versionCode) == true
            }
        }

    private fun Patch<*>.dependencyClosure(visited: MutableSet<Patch<*>> = mutableSetOf()): Set<Patch<*>> {
        dependencies.forEach { dependency ->
            if (visited.add(dependency)) dependency.dependencyClosure(visited)
        }
        return visited
    }

    private companion object {
        val recommendedBundles = listOf(
            galaxyXrRecommended5001712Patch,
            galaxyXrRecommended5002322Patch,
            galaxyXrRecommended5002318Patch,
            galaxyXrLegacyFoundationPatch,
        )

        val legacyFoundationPatches = listOf(
            androidXrNativePermissionNamesPatch,
            deviceIdentityPatch,
            xrCoreRuntimePatch,
            xrDeviceConfigBaselinePatch,
            xrManifestCapabilityPackPatch,
            xrLauncherBootstrapPatch,
            xrInputRoutingConfigPatch,
        )

        val allIndividualPatches = listOf(
            androidXrNativePermissionNamesPatch,
            appearOnTopPatch,
            changePackageNamePatch,
            controllerVelocityPatch,
            deviceIdentityPatch,
            forceHmdInitializationGatesPatch,
            forceLobbyPermissionStateGatePatch,
            forceStreamXrGatesPatch,
            gxrFacebridgePatch,
            xrGalaxyXrHighResolutionPatch,
            microphoneInputPresetPatch,
            oledCalibrationPatch,
            unrestrictedBatteryUsagePatch,
            videoDitherPatch,
            hmdOnlyPatch,
            xrCoreRuntimePatch,
            xrDeviceConfigBaselinePatch,
            xrManifestCapabilityPackPatch,
            xrLauncherBootstrapPatch,
            xrInputRoutingConfigPatch,
        )
    }
}
