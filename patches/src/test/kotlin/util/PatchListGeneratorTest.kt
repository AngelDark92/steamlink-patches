package util

import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK
import app.template.patches.shared.Constants.COMPATIBILITY_STEAM_LINK_EXPERIMENTAL
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class PatchListGeneratorTest {
    @Test
    fun `stable compatibility separates supported and warning-only versions`() {
        assertEquals(
            listOf("2.0.22"),
            targetsForReleaseChannel(COMPATIBILITY_STEAM_LINK, ReleaseChannel.STABLE).map { it.version },
        )
        assertEquals(
            listOf(null),
            targetsForReleaseChannel(COMPATIBILITY_STEAM_LINK, ReleaseChannel.EXPERIMENTAL).map { it.version },
        )
    }

    @Test
    fun `projection experiments stay out of stable release channel`() {
        assertTrue(
            targetsForReleaseChannel(COMPATIBILITY_STEAM_LINK_EXPERIMENTAL, ReleaseChannel.STABLE).isEmpty(),
        )
        assertEquals(
            listOf("2.0.22", null),
            targetsForReleaseChannel(
                COMPATIBILITY_STEAM_LINK_EXPERIMENTAL,
                ReleaseChannel.EXPERIMENTAL,
            ).map { it.version },
        )
    }
}
