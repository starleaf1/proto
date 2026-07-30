package link.dendritik.proto.pipe.calendar

import link.dendritik.proto.pipe.protocol.EventKind

/**
 * One calendar entry, flattened away from any framework type.
 *
 * This is the whole of what crosses the wire about an entry, and deliberately so:
 * the watch draws a position and a duration, never a title. Notification and
 * calendar *content* never leaves the phone.
 *
 * [id] must be stable across re-scans and unique per *instance* — a recurring
 * weekly meeting produces many of these from one event row, and two of them
 * colliding would make the later one silently replace the earlier.
 */
data class EventFacts(
    val id: Int,
    val startUtcS: Int,
    val durationMin: Int,
    val kind: EventKind,
) {
    /** Zero duration means a point in time: a task or a reminder, not a meeting. */
    val isPoint: Boolean get() = durationMin == 0
}
