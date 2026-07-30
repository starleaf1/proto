package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.calendar.EventFacts
import link.dendritik.proto.pipe.protocol.EventOp

/**
 * Packs calendar entries into the byte array `Protocol.KEY_CAL_EVENTS` carries.
 *
 * One blob rather than a message per entry: a first sync can be twenty entries,
 * and twenty round trips over Bluetooth is both slow and twenty chances to be
 * dropped by a transport with no retry contract. Pure and framework-free, so the
 * whole format is covered by JVM unit tests.
 *
 * Layout, little-endian throughout:
 * ```
 * header   u8 version   u8 count   u32 reserved
 * record   u32 id       i32 startEpochS   u16 durMin   u8 kind   u8 op
 * ```
 * `startEpochS` is absolute UTC seconds. Pebble takes its clock from the phone, and
 * absolute timestamps avoid a whole class of bug that "minutes from now" invites —
 * a reference instant that has already moved by the time the watch decodes it.
 */
object EventBlob {
    const val VERSION = 1
    const val HEADER_BYTES = 6
    const val RECORD_BYTES = 12

    /**
     * Records per message. The watch opens a 512-byte inbox; this is 294 bytes of
     * it, leaving room for the other keys that ride along and for a dictionary's
     * own overhead. An oversized AppMessage is not truncated — it fails to
     * transmit at all — so the cap is deliberately well under the limit.
     */
    const val MAX_RECORDS = 24

    data class Record(val event: EventFacts, val op: EventOp)

    /**
     * Splits into transmittable messages. An empty list still yields one empty
     * message: that is how a flush says "the next six hours are clear", and
     * sending nothing at all would leave the watch showing what it had.
     */
    fun chunk(records: List<Record>): List<ByteArray> =
        if (records.isEmpty()) listOf(pack(emptyList()))
        else records.chunked(MAX_RECORDS).map(::pack)

    fun pack(records: List<Record>): ByteArray {
        require(records.size <= MAX_RECORDS) { "too many records: ${records.size}" }
        val out = ByteArray(HEADER_BYTES + records.size * RECORD_BYTES)
        out[0] = VERSION.toByte()
        out[1] = records.size.toByte()
        // Bytes 2..5 are reserved and sent as zero.

        var o = HEADER_BYTES
        for (r in records) {
            putInt(out, o, r.event.id)
            putInt(out, o + 4, r.event.startUtcS)
            // Clamped rather than wrapped: a duration that does not fit is absurd
            // (u16 minutes is 45 days) and a wrapped one would draw a wrong band.
            putShort(out, o + 8, r.event.durationMin.coerceIn(0, 0xFFFF))
            out[o + 10] = r.event.kind.wire.toByte()
            out[o + 11] = r.op.wire.toByte()
            o += RECORD_BYTES
        }
        return out
    }

    private fun putInt(b: ByteArray, at: Int, v: Int) {
        b[at] = (v and 0xFF).toByte()
        b[at + 1] = ((v ushr 8) and 0xFF).toByte()
        b[at + 2] = ((v ushr 16) and 0xFF).toByte()
        b[at + 3] = ((v ushr 24) and 0xFF).toByte()
    }

    private fun putShort(b: ByteArray, at: Int, v: Int) {
        b[at] = (v and 0xFF).toByte()
        b[at + 1] = ((v ushr 8) and 0xFF).toByte()
    }
}

/**
 * Turns two successive calendar scans into the smallest set of records that brings
 * the watch's table in line with ours.
 *
 * Removal is one op regardless of cause. An entry that was deleted, an appointment
 * that was cancelled and a task that was completed all leave the scan the same way
 * — by no longer being in it — and the watch renders no reason, so the wire carries
 * none.
 */
object EventDiff {
    fun diff(previous: Map<Int, EventFacts>, current: List<EventFacts>): List<EventBlob.Record> {
        val out = mutableListOf<EventBlob.Record>()
        val seen = HashSet<Int>(current.size)

        for (e in current) {
            seen += e.id
            if (previous[e.id] != e) out += EventBlob.Record(e, EventOp.UPSERT)
        }
        for ((id, old) in previous) {
            if (id !in seen) out += EventBlob.Record(old, EventOp.REMOVE)
        }
        return out
    }
}
