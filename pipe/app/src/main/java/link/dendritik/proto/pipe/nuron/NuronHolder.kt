package link.dendritik.proto.pipe.nuron

import link.dendritik.proto.pipe.calendar.EventFacts

/**
 * What Nuron's entries are *right now*, given a history of answers and
 * non-answers.
 *
 * ## The decision this encodes
 *
 * [NuronScan.Unknown] means the projection could not be read. The two obvious
 * responses are both wrong on their own:
 *
 *   contribute nothing  — a single transient fault, a mid-upgrade process kill,
 *     one thrown binder call, and every Nuron marker leaves the watch. The next
 *     tick restores them, so the visible result is markers that flicker.
 *   keep the last set forever — a permanently broken provider then pins stale
 *     markers on the wrist indefinitely, and the watch has no way to know it is
 *     being told something old. That is the worse failure: a wrong marker is
 *     read as a fact, and unlike a missing one nothing about it looks off.
 *
 * So: hold across a few failures, then let go. Three ticks is the same shape of
 * judgement the watch's own liveness watchdog makes ("it missed one and the
 * retry missed too"), and on the slow 600 s tier it is half an hour of grace —
 * long enough to ride out an app update, short enough that stale markers age
 * out well inside the six-hour window they live in.
 *
 * Pure and single-threaded: `PipeEngine` reconciles on one thread, and making
 * this stateful-but-pure is what lets the decay be unit-tested without a clock.
 */
class NuronHolder(private val maxStaleTicks: Int = DEFAULT_MAX_STALE_TICKS) {

    private var last: List<EventFacts> = emptyList()
    private var staleTicks: Int = 0

    /** For the diagnostics screen; not used in any decision. */
    var state: String = "never read"
        private set

    fun accept(scan: NuronScan): List<EventFacts> = when (scan) {
        is NuronScan.Facts -> {
            last = scan.events
            staleTicks = 0
            state = "ok (${scan.events.size})"
            last
        }
        NuronScan.Absent -> {
            // Durable, and it will not resolve itself. Drop immediately: holding
            // markers for an app that has been uninstalled is indefensible.
            last = emptyList()
            staleTicks = 0
            state = "absent"
            last
        }
        NuronScan.Unknown -> {
            staleTicks++
            if (staleTicks > maxStaleTicks) {
                last = emptyList()
                state = "unreadable (gave up after $maxStaleTicks)"
            } else {
                state = "unreadable (holding, $staleTicks/$maxStaleTicks)"
            }
            last
        }
    }

    companion object {
        const val DEFAULT_MAX_STALE_TICKS = 3
    }
}
