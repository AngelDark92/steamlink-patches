package app.template.patches.steamlink.androidxr

import com.android.tools.smali.dexlib2.Opcodes
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import javax.xml.parsers.DocumentBuilderFactory

class NativeXrTrackingConfigTest {
    @Test
    fun `native GXRP host requires usable unicast IPv4`() {
        assertTrue(isUsableNativeGxrpHostIpv4("192.168.1.27"))
        assertTrue(isUsableNativeGxrpHostIpv4("10.0.0.4"))
        listOf(null, "", "0.1.2.3", "127.0.0.1", "224.0.0.1", "240.0.0.1", "255.255.255.255")
            .forEach { assertFalse(isUsableNativeGxrpHostIpv4(it), "$it must be rejected") }
    }

    @Test
    fun `native GXRP metadata uses fixed host-matching ports`() {
        val metadata = nativeGxrpApplicationMetadata(
            hostIpv4 = "192.168.1.27",
            pairingTokenHex = "ab".repeat(32),
            versionCode = "5002322",
        )

        assertEquals("29981", metadata["gxr.telemetry.controlPort"])
        assertEquals("29982", metadata["gxr.telemetry.trackingPort"])
        assertEquals("192.168.1.27", metadata["gxr.telemetry.host"])
        assertEquals("5002322", metadata["gxr.build.versionCode"])
    }

    @Test
    fun `native GXRP metadata is direct idempotent application state`() {
        val document = DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument()
        val manifest = document.createElement("manifest").also(document::appendChild)
        val app = document.createElement("application").also(manifest::appendChild)

        upsertDirectApplicationMetadata(document, app, "gxr.telemetry.host", "192.168.1.27")
        upsertDirectApplicationMetadata(document, app, "gxr.telemetry.host", "192.168.1.30")

        val matches = (0 until app.getElementsByTagName("meta-data").length)
            .map { app.getElementsByTagName("meta-data").item(it) as org.w3c.dom.Element }
            .filter { it.getAttribute("android:name") == "gxr.telemetry.host" }
        assertEquals(1, matches.size)
        assertEquals("192.168.1.30", matches.single().getAttribute("android:value"))
    }

    @Test
    fun `native helper extension contains no SDL or controller class fragments`() {
        assertEquals(
            setOf(
                "Lcom/valvesoftware/steamlink/GalaxyXRPermissionActivity;",
                "Lcom/valvesoftware/steamlink/GxrOverlayBridge;",
                "Lcom/valvesoftware/steamlink/GxrResolutionProbe;",
            ),
            extensionClassTypes("/extensions/minimal-extension.mpe"),
        )
    }

    @Test
    fun `legacy extension also contains helpers only so transitive merge is inert on native builds`() {
        assertEquals(
            setOf("Lorg/libsdl/app/GxrSdlBridge;"),
            extensionClassTypes("/extensions/extension.mpe"),
        )
    }

    @Test
    fun `native stock controller config carries hand routing absent from legacy baseline`() {
        // Minimal fixture shared by byte-identical 5002318/5002322 controller configs. Keep markers
        // together: the runtime needs the extension, hand profile/type, and both exported poses.
        val stock =
            """
            {
              "requestedExtensions": ["XR_EXT_hand_interaction"],
              "staticProps": {
                "/interaction_profiles/ext/hand_interaction_ext": {
                  "sControllerType": "svl_hand_interaction_augmented"
                }
              },
              "additionalPoses": ["/input/grip/pose", "/input/aim/pose"]
            }
            """.trimIndent()
        val legacy = requireNotNull(
            javaClass.getResource("/steamlink/androidxr/controller_config.json"),
        ).readText()
        val requiredNativeRouting = listOf(
            "XR_EXT_hand_interaction",
            "/interaction_profiles/ext/hand_interaction_ext",
            "svl_hand_interaction_augmented",
            "/input/aim/pose",
        )

        requiredNativeRouting.forEach { marker ->
            assertTrue(stock.contains(marker), "native stock is missing $marker")
            assertFalse(legacy.contains(marker), "legacy baseline unexpectedly contains $marker")
        }
        assertTrue(stock.contains("/input/grip/pose"))
    }

    private fun extensionClassTypes(resource: String): Set<String> =
        requireNotNull(javaClass.getResourceAsStream(resource)).use { input ->
            DexBackedDexFile.fromInputStream(Opcodes.getDefault(), input)
                .classes
                .map { it.type }
                .toSet()
        }
}
