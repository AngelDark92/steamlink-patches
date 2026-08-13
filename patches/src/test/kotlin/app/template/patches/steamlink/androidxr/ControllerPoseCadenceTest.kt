package app.template.patches.steamlink.androidxr

import kotlin.test.Test
import kotlin.test.assertContentEquals

class ControllerPoseCadenceTest {
    @Test
    fun `all cadence modes leave an unknown native layout untouched`() {
        val input = ByteArray(128) { it.toByte() }

        listOf("stock-4x", "half-2x", "display-1x").forEach { mode ->
            assertContentEquals(input, patchControllerPoseCadence(input, mode))
        }
    }
}
