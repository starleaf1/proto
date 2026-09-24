package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.calendar.EventFacts
import link.dendritik.proto.pipe.protocol.EventKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The merge is the only place the two sources meet, and `PebbleSender` diffs its
 * output against what it believes the watch holds. So the property under test is
 * not "the right entries" so much as "the same answer every time" — an unstable
 * order would emit removes and adds for a table that did not change.
 */
class MergePolicyTest {

    private fun cal(id: Int, start: Int, dur: Int = 30) =
        EventFacts(id and 0x7FFF_FFFF, start, dur, EventKind.APPOINTMENT)

    private fun nur(id: Int, start: Int) =
        EventFacts(id or Int.MIN_VALUE, start, 0, EventKind.TASK)

    @Test
    fun `a full table fits in one message`() {
        // Every calendar send is a flush of the whole table, so this is what keeps each
        // one to a single AppMessage and the multi-message MORE path untravelled.
        assertTrue(MergePolicy.MAX_MERGED <= EventBlob.MAX_RECORDS)
    }

    @Test
    fun `both sources are present in the result`() {
        val out = MergePolicy.merge(listOf(cal(1, 100)), listOf(nur(1, 200)))
        assertEquals(2, out.size)
    }

    @Test
    fun `the result is sorted soonest first`() {
        val out = MergePolicy.merge(
            listOf(cal(1, 300), cal(2, 100)),
            listOf(nur(1, 200), nur(2, 50)),
        )
        assertEquals(listOf(50, 100, 200, 300), out.map { it.startUtcS })
    }

    @Test
    fun `the result does not depend on input order`() {
        val calendar = listOf(cal(1, 300), cal(2, 100), cal(3, 700))
        val nuron = listOf(nur(1, 200), nur(2, 50), nur(3, 400))
        assertEquals(
            MergePolicy.merge(calendar, nuron),
            MergePolicy.merge(calendar.reversed(), nuron.reversed()),
        )
    }

    @Test
    fun `entries at the same instant are ordered deterministically`() {
        // The tie-break is what stops two entries at 09:00 from swapping places
        // between scans. kind first, then id -- and id is unique, so there is
        // never an actual tie left to resolve by luck.
        val a = MergePolicy.merge(listOf(cal(1, 100, dur = 30)), listOf(nur(1, 100)))
        val b = MergePolicy.merge(listOf(cal(1, 100, dur = 30)), listOf(nur(1, 100)))
        assertEquals(a, b)
        assertEquals(EventKind.APPOINTMENT, a[0].kind)
        assertEquals(EventKind.TASK, a[1].kind)
    }

    @Test
    fun `the result is capped at MAX_MERGED so a flush stays one message`() {
        val calendar = (1..20).map { cal(it, it * 100) }
        val nuron = (1..20).map { nur(it, it * 100 + 50) }
        val out = MergePolicy.merge(calendar, nuron)
        assertEquals(MergePolicy.MAX_MERGED, out.size)
        // 6-byte header + 12 bytes per record must fit the budget protocol.md
        // works out against the 512-byte inbox.
        assertTrue(6 + out.size * 12 <= 294)
    }

    @Test
    fun `truncation keeps the soonest, not one source before the other`() {
        // "Calendar first, then Nuron" would make membership depend on the
        // relative sizes of the two sources, so adding a meeting could evict a
        // quest that starts sooner.
        val calendar = (1..24).map { cal(it, 10_000 + it) }
        val nuron = listOf(nur(99, 1))
        val out = MergePolicy.merge(calendar, nuron)
        assertEquals(MergePolicy.MAX_MERGED, out.size)
        assertEquals(1, out.first().startUtcS)
        assertTrue(out.any { it.id < 0 })
    }

    @Test
    fun `an entry displaced past the cap simply leaves the list`() {
        val calendar = (1..24).map { cal(it, 1000 + it) }
        val before = MergePolicy.merge(calendar, emptyList())
        val after = MergePolicy.merge(calendar, listOf(nur(99, 1)))
        val dropped = before.map { it.id }.toSet() - after.map { it.id }.toSet()
        assertEquals(1, dropped.size)
        // The one that left is the furthest out, which is the one the six-hour
        // window was always going to lose first.
        assertEquals(before.last().id, dropped.first())
    }

    @Test
    fun `empty inputs are handled`() {
        assertTrue(MergePolicy.merge(emptyList(), emptyList()).isEmpty())
        assertEquals(1, MergePolicy.merge(listOf(cal(1, 1)), emptyList()).size)
        assertEquals(1, MergePolicy.merge(emptyList(), listOf(nur(1, 1))).size)
    }

    @Test
    fun `a duplicate id collapses rather than occupying two slots`() {
        // Should be impossible across sources -- bit 31 keeps them apart -- but
        // the wire has one slot per id, and emitting two records claiming it
        // would make the watch's table depend on arrival order within a message.
        val e = cal(1, 100)
        assertEquals(1, MergePolicy.merge(listOf(e), listOf(e)).size)
    }
}
