package link.dendritik.proto.pipe.calendar

/**
 * One calendar on the phone, flattened away from any framework type — what the
 * settings screen lists and what [CalendarChoice] decides over.
 *
 * [id] is the provider's row id, which is what an instance query filters on. [key] is
 * what a choice is remembered by, and the two are different on purpose — see
 * [CalendarChoice.key].
 */
data class CalendarInfo(
    val id: Long,
    val key: String,
    val name: String,
    val account: String,
    val color: Int,
)

/**
 * Which calendars reach the watch.
 *
 * The stored choice is `null` until the user first changes anything, and `null` means
 * every calendar: that is the behaviour from before there was a choice, so an update
 * changes nothing on the wrist. Once there is a choice it is an **allowlist** — the set
 * of keys that are sent — and a calendar that appears afterwards is not sent until it is
 * ticked. That is the direction a privacy control has to fail in: a new work account
 * turning up on the wrist unasked is the leak this exists to stop, whereas an unticked
 * calendar is visible, and fixable, on the settings screen.
 *
 * Pure, and unit-tested, because the wrong answer is invisible from the phone: it is
 * markers that are, or are not, on a watch.
 */
object CalendarChoice {

    /**
     * What a choice is remembered by.
     *
     * Not the row id alone. Removing and re-adding an account, or clearing the calendar
     * provider's storage, re-creates every calendar under a fresh `_ID`, and an allowlist
     * keyed on those would silently drop every one of them from the watch — which reads
     * as a clear afternoon, not as a setting. A synced calendar's `_SYNC_ID` is the
     * server's own identity for it and survives both, so it keys on that, qualified by
     * the account, because two accounts can each have a calendar with the same server id.
     * A local calendar has no sync id, and its row id is the only identity it has.
     */
    fun key(id: Long, accountType: String?, accountName: String?, syncId: String?): String =
        if (accountType != null && accountName != null && syncId != null) {
            "sync$SEP$accountType$SEP$accountName$SEP$syncId"
        } else {
            "id$SEP$id"
        }

    fun isSent(key: String, chosen: Set<String>?): Boolean = chosen == null || key in chosen

    /**
     * The row ids to query, or `null` for no filter at all.
     *
     * An empty list is a real answer — nothing is sent — and is not the same as `null`.
     */
    fun sentIds(calendars: List<CalendarInfo>, chosen: Set<String>?): List<Long>? =
        chosen?.let { keys -> calendars.filter { it.key in keys }.map { it.id } }

    /**
     * The choice after one checkbox changes.
     *
     * The first change turns "every calendar" into an explicit set of everything
     * currently present, so unticking one leaves the rest as they were. Keys for
     * calendars not present right now are kept rather than pruned: an account mid-sync
     * can briefly list nothing, and a ticked calendar must still be ticked when it
     * comes back.
     */
    fun toggle(
        calendars: List<CalendarInfo>,
        chosen: Set<String>?,
        key: String,
        send: Boolean,
    ): Set<String> {
        val base = chosen ?: calendars.mapTo(mutableSetOf()) { it.key }
        return if (send) base + key else base - key
    }

    /** Unit separator: cannot occur in an account name or a server id in practice. */
    private const val SEP = '\u001F'
}
