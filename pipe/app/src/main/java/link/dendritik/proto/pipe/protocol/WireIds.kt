package link.dendritik.proto.pipe.protocol

/**
 * Partitioning the one flat `CalEvents` id space between two sources.
 *
 * ## Why this is arithmetic and not a hope
 *
 * The wire carries a `u32` per entry and the watch upserts by it. With one
 * source, a hash collision was a remote nuisance. With two, it is a *cross-source*
 * collision — a Nuron quest and a calendar instance landing on the same id — and
 * the failure mode is that one silently replaces the other. Nobody reports "the
 * marker for my 3pm meeting is missing" as a hashing bug; they report the
 * watchface as unreliable, and the cause is invisible from either side of a
 * Bluetooth link.
 *
 * So the space is split by a bit rather than left to the spread of FNV-1a.
 * Calendar ids are masked to 31 bits, Nuron ids set bit 31, and disjointness is
 * then a property of the arithmetic that no future change to either hash can
 * break. The cost is one bit of a space that holds at most two dozen live
 * entries.
 *
 * Within a source, collisions remain possible at roughly 2e-7 for 24 entries in
 * a 31-bit space, and the failure there is a *disappearance*, never a clone,
 * because the table is keyed by id. That is the right way round: a missing
 * marker ages out and the next flush restores it.
 *
 * The Nuron side of this is mirrored in `QuestJournal`'s `WatchKeys.wireId` and
 * in the server's `functions/src/pebble/keys.ts`, which produce the raw hash;
 * this object only owns the partition.
 */
object WireIds {

    /** Set on every Nuron id, clear on every calendar id. */
    const val NURON_BIT: Int = Int.MIN_VALUE

    /** Mask applied to the calendar source's own hash. */
    const val CALENDAR_MASK: Int = 0x7FFF_FFFF

    fun calendar(rawHash: Int): Int = rawHash and CALENDAR_MASK

    /**
     * Nuron ids arrive already partitioned — `WatchKeys.wireId` sets the bit on
     * the phone side, because that is where the hash is computed. This is here
     * to assert that, not to do it: an id arriving without the bit means the two
     * implementations have drifted, and silently OR-ing it in would hide exactly
     * the divergence that matters.
     */
    fun isNuron(id: Int): Boolean = (id and NURON_BIT) != 0

    fun isCalendar(id: Int): Boolean = !isNuron(id)
}
