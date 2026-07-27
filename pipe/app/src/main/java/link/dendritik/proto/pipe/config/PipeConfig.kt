package link.dendritik.proto.pipe.config

import link.dendritik.proto.pipe.protocol.PhoneState

/** Which watch icon a notification feeds. */
enum class IconCategory { CHAT, CALL }

/**
 * A package whose name is only known at runtime because the user chooses it.
 * Resolved by `PipeConfig.resolve` and unioned with the rule's static packages,
 * which stay as a fallback for devices where the lookup returns nothing.
 */
enum class DynamicPackage { DEFAULT_SMS, DEFAULT_DIALER }

/**
 * One app's routing rule. Matching is deliberately layered, because no single
 * signal is reliable across vendors and app versions:
 *
 *  1. the notification's channel id against [callChannels] then [chatChannels]
 *     (case-insensitive substring — channel ids are versioned, opaque strings and
 *     `messages_2` must still match `message`),
 *  2. failing that, the notification's own `category`,
 *  3. failing that, [fallback] — left null for apps that post a lot of
 *     non-conversational noise, so an Instagram *like* never lights the envelope.
 *
 * Calls are tested before chats: WhatsApp's call channel must not be swallowed by
 * a broad chat pattern.
 */
data class AppRule(
    val id: String,
    val label: String,
    val packages: Set<String> = emptySet(),
    val dynamic: DynamicPackage? = null,
    val chatChannels: List<String> = emptyList(),
    val callChannels: List<String> = emptyList(),
    val fallback: IconCategory? = null,
)

/**
 * The user-facing configuration. Hard-coded defaults for now — [DEFAULT] is the
 * single place to change them, and the shape is already what a settings screen
 * would persist.
 */
data class PipeConfig(
    val rules: List<AppRule>,
    /** Drop anything the user has silenced. */
    val ignoreSilent: Boolean = true,
    /**
     * Call states that [ignoreSilent] does *not* apply to, because for these the
     * phone icon reports a **state** rather than raising an alert:
     *
     *  - `ONGOING` — every dialler posts its in-call notification on a deliberately
     *    silent low-importance channel, so filtering it would make "green while on
     *    a call" unreachable.
     *  - `MISSED` — a call you missed is worth showing even from a silenced
     *    channel; that is the whole point of a missed-call indicator.
     *
     * `RINGING` is deliberately absent: a ring the user chose to silence is an
     * alert they asked not to receive. Chats are never exempt.
     */
    val silentExemptCallStates: Set<PhoneState> = setOf(PhoneState.ONGOING, PhoneState.MISSED),
) {
    /** The rule owning [packageName], or null if the app is not configured. */
    fun ruleFor(packageName: String): AppRule? =
        rules.firstOrNull { packageName in it.packages }

    /**
     * Fill in [DynamicPackage] entries. Call once at listener startup and again
     * whenever the default SMS or dialer app might have changed.
     */
    fun resolve(lookup: (DynamicPackage) -> String?): PipeConfig = copy(
        rules = rules.map { rule ->
            val extra = rule.dynamic?.let(lookup)
            if (extra == null) rule else rule.copy(packages = rule.packages + extra)
        }
    )

    companion object {
        /**
         * Chat: Telegram, WhatsApp, SMS, Instagram, Facebook Messenger, Email.
         * Calls: the same set, with the default dialler standing in for SMS.
         */
        val DEFAULT = PipeConfig(
            rules = listOf(
                AppRule(
                    id = "telegram",
                    label = "Telegram",
                    packages = setOf(
                        "org.telegram.messenger",
                        "org.telegram.messenger.web",
                        "org.telegram.plus",
                    ),
                    chatChannels = listOf("message", "private", "group", "channel", "chat"),
                    callChannels = listOf("call", "voip"),
                    // Telegram's non-conversational notices ride low-importance
                    // channels, which the silent filter already drops.
                    fallback = IconCategory.CHAT,
                ),
                AppRule(
                    id = "whatsapp",
                    label = "WhatsApp",
                    packages = setOf("com.whatsapp", "com.whatsapp.w4b"),
                    chatChannels = listOf("message", "group"),
                    callChannels = listOf("call", "voip"),
                    fallback = IconCategory.CHAT,
                ),
                AppRule(
                    id = "sms",
                    label = "SMS",
                    dynamic = DynamicPackage.DEFAULT_SMS,
                    packages = setOf(
                        "com.google.android.apps.messaging",
                        "com.android.messaging",
                        "com.samsung.android.messaging",
                    ),
                    chatChannels = listOf("sms", "mms", "message", "incoming"),
                    fallback = IconCategory.CHAT,
                ),
                AppRule(
                    id = "instagram",
                    label = "Instagram",
                    packages = setOf("com.instagram.android"),
                    chatChannels = listOf("direct", "dm"),
                    callChannels = listOf("call", "videochat"),
                    // No fallback: likes, follows and suggestions are not messages.
                    fallback = null,
                ),
                AppRule(
                    id = "messenger",
                    label = "Facebook Messenger",
                    packages = setOf("com.facebook.orca", "com.facebook.mlite"),
                    chatChannels = listOf("message"),
                    callChannels = listOf("call", "rtc", "voip"),
                    fallback = null,
                ),
                AppRule(
                    id = "email",
                    label = "Email",
                    packages = setOf(
                        "com.google.android.gm",
                        "com.microsoft.office.outlook",
                        "ch.protonmail.android",
                        "com.fsck.k9",
                        "net.thunderbird.android",
                        "com.yahoo.mobile.client.android.mail",
                    ),
                    chatChannels = listOf("mail"),
                    callChannels = listOf("call", "meet"),
                    // Gmail's channel ids are per-account and opaque; it is the
                    // notification's `email` category that identifies these.
                    fallback = null,
                ),
                AppRule(
                    id = "dialer",
                    label = "Phone",
                    dynamic = DynamicPackage.DEFAULT_DIALER,
                    packages = setOf(
                        "com.google.android.dialer",
                        "com.android.dialer",
                        "com.android.server.telecom",
                        "com.samsung.android.dialer",
                    ),
                    callChannels = listOf(
                        "call", "incoming", "ongoing", "missed", "phone",
                    ),
                    fallback = IconCategory.CALL,
                ),
            )
        )
    }
}
