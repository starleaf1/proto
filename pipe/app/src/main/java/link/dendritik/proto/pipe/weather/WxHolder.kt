package link.dendritik.proto.pipe.weather

/**
 * The result of asking for a forecast, with "clear", "not asked" and "no answer" kept
 * apart — the distinction [NuronScan][link.dendritik.proto.pipe.nuron.NuronScan] draws,
 * for the same reason: an empty list read as a fact is a clear sky on a watch that was
 * never told anything.
 */
sealed interface WxScan {
    /** A forecast came back. The list may be empty, and then the sky is clear. */
    data class Alerts(val alerts: List<WxAlert>, val fetchedAtS: Int) : WxScan

    /** Switched off, or the location grant is missing. A durable zero. */
    data object Absent : WxScan

    /** No location fix, the network, the server, the parse — nothing can be concluded. */
    data class Unknown(val reason: String) : WxScan
}

/**
 * What the watch should be told about the weather *now*, given a history of answers.
 *
 * Three answers, not two, because the key itself is three-valued on the wire:
 *  - a list, possibly empty, which replaces what the watch holds;
 *  - `null`, which leaves the key out, and the watch keeps what it has.
 *
 * `null` is only ever the answer before anything has been heard since the process
 * started. The watch restores its own table across a relaunch, and a companion that has
 * not fetched yet has nothing better to offer than what the watch already holds.
 *
 * A fetch that fails holds the last forecast for [maxAgeS], less whatever of it has
 * ended, and then lets go with an empty list. A storm forecast three hours ago is still
 * a good guess; one forecast yesterday is a stale marker read as a fact, and a wrong
 * marker is worse than a missing one because nothing about it looks off.
 *
 * Pure and single-threaded, like [NuronHolder][link.dendritik.proto.pipe.nuron.NuronHolder].
 */
class WxHolder(private val maxAgeS: Int = DEFAULT_MAX_AGE_S) {

    private var last: List<WxAlert>? = null
    private var lastAtS = 0

    /** For the diagnostics screen; not used in any decision. */
    var state: String = "never fetched"
        private set

    fun accept(scan: WxScan) {
        when (scan) {
            is WxScan.Alerts -> {
                last = scan.alerts
                lastAtS = scan.fetchedAtS
                state = "ok (${scan.alerts.size})"
            }
            WxScan.Absent -> {
                last = emptyList()
                lastAtS = 0
                state = "off"
            }
            is WxScan.Unknown -> {
                state = "unavailable: ${scan.reason}"
            }
        }
    }

    fun current(nowS: Int): List<WxAlert>? {
        val held = last ?: return null
        if (lastAtS != 0 && nowS - lastAtS > maxAgeS) return emptyList()
        return held.filter { it.endUtcS > nowS }
    }

    companion object {
        const val DEFAULT_MAX_AGE_S = 3 * 3600
    }
}

/**
 * Whether a fetch is owed. Pure, so the cadence is tested rather than trusted.
 *
 * A forecast changes over hours, not minutes, so a success holds for [SUCCESS_S]; the
 * periodic tick is ten minutes, so that is every third. A failure retries on the next
 * tick but not before — [ATTEMPT_S] is just under one period, so a tick that slips a
 * little in Doze still finds it due, and a burst of reconnects does not hammer the
 * server.
 */
object WxSchedule {
    const val SUCCESS_S = 30 * 60
    const val ATTEMPT_S = 9 * 60

    fun due(nowS: Int, lastAttemptS: Int, lastSuccessS: Int): Boolean =
        (lastSuccessS == 0 || nowS - lastSuccessS >= SUCCESS_S) &&
            (lastAttemptS == 0 || nowS - lastAttemptS >= ATTEMPT_S)
}
