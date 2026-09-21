package link.dendritik.proto.pipe.nuron

import android.database.Cursor
import android.os.Bundle
import link.dendritik.proto.pipe.calendar.EventFacts
import link.dendritik.proto.pipe.protocol.EventKind
import link.dendritik.proto.pipe.protocol.WireIds

/**
 * Turning Nuron's cursor into facts — the pure half of [NuronSource].
 *
 * Split out so the interesting decisions (which rows to trust, what an empty
 * answer means, what to do with an id that has drifted) are testable on a bare
 * JVM with a `MatrixCursor`, leaving the adapter with nothing in it but the
 * `ContentResolver` call. This is the same boundary `EventFacts` draws for the
 * calendar source, and `pipe`'s architectural rule is that framework types stop
 * at it.
 */
object NuronCursor {

    /**
     * Read a cursor Nuron returned.
     *
     * Columns are looked up by name rather than by position: the two contract
     * files are copies, and a reordering on one side would otherwise be read as
     * plausible-looking nonsense — a start time interpreted as a duration puts a
     * marker fifty thousand years out rather than failing.
     */
    fun read(cursor: Cursor?, extras: Bundle?): NuronScan {
        if (cursor == null) return NuronScan.Unknown

        val reconciledAt = extras?.getLong(NuronContract.EXTRA_RECONCILED_AT_MS, 0L) ?: 0L

        val iId = cursor.getColumnIndex(NuronContract.COL_ID)
        val iStart = cursor.getColumnIndex(NuronContract.COL_START_EPOCH_S)
        val iDur = cursor.getColumnIndex(NuronContract.COL_DUR_MIN)
        val iKind = cursor.getColumnIndex(NuronContract.COL_KIND)
        if (iId < 0 || iStart < 0 || iDur < 0 || iKind < 0) return NuronScan.Unknown

        val rows = mutableListOf<Row>()
        while (cursor.moveToNext()) {
            rows += Row(
                id = cursor.getInt(iId),
                startUtcS = cursor.getInt(iStart),
                durMin = cursor.getInt(iDur),
                kind = cursor.getInt(iKind),
            )
        }
        return classify(reconciledAt, rows)
    }

    /** One row as the provider spelled it, before any of it is believed. */
    data class Row(val id: Int, val startUtcS: Int, val durMin: Int, val kind: Int)

    /**
     * What a set of rows and a reconciliation stamp actually mean.
     *
     * Pure, and separate from the cursor walk, because this is where the
     * judgements are: which rows to trust and what an empty answer signifies.
     * Walking a cursor is not worth a Robolectric dependency; deciding that zero
     * rows means "do not delete anything" is worth a test.
     */
    fun classify(reconciledAtMs: Long, rows: List<Row>): NuronScan {
        if (reconciledAtMs <= 0L) {
            // Nuron has never built its projection: a fresh install, a signed-out
            // user, or a first sync still in flight. Zero rows here means "I have
            // not looked", not "there is nothing", and deleting on the strength of
            // it is the failure this whole three-way exists to prevent.
            return NuronScan.Unknown
        }

        val out = mutableListOf<EventFacts>()
        for (r in rows) {
            // An id without the Nuron bit means this app and Nuron's WatchKeys
            // have drifted. Dropping the row is deliberate: OR-ing the bit in
            // would hide the divergence, and a Nuron id colliding with a
            // calendar one silently replaces a real meeting's marker.
            if (!WireIds.isNuron(r.id)) continue

            out += EventFacts(
                id = r.id,
                startUtcS = r.startUtcS,
                // Clamped rather than trusted, exactly as docs/protocol.md
                // requires of everything before it is packed: durMin is a u16 on
                // the wire and a negative one would wrap to fifty thousand
                // minutes, drawing a band across the whole face.
                durationMin = r.durMin.coerceIn(0, 65535),
                kind = if (r.kind == EventKind.APPOINTMENT.wire) {
                    EventKind.APPOINTMENT
                } else {
                    EventKind.TASK
                },
            )
        }
        return NuronScan.Facts(out)
    }
}
