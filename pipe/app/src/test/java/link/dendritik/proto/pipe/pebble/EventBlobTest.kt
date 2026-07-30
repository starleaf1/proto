package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.calendar.EventFacts
import link.dendritik.proto.pipe.protocol.EventKind
import link.dendritik.proto.pipe.protocol.EventOp
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The wire format, decoded back the way `watchface/src/c/wire.c` decodes it.
 *
 * A packing bug here is invisible on this side and shows up as markers in the wrong
 * place on a watch, so the format is asserted byte by byte rather than round-tripped
 * through a Kotlin unpacker that could share the same mistake.
 */
class EventBlobTest {

    private fun appointment(id: Int, start: Int, dur: Int) =
        EventFacts(id, start, dur, EventKind.APPOINTMENT)

    private fun task(id: Int, start: Int) = EventFacts(id, start, 0, EventKind.TASK)

    private fun u8(b: ByteArray, at: Int) = b[at].toInt() and 0xFF

    private fun readInt(b: ByteArray, at: Int) =
        u8(b, at) or (u8(b, at + 1) shl 8) or (u8(b, at + 2) shl 16) or (u8(b, at + 3) shl 24)

    private fun readShort(b: ByteArray, at: Int) = u8(b, at) or (u8(b, at + 1) shl 8)

    @Test
    fun `header carries version and count`() {
        val blob = EventBlob.pack(
            listOf(
                EventBlob.Record(appointment(1, 100, 60), EventOp.UPSERT),
                EventBlob.Record(task(2, 200), EventOp.UPSERT),
            ),
        )
        assertEquals(EventBlob.VERSION, u8(blob, 0))
        assertEquals(2, u8(blob, 1))
        assertEquals(EventBlob.HEADER_BYTES + 2 * EventBlob.RECORD_BYTES, blob.size)
        // Bytes 2..5 are reserved and must be zero, so the watch can rely on them
        // later without an older companion having put anything there.
        assertArrayEquals(byteArrayOf(0, 0, 0, 0), blob.copyOfRange(2, 6))
    }

    @Test
    fun `record fields land at the offsets the watch reads`() {
        val e = appointment(id = 0x11223344, start = 1_700_000_000, dur = 90)
        val blob = EventBlob.pack(listOf(EventBlob.Record(e, EventOp.UPSERT)))
        val at = EventBlob.HEADER_BYTES

        assertEquals(0x11223344, readInt(blob, at))
        assertEquals(1_700_000_000, readInt(blob, at + 4))
        assertEquals(90, readShort(blob, at + 8))
        assertEquals(EventKind.APPOINTMENT.wire, u8(blob, at + 10))
        assertEquals(EventOp.UPSERT.wire, u8(blob, at + 11))
    }

    @Test
    fun `negative ids survive as unsigned little-endian`() {
        // Instance ids are an FNV mix, so about half of them are negative Ints. The
        // watch reads them into a uint32 and only ever compares them for equality.
        val e = appointment(id = -1, start = 0, dur = 1)
        val blob = EventBlob.pack(listOf(EventBlob.Record(e, EventOp.UPSERT)))
        assertEquals(-1, readInt(blob, EventBlob.HEADER_BYTES))
    }

    @Test
    fun `task kind and remove op are distinguishable`() {
        val blob = EventBlob.pack(listOf(EventBlob.Record(task(7, 5), EventOp.REMOVE)))
        val at = EventBlob.HEADER_BYTES
        assertEquals(EventKind.TASK.wire, u8(blob, at + 10))
        assertEquals(EventOp.REMOVE.wire, u8(blob, at + 11))
    }

    @Test
    fun `duration is clamped rather than wrapped`() {
        // u16 minutes is 45 days. A wrapped value would draw a band somewhere absurd;
        // a clamped one draws a very long band, which is at least honest.
        val blob = EventBlob.pack(
            listOf(EventBlob.Record(appointment(1, 0, 100_000), EventOp.UPSERT)),
        )
        assertEquals(0xFFFF, readShort(blob, EventBlob.HEADER_BYTES + 8))
    }

    @Test
    fun `an empty flush still produces one message`() {
        val chunks = EventBlob.chunk(emptyList())
        assertEquals(1, chunks.size)
        assertEquals(0, u8(chunks[0], 1))
        assertEquals(EventBlob.HEADER_BYTES, chunks[0].size)
    }

    @Test
    fun `chunking never exceeds the record cap`() {
        val records = (1..EventBlob.MAX_RECORDS * 2 + 3).map {
            EventBlob.Record(appointment(it, it * 60, 30), EventOp.UPSERT)
        }
        val chunks = EventBlob.chunk(records)
        assertEquals(3, chunks.size)
        chunks.forEach { assertTrue(u8(it, 1) <= EventBlob.MAX_RECORDS) }
        assertEquals(records.size, chunks.sumOf { u8(it, 1) })
        // Every chunk has to fit the watch's 512-byte inbox with room for the other
        // keys riding along; an oversized AppMessage fails to transmit entirely.
        chunks.forEach { assertTrue("chunk too big: ${it.size}", it.size <= 320) }
    }
}
