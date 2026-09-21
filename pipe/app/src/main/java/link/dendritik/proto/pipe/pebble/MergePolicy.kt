package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.calendar.EventFacts

/**
 * Where two sources become one table.
 *
 * ## Determinism is the whole requirement
 *
 * `PebbleSender` diffs each scan against what it believes the watch holds, and
 * sends the difference. So the merge must be a *function* of its inputs and
 * nothing else: two scans that found the same entries must produce a
 * byte-identical list, or the diff invents removes and adds for a table that did
 * not change and the markers blink once a tick for no reason.
 *
 * Hence a total order over `(startUtcS, kind, id)`. `id` is unique, so there are
 * no ties, so the result cannot depend on map iteration order, on which source
 * was read first, or on how either source happened to sort internally.
 *
 * ## Truncation is by that same order, and by nothing else
 *
 * The obvious alternative — take the calendar first, then fill with Nuron — is
 * the one to avoid: membership would then depend on the *relative sizes* of the
 * two sources, which flips as either one changes, so adding a meeting could
 * silently evict a quest that starts sooner. Soonest-first is the only rule
 * where what falls off the end is also what matters least.
 */
object MergePolicy {

    /**
     * Deliberately 24, not [link.dendritik.proto.pipe.protocol.EVENTS_MAX]'s 32.
     *
     * Three reasons, in order of how much they matter:
     *
     *  - It keeps a flush to exactly one AppMessage. `6 + 24 * 12 = 294` payload
     *    bytes, which is the budget `docs/protocol.md` already works out against
     *    the 512-byte inbox. The multi-message `MORE` path and its "bail on the
     *    first failed chunk" recovery then stay untravelled, which is worth more
     *    than eight extra entries nobody can read on a 144-pixel screen.
     *  - It is under the watch's `EVENTS_MAX`, so the watchface's own full-table
     *    eviction — which picks the furthest-future victim in table order — is
     *    never reached. The table is decided here, on the phone, where it is
     *    unit-tested, rather than there, in C, over Bluetooth.
     *  - The persisted copy caps at `STORE_CAP` 21 regardless, and `events_save`
     *    keeps the 21 soonest, which is a deterministic prefix of a
     *    deterministic list.
     */
    const val MAX_MERGED = 24

    private val ORDER: Comparator<EventFacts> =
        compareBy<EventFacts> { it.startUtcS }
            .thenBy { it.kind.wire }
            .thenBy { it.id }

    fun merge(calendar: List<EventFacts>, nuron: List<EventFacts>): List<EventFacts> {
        // Last writer wins on a duplicate id. It should not happen — the two
        // sources partition the id space by bit 31 — but collapsing rather than
        // emitting both is the safe reading: the wire has one slot per id, and
        // two records claiming it would make the watch's table depend on which
        // arrived last within a single message.
        val byId = LinkedHashMap<Int, EventFacts>(calendar.size + nuron.size)
        for (e in calendar) byId[e.id] = e
        for (e in nuron) byId[e.id] = e

        return byId.values.sortedWith(ORDER).take(MAX_MERGED)
    }
}
