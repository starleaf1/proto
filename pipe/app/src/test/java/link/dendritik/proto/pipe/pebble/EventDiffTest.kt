package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.calendar.EventFacts
import link.dendritik.proto.pipe.protocol.EventKind
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Whether two successive calendar scans are worth a message.
 *
 * Every message carries the whole table, so this only decides whether to send. The
 * case that matters most is the first one: an unchanged scan must send nothing, because
 * the calendar provider fires its observer on churn that changes no entry, and each of
 * those would otherwise wake the radio.
 */
class EventDiffTest {

    private fun ev(id: Int, start: Int = 1000, dur: Int = 30) =
        EventFacts(id, start, dur, if (dur == 0) EventKind.TASK else EventKind.APPOINTMENT)

    private fun previous(vararg events: EventFacts) = events.associateBy { it.id }

    @Test
    fun `nothing changed sends nothing`() {
        assertFalse(EventDiff.changed(previous(ev(1), ev(2)), listOf(ev(1), ev(2))))
    }

    @Test
    fun `scan order is not a change`() {
        assertFalse(EventDiff.changed(previous(ev(1), ev(2)), listOf(ev(2), ev(1))))
    }

    @Test
    fun `an empty window after an empty window is not a change`() {
        assertFalse(EventDiff.changed(emptyMap(), emptyList()))
    }

    @Test
    fun `a new entry is a change`() {
        assertTrue(EventDiff.changed(previous(ev(1)), listOf(ev(1), ev(2))))
    }

    @Test
    fun `an edited entry is a change`() {
        // The id is derived from the start time rounded to the minute, so a genuine
        // reschedule usually re-keys. Editing the duration does not, and that has to
        // reach the watch too.
        assertTrue(EventDiff.changed(previous(ev(1, dur = 30)), listOf(ev(1, dur = 90))))
    }

    @Test
    fun `a vanished entry is a change`() {
        assertTrue(EventDiff.changed(previous(ev(1), ev(2)), listOf(ev(2))))
    }

    @Test
    fun `an emptied window is a change`() {
        assertTrue(EventDiff.changed(previous(ev(1), ev(2)), emptyList()))
    }
}
