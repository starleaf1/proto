package link.dendritik.proto.pipe.notify

/**
 * Everything the classifier needs from one posted notification, flattened out of
 * `StatusBarNotification` at the edge of the app.
 *
 * The point of this type is that it holds no Android classes, so every routing
 * decision below it is a pure function over plain data and testable on the JVM.
 * Nothing here is notification *content* — no title, no text, no sender. Only the
 * metadata needed to pick an icon ever leaves the listener.
 */
data class NotificationFacts(
    /** `StatusBarNotification.getKey()` — the system's stable identity for dedup. */
    val key: String,
    val packageName: String,
    /** Null below API 26, where channels do not exist. */
    val channelId: String? = null,
    /** `Notification.category`, e.g. `msg`, `call`, `missed_call`, `email`. */
    val category: String? = null,
    /** Channel importance from the ranking; null below API 26. */
    val importance: Int? = null,
    /** `Notification.priority` — the pre-channel signal, still set by many apps. */
    val priority: Int = 0,
    val isOngoing: Boolean = false,
    val isClearable: Boolean = true,
    val isGroupSummary: Boolean = false,
    val hasFullScreenIntent: Boolean = false,
    /** Lowercased action labels — "answer"/"decline" mark a ringing call. */
    val actionTitles: List<String> = emptyList(),
    /** `Notification.EXTRA_CALL_TYPE` from a CallStyle notification; API 31+. */
    val callType: Int? = null,
)
