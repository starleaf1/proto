package link.dendritik.proto.pipe.nuron

import link.dendritik.proto.pipe.protocol.EventKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NuronCursorTest {

    private fun row(id: Int, start: Int = 1_000_000, dur: Int = 0, kind: Int = 1) =
        NuronCursor.Row(id, start, dur, kind)

    private val nuronId = -554486453   // bit 31 set
    private val calendarId = 1234567   // bit 31 clear

    @Test
    fun `a never-reconciled provider is Unknown, not empty`() {
        // This is the one that matters. Zero rows with no stamp is a cold boot,
        // and reading it as "the user's day is clear" takes every marker off the
        // watch on the first tick after a reboot.
        assertEquals(NuronScan.Unknown, NuronCursor.classify(0L, emptyList()))
        // Even with rows: a stamp of zero means the projection was never built,
        // so whatever is in the table is not something to act on.
        assertEquals(NuronScan.Unknown, NuronCursor.classify(0L, listOf(row(nuronId))))
    }

    @Test
    fun `a reconciled provider with no rows is a genuine empty`() {
        val scan = NuronCursor.classify(1_700_000_000_000L, emptyList())
        assertTrue(scan is NuronScan.Facts)
        assertTrue((scan as NuronScan.Facts).events.isEmpty())
    }

    @Test
    fun `rows without the Nuron bit are dropped rather than repaired`() {
        // An id missing the bit means this app and Nuron's WatchKeys have
        // drifted. OR-ing it in would hide the divergence and let a Nuron entry
        // land on a calendar id, silently replacing a real meeting's marker.
        val scan = NuronCursor.classify(1L, listOf(row(nuronId), row(calendarId))) as NuronScan.Facts
        assertEquals(1, scan.events.size)
        assertEquals(nuronId, scan.events[0].id)
    }

    @Test
    fun `duration is clamped rather than trusted`() {
        // durMin is a u16 on the wire; a negative would wrap to ~65000 minutes
        // and draw a band across the entire face.
        val scan = NuronCursor.classify(
            1L,
            listOf(row(nuronId, dur = -5), row(nuronId or 0x10, dur = 99_999)),
        ) as NuronScan.Facts
        assertEquals(listOf(0, 65535), scan.events.map { it.durationMin })
    }

    @Test
    fun `kind maps to the wire enum and defaults to task`() {
        val scan = NuronCursor.classify(
            1L,
            listOf(
                row(nuronId, kind = 0),
                row(nuronId or 0x10, kind = 1),
                row(nuronId or 0x20, kind = 7),
            ),
        ) as NuronScan.Facts
        assertEquals(
            listOf(EventKind.APPOINTMENT, EventKind.TASK, EventKind.TASK),
            scan.events.map { it.kind },
        )
    }
}
