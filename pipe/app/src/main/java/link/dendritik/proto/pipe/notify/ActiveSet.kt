package link.dendritik.proto.pipe.notify

import link.dendritik.proto.pipe.protocol.IconState
import link.dendritik.proto.pipe.protocol.PhoneState

/**
 * The notifications currently in the shade that the watch cares about, keyed by
 * the system's notification key.
 *
 * Holding the whole live set — rather than counters we increment and decrement — is
 * what makes dismissal correct: when the last chat is swiped away the map is empty
 * and the envelope goes back to faded on its own. Counters drift the moment an
 * update is posted for a notification we already counted, or the listener
 * reconnects and replays the shade.
 */
class ActiveSet {
    private val verdicts = LinkedHashMap<String, Verdict>()

    /** Add or update one notification. Returns true if the snapshot changed. */
    fun put(key: String, verdict: Verdict): Boolean {
        val before = snapshot()
        if (verdict is Verdict.Ignore) verdicts.remove(key) else verdicts[key] = verdict
        return snapshot() != before
    }

    fun remove(key: String): Boolean = verdicts.remove(key) != null

    /** Replace the whole set, as after `onListenerConnected` replays the shade. */
    fun reset(all: Map<String, Verdict>) {
        verdicts.clear()
        all.forEach { (key, verdict) -> if (verdict !is Verdict.Ignore) verdicts[key] = verdict }
    }

    fun snapshot(): IconState = fold(verdicts.values)

    companion object {
        /**
         * Collapse many notifications into the three values the watch renders.
         * The phone icon shows the most urgent live call state — a phone ringing
         * now outranks a call in progress, which outranks one already missed.
         */
        fun fold(verdicts: Collection<Verdict>): IconState {
            var unread = 0
            var missed = 0
            var phone = PhoneState.IDLE
            for (verdict in verdicts) when (verdict) {
                is Verdict.Chat -> unread++
                is Verdict.Call -> {
                    if (verdict.state == PhoneState.MISSED) missed++
                    if (verdict.state.precedence > phone.precedence) phone = verdict.state
                }
                is Verdict.Ignore -> Unit
            }
            return IconState(unreadCount = unread, missedCount = missed, phone = phone)
        }
    }
}
