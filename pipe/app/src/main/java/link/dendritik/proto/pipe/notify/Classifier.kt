package link.dendritik.proto.pipe.notify

import android.app.Notification
import android.app.NotificationManager
import link.dendritik.proto.pipe.config.AppRule
import link.dendritik.proto.pipe.config.IconCategory
import link.dendritik.proto.pipe.config.PipeConfig
import link.dendritik.proto.pipe.protocol.PhoneState

/**
 * What one notification means to the watch: a chat that lights the envelope, a
 * call in some state that colours the phone icon, or nothing at all.
 */
sealed interface Verdict {
    data object Ignore : Verdict
    data object Chat : Verdict
    data class Call(val state: PhoneState) : Verdict
}

/**
 * Routes notifications to watch icons. Pure: no Android framework objects, no
 * state, so the whole decision table is unit-testable.
 *
 * The Android constants referenced here (`CATEGORY_*`, `IMPORTANCE_*`,
 * `CALL_TYPE_*`) are compile-time `static final` values and inline into the
 * bytecode, so they work under the stubbed android.jar that unit tests run against.
 */
object Classifier {

    // Notification.CallStyle.CALL_TYPE_*, inlined so this file needs no API 31 guard.
    private const val CALL_TYPE_INCOMING = 1
    private const val CALL_TYPE_ONGOING = 2
    private const val CALL_TYPE_SCREENING = 3

    fun classify(facts: NotificationFacts, config: PipeConfig): Verdict {
        // A group summary duplicates its children; counting both double-counts.
        if (facts.isGroupSummary) return Verdict.Ignore

        val rule = config.ruleFor(facts.packageName) ?: return Verdict.Ignore

        return when (categoryOf(facts, rule)) {
            IconCategory.CALL -> {
                val state = callState(facts)
                // Some call states are exempt from the silence rule, so it has to be
                // judged against the resolved state rather than before it.
                if (isSuppressed(facts, config, state)) Verdict.Ignore
                else Verdict.Call(state)
            }
            IconCategory.CHAT ->
                if (isSuppressed(facts, config, null)) Verdict.Ignore else Verdict.Chat
            null -> Verdict.Ignore
        }
    }

    /**
     * Has the user silenced this? Channel importance is the modern answer;
     * `priority` covers API 24-25, where channels do not exist yet.
     */
    fun isSilent(facts: NotificationFacts): Boolean = facts.importance?.let {
        it < NotificationManager.IMPORTANCE_DEFAULT
    } ?: (facts.priority < Notification.PRIORITY_DEFAULT)

    /** [state] is null for chats, which are never exempt. */
    private fun isSuppressed(
        facts: NotificationFacts,
        config: PipeConfig,
        state: PhoneState?,
    ): Boolean {
        if (!config.ignoreSilent || !isSilent(facts)) return false
        return state == null || state !in config.silentExemptCallStates
    }

    /**
     * Which icon this notification feeds: configured channel patterns first (the
     * user's own routing), then the notification's declared category, then the
     * rule's fallback. Calls are tested before chats so a broad chat pattern
     * cannot swallow a call channel.
     */
    private fun categoryOf(facts: NotificationFacts, rule: AppRule): IconCategory? {
        val channel = facts.channelId?.lowercase()
        if (channel != null) {
            if (rule.callChannels.anyIn(channel)) return IconCategory.CALL
            if (rule.chatChannels.anyIn(channel)) return IconCategory.CHAT
        }
        // CallStyle is unambiguous even when the channel id is opaque or absent.
        if (facts.callType != null) return IconCategory.CALL
        when (facts.category) {
            Notification.CATEGORY_CALL, Notification.CATEGORY_MISSED_CALL -> return IconCategory.CALL
            Notification.CATEGORY_MESSAGE, Notification.CATEGORY_EMAIL -> return IconCategory.CHAT
        }
        return rule.fallback
    }

    /**
     * Ringing, in progress, or missed. Ordered most-certain signal first: an
     * explicit missed-call category, then CallStyle's own call type, then the
     * shape of the notification (a full-screen intent or answer/decline buttons
     * mean it is ringing right now), then channel-name hints, then whether the
     * system still considers it ongoing.
     */
    fun callState(facts: NotificationFacts): PhoneState {
        val channel = facts.channelId?.lowercase().orEmpty()

        if (facts.category == Notification.CATEGORY_MISSED_CALL) return PhoneState.MISSED
        if ("missed" in channel) return PhoneState.MISSED

        when (facts.callType) {
            CALL_TYPE_INCOMING, CALL_TYPE_SCREENING -> return PhoneState.RINGING
            CALL_TYPE_ONGOING -> return PhoneState.ONGOING
        }

        if (facts.hasFullScreenIntent) return PhoneState.RINGING
        if (facts.hasAnswerAndDecline()) return PhoneState.RINGING
        if ("incoming" in channel || "ring" in channel) return PhoneState.RINGING

        if ("ongoing" in channel || "in_call" in channel || "in-call" in channel) {
            return PhoneState.ONGOING
        }
        // Still live but with nothing to answer: a call in progress. Otherwise the
        // notification is a dismissible record of a call that already ended.
        return if (facts.isOngoing || !facts.isClearable) PhoneState.ONGOING else PhoneState.MISSED
    }

    /**
     * Both an accept and a reject button is the signature of an incoming call, and
     * it is the one cue that works on apps that set neither CallStyle nor a
     * category. Matched loosely: labels are localised, so this is a hint that only
     * ever runs after the stronger signals above have declined to answer.
     */
    private fun NotificationFacts.hasAnswerAndDecline(): Boolean {
        val answer = actionTitles.any { t -> ANSWER_WORDS.any { it in t } }
        val decline = actionTitles.any { t -> DECLINE_WORDS.any { it in t } }
        return answer && decline
    }

    private val ANSWER_WORDS = listOf("answer", "accept", "pick up")
    private val DECLINE_WORDS = listOf("decline", "reject", "dismiss", "hang up", "ignore")

    private fun List<String>.anyIn(haystack: String) = any { it in haystack }
}
