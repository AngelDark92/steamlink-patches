package app.template.patches.shared

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class ConstantsTest {
    @Test
    fun `all 2_0_22 builds are stable and other versions are experimental`() {
        val compatibilities = listOf(
            Constants.COMPATIBILITY_STEAM_LINK,
            Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL,
            Constants.COMPATIBILITY_STEAM_LINK_HMD_ONLY,
        )

        compatibilities.forEach { compatibility ->
            val supported = compatibility.targets.single { it.version == "2.0.22" }
            assertFalse(supported.isExperimental)
            assertNull(supported.versionCodes)

            val fallback = compatibility.targets.single { it.version == null }
            assertTrue(fallback.isExperimental)
            assertNull(fallback.versionCodes)
        }
    }
}
