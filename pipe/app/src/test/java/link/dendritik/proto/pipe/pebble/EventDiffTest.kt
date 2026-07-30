package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.calendar.EventFacts
import link.dendritik.proto.pipe.protocol.EventKind
import link.dendritik.proto.pipe.protocol.EventOp
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * What two successive calendar scans should send.
 *
 * The interesting cases are all absences: an entry deleted, an appointment cancelled
 * and a task completed are indistinguishable from here, because all three simply
 * stop appearing in the scan. That is the whole reason the wire carries one removal
 * op rather than a reason.
 */
class EventDiffTest {

    private fun ev(id: Int, start: Int = 1000, dur: Int = 30) =
        EventFacts(id, start, dur, if (dur == 0) EventKind.TASK else EventKind.APPOINTMENT)

    private fun previous(vararg events: EventFacts) = events.associateBy { it.id }

    @Test
    fun `nothing changed sends nothing`() {
        val a = ev(1)
        assertTrue(EventDiff.diff(previous(a), listOf(a)).isEmpty())
    }

    @Test
    fun `a new entry is an upsert`() {
        val records = EventDiff.diff(previous(ev(1)), listOf(ev(1), ev(2)))
        assertEquals(1, records.size)
        assertEquals(EventOp.UPSERT, records[0].op)
        assertEquals(2, records[0].event.id)
    }

    @Test
    fun `a moved entry is an upsert, not a remove and an add`() {
        // The id is derived from the start time rounded to the minute, so a genuine
        // reschedule usually re-keys. Editing the duration does not, and that path has
        // to reach the watch as an update to the same marker.
        val records = EventDiff.diff(previous(ev(1, dur = 30)), listOf(ev(1, dur = 90)))
        assertEquals(1, records.size)
        assertEquals(EventOp.UPSERT, records[0].op)
        assertEquals(90, records[0].event.durationMin)
    }

    @Test
    fun `a vanished entry is a remove`() {
        val records = EventDiff.diff(previous(ev(1), ev(2)), listOf(ev(2)))
        assertEquals(1, records.size)
        assertEquals(EventOp.REMOVE, records[0].op)
        assertEquals(1, records[0].event.id)
    }

    @Test
    fun `an emptied window removes everything it held`() {
        val records = EventDiff.diff(previous(ev(1), ev(2), ev(3)), emptyList())
        assertEquals(3, records.size)
        assertTrue(records.all { it.op == EventOp.REMOVE })
    }

    @Test
    fun `a first scan against nothing is all upserts`() {
        val records = EventDiff.diff(emptyMap(), listOf(ev(1), ev(2, dur = 0)))
        assertEquals(2, records.size)
        assertTrue(records.all { it.op == EventOp.UPSERT })
    }
}
