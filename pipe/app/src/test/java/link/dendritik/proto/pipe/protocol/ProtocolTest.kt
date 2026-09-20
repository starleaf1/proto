package link.dendritik.proto.pipe.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

/**
 * The half of the contract that lives in two files at once.
 *
 * A watchface's UUID is compiled into its binary from its own `package.json`, and the
 * companion addresses it from [Protocol.APP_UUIDS]. Nothing links the two, and a
 * mismatch is silent in the worst way: every send is NACKed by the firmware, the
 * companion logs a clean success, and the face simply never receives anything. This
 * reads both manifests and asserts they agree.
 *
 * Skipped rather than failed when the watchface directories are not reachable, so the
 * suite still runs from a checkout of `pipe/` alone.
 */
class ProtocolTest {

    private val faces = listOf("watchface-digital", "watchface-analog")

    /** Gradle runs unit tests with the module directory as the working directory. */
    private fun repoRoot(): File? {
        var dir: File? = File(".").absoluteFile
        while (dir != null) {
            if (faces.all { File(dir, "$it/package.json").isFile }) return dir
            dir = dir.parentFile
        }
        return null
    }

    private fun field(json: String, name: String): String =
        Regex("\"$name\"\\s*:\\s*\"([^\"]+)\"").find(json)?.groupValues?.get(1)
            ?: error("no \"$name\" in package.json")

    @Test
    fun `every watchface uuid is addressed`() {
        val root = repoRoot()
        assumeTrue("watchface sources not reachable", root != null)

        val declared = faces.map { face ->
            face to field(File(root, "$face/package.json").readText(), "uuid").lowercase()
        }
        val addressed = Protocol.APP_UUIDS.map { it.toString().lowercase() }

        for ((face, uuid) in declared) {
            assertTrue(
                "$face declares $uuid, which Protocol.APP_UUIDS does not address",
                uuid in addressed,
            )
        }
        assertEquals(
            "Protocol.APP_UUIDS addresses a UUID no watchface declares",
            declared.size,
            addressed.size,
        )
    }

    @Test
    fun `message key ids are positional from 10000`() {
        val root = repoRoot()
        assumeTrue("watchface sources not reachable", root != null)

        // Ids are assigned by position in `messageKeys`, so a reordered or inserted
        // name silently renumbers every key after it — and Android only ever sees the
        // numbers. Both faces must declare the same array in the same order.
        val expected = listOf(
            "Heartbeat" to Protocol.KEY_HEARTBEAT,
            "CalEvents" to Protocol.KEY_CAL_EVENTS,
            "CalFlags" to Protocol.KEY_CAL_FLAGS,
            "NavManeuver" to Protocol.KEY_NAV_MANEUVER,
            "NavDistance" to Protocol.KEY_NAV_DISTANCE,
            "NavUnit" to Protocol.KEY_NAV_UNIT,
            "PhoneBattery" to Protocol.KEY_PHONE_BATTERY,
        )
        for (face in faces) {
            val json = File(root, "$face/package.json").readText()
            val block = Regex("\"messageKeys\"\\s*:\\s*\\[(.*?)]", RegexOption.DOT_MATCHES_ALL)
                .find(json)?.groupValues?.get(1) ?: error("no messageKeys in $face")
            val names = Regex("\"([^\"]+)\"").findAll(block).map { it.groupValues[1] }.toList()
            assertEquals("$face declares the wrong number of keys", expected.size, names.size)
            expected.forEachIndexed { i, (name, id) ->
                assertEquals("$face key $i", name, names[i])
                assertEquals("$name id", 10000 + i, id)
            }
        }
    }
}
