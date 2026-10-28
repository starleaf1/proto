package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.protocol.Hazard
import link.dendritik.proto.pipe.weather.WxAlert
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The weather blob, decoded the way `wire.c`'s `handle_wx` decodes it — byte by byte,
 * for the reason [EventBlobTest] gives.
 */
class WxBlobTest {

    private fun u8(b: ByteArray, at: Int) = b[at].toInt() and 0xFF
    private fun readInt(b: ByteArray, at: Int) =
        u8(b, at) or (u8(b, at + 1) shl 8) or (u8(b, at + 2) shl 16) or (u8(b, at + 3) shl 24)
    private fun readShort(b: ByteArray, at: Int) = u8(b, at) or (u8(b, at + 1) shl 8)

    @Test
    fun `records sit at the documented offsets`() {
        val blob = WxBlob.pack(
            listOf(
                WxAlert(Hazard.THUNDER, 1_761_600_000, 90),
                WxAlert(Hazard.GUSTS, 1_761_610_800, 45),
            ),
        )
        assertEquals(2 + 2 * 8, blob.size)
        assertEquals(WxBlob.VERSION, u8(blob, 0))
        assertEquals(2, u8(blob, 1))

        assertEquals(Hazard.THUNDER.wire, u8(blob, 2))
        assertEquals(0, u8(blob, 3))                         // reserved
        assertEquals(1_761_600_000, readInt(blob, 4))
        assertEquals(90, readShort(blob, 8))

        assertEquals(Hazard.GUSTS.wire, u8(blob, 10))
        assertEquals(1_761_610_800, readInt(blob, 12))
        assertEquals(45, readShort(blob, 16))
    }

    @Test
    fun `an empty forecast is a header and no records`() {
        val blob = WxBlob.pack(emptyList())
        assertEquals(2, blob.size)
        assertEquals(0, u8(blob, 1))
    }

    @Test
    fun `a duration past u16 clamps rather than wraps`() {
        val blob = WxBlob.pack(listOf(WxAlert(Hazard.HEAT, 0, 70_000)))
        assertEquals(0xFFFF, readShort(blob, 8))
    }

    @Test(expected = IllegalArgumentException::class)
    fun `more than the watch holds is refused`() {
        WxBlob.pack(List(WxBlob.MAX_RECORDS + 1) { WxAlert(Hazard.RAIN, it, 60) })
    }

    @Test
    fun `the largest flush still fits the watch's inbox`() {
        // A Pebble dictionary is a 1-byte header and 7 bytes of tuple header per key.
        // The flush carries four: CalEvents, CalFlags, Heartbeat and WxAlerts.
        val cal = EventBlob.HEADER_BYTES + EventBlob.MAX_RECORDS * EventBlob.RECORD_BYTES
        val wx = WxBlob.HEADER_BYTES + WxBlob.MAX_RECORDS * WxBlob.RECORD_BYTES
        val total = 1 + 4 * 7 + cal + 4 + 4 + wx
        assertEquals(397, total)
        assertTrue("$total bytes against a 512-byte inbox", total <= 512)
    }

    @Test
    fun `weather alone is news, and leaving it out is not`() {
        val a = listOf(WxAlert(Hazard.SNOW, 100, 60))
        assertTrue(SyncGate.changed(false, null, a))
        assertTrue(SyncGate.changed(false, a, emptyList()))
        assertFalse(SyncGate.changed(false, a, a))
        assertFalse(SyncGate.changed(false, a, null))
        assertTrue(SyncGate.changed(true, a, a))
    }
}
