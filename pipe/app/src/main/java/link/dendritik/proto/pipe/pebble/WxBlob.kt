package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.weather.WxAlert

/**
 * Packs weather alerts into the byte array `Protocol.KEY_WX_ALERTS` carries.
 *
 * Layout, little-endian throughout:
 * ```
 * header   u8 version   u8 count
 * record   u8 hazard    u8 reserved   i32 onsetEpochS   u16 durMin
 * ```
 * Replaced whole whenever the key is present; an empty blob is "the forecast is clear".
 * The reserved byte is sent as zero. It was going to be a severity, and it is not one
 * yet because the watch would not draw it — and the wire carries nothing the watch
 * does not render.
 */
object WxBlob {
    const val VERSION = 1
    const val HEADER_BYTES = 2
    const val RECORD_BYTES = 8

    /** The watch's table. 2 + 8 × 8 = 66 bytes, riding in the calendar flush's message. */
    const val MAX_RECORDS = 8

    fun pack(alerts: List<WxAlert>): ByteArray {
        require(alerts.size <= MAX_RECORDS) { "too many alerts: ${alerts.size}" }
        val out = ByteArray(HEADER_BYTES + alerts.size * RECORD_BYTES)
        out[0] = VERSION.toByte()
        out[1] = alerts.size.toByte()
        var o = HEADER_BYTES
        for (a in alerts) {
            out[o] = a.hazard.wire.toByte()
            // o + 1 reserved, zero.
            val t = a.onsetUtcS
            out[o + 2] = (t and 0xFF).toByte()
            out[o + 3] = ((t ushr 8) and 0xFF).toByte()
            out[o + 4] = ((t ushr 16) and 0xFF).toByte()
            out[o + 5] = ((t ushr 24) and 0xFF).toByte()
            val d = a.durationMin.coerceIn(0, 0xFFFF)
            out[o + 6] = (d and 0xFF).toByte()
            out[o + 7] = ((d ushr 8) and 0xFF).toByte()
            o += RECORD_BYTES
        }
        return out
    }
}

/**
 * Whether a sync has anything to say, calendar and weather together.
 *
 * Weather only counts when there is a list to send: `null` means "leave the key out",
 * which is never news. The calendar's gate is [EventDiff]'s; this is the one place the
 * two are combined, so a forecast that changed with the calendar unchanged still goes
 * out between ticks.
 */
object SyncGate {
    fun changed(eventsChanged: Boolean, sentWx: List<WxAlert>?, wx: List<WxAlert>?): Boolean =
        eventsChanged || (wx != null && wx != sentWx)
}
