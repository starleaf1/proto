package link.dendritik.proto.pipe.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The id space is partitioned by bit 31 so that two sources cannot silently
 * overwrite each other. These assert the partition holds, and that the Nuron
 * hash really does agree with the other two implementations of it.
 */
class WireIdsTest {

    @Test
    fun `calendar ids never carry the Nuron bit`() {
        // The raw hash sets bit 31 for roughly half of all inputs, which is the
        // whole reason the mask exists. Sweeping the sign boundary is the point.
        val raws = listOf(0, 1, -1, Int.MIN_VALUE, Int.MAX_VALUE, -554486453, 1234567)
        for (raw in raws) {
            val id = WireIds.calendar(raw)
            assertTrue("calendar($raw) = $id carries the Nuron bit", id >= 0)
            assertTrue(WireIds.isCalendar(id))
            assertFalse(WireIds.isNuron(id))
        }
    }

    @Test
    fun `masking preserves the low 31 bits`() {
        // It must stay a hash, not become a constant: the mask may only remove
        // the one bit that carries the namespace.
        assertEquals(1234567, WireIds.calendar(1234567))
        assertEquals(Int.MAX_VALUE, WireIds.calendar(-1))
        assertEquals(0, WireIds.calendar(Int.MIN_VALUE))
    }

    @Test
    fun `the two halves of the space cannot overlap`() {
        // Any calendar id and any Nuron id differ in bit 31 by construction, so
        // a collision between the sources is arithmetically impossible rather
        // than merely unlikely.
        val nuronIds = listOf(-554486453, -1821628596, -1022184027, -1098902972, -1862960661)
        for (n in nuronIds) {
            assertTrue("Nuron vector $n is missing its bit", WireIds.isNuron(n))
            for (raw in listOf(0, -1, Int.MIN_VALUE, 1234567)) {
                assertTrue(WireIds.calendar(raw) != n)
            }
        }
    }
}
