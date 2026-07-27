package link.dendritik.proto.pipe.notify

import android.app.Notification
import android.app.NotificationManager
import link.dendritik.proto.pipe.config.PipeConfig
import link.dendritik.proto.pipe.protocol.PhoneState
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The routing decision table. [Classifier] is pure, so every case here is a plain
 * JVM test — no Robolectric, no device.
 */
class ClassifierTest {

    private val config = PipeConfig.DEFAULT

    /** Defaults describe an ordinary, alerting notification. */
    private fun facts(
        pkg: String,
        channel: String? = null,
        category: String? = null,
        importance: Int? = NotificationManager.IMPORTANCE_DEFAULT,
        priority: Int = Notification.PRIORITY_DEFAULT,
        ongoing: Boolean = false,
        clearable: Boolean = true,
        groupSummary: Boolean = false,
        fullScreen: Boolean = false,
        actions: List<String> = emptyList(),
        callType: Int? = null,
    ) = NotificationFacts(
        key = "0|$pkg|1|null|0",
        packageName = pkg,
        channelId = channel,
        category = category,
        importance = importance,
        priority = priority,
        isOngoing = ongoing,
        isClearable = clearable,
        isGroupSummary = groupSummary,
        hasFullScreenIntent = fullScreen,
        actionTitles = actions,
        callType = callType,
    )

    private fun classify(f: NotificationFacts) = Classifier.classify(f, config)

    // --- the envelope ------------------------------------------------------

    @Test
    fun `configured chat apps light the envelope`() {
        val cases = mapOf(
            "org.telegram.messenger" to "private_messages",
            "com.whatsapp" to "messages_1",
            "com.google.android.apps.messaging" to "sms_received",
            "com.instagram.android" to "direct_v2_message",
            "com.facebook.orca" to "messages_group",
        )
        cases.forEach { (pkg, channel) ->
            assertEquals("$pkg/$channel", Verdict.Chat, classify(facts(pkg, channel)))
        }
    }

    @Test
    fun `gmail is routed by its email category since its channel ids are opaque`() {
        val f = facts("com.google.android.gm", channel = "^p^acct1", category = Notification.CATEGORY_EMAIL)
        assertEquals(Verdict.Chat, classify(f))
    }

    @Test
    fun `unconfigured apps are ignored`() {
        assertEquals(Verdict.Ignore, classify(facts("com.spotify.music", "playback")))
    }

    @Test
    fun `instagram social noise does not light the envelope`() {
        // No fallback for Instagram: a like is not a chat message.
        assertEquals(Verdict.Ignore, classify(facts("com.instagram.android", "ig_other_likes")))
        assertEquals(Verdict.Ignore, classify(facts("com.facebook.orca", "other_notifications")))
    }

    @Test
    fun `group summaries are ignored so their children are not double counted`() {
        val f = facts("com.whatsapp", "messages", groupSummary = true)
        assertEquals(Verdict.Ignore, classify(f))
    }

    // --- the silent rule ---------------------------------------------------

    @Test
    fun `silenced channels are never forwarded`() {
        listOf(NotificationManager.IMPORTANCE_LOW, NotificationManager.IMPORTANCE_MIN)
            .forEach { importance ->
                val f = facts("com.whatsapp", "silent_messages", importance = importance)
                assertEquals(Verdict.Ignore, classify(f))
            }
    }

    @Test
    fun `below API 26 silence falls back to priority`() {
        val f = facts("com.whatsapp", channel = null, category = Notification.CATEGORY_MESSAGE,
            importance = null, priority = Notification.PRIORITY_LOW)
        assertEquals(Verdict.Ignore, classify(f))

        val alerting = f.copy(priority = Notification.PRIORITY_HIGH)
        assertEquals(Verdict.Chat, classify(alerting))
    }

    @Test
    fun `a call in progress survives the silent rule`() {
        // Every dialler posts the in-call notification on a deliberately silent
        // low-importance channel. Dropping it would make green unreachable.
        val f = facts(
            "com.google.android.dialer",
            channel = "phone_ongoing_call",
            importance = NotificationManager.IMPORTANCE_LOW,
            ongoing = true,
            clearable = false,
        )
        assertEquals(Verdict.Call(PhoneState.ONGOING), classify(f))
    }

    @Test
    fun `a silent missed call still lights red`() {
        // A call you missed is worth showing even from a silenced channel — that is
        // what a missed-call indicator is for.
        listOf(NotificationManager.IMPORTANCE_LOW, NotificationManager.IMPORTANCE_MIN)
            .forEach { importance ->
                val f = facts(
                    "com.google.android.dialer",
                    channel = "phone_missed_call",
                    importance = importance,
                )
                assertEquals(Verdict.Call(PhoneState.MISSED), classify(f))
            }
    }

    @Test
    fun `a silenced ring is dropped, unlike a silent missed call`() {
        // A ring the user chose to silence is an alert they asked not to receive.
        val f = facts(
            "com.google.android.dialer",
            channel = "phone_incoming_call",
            importance = NotificationManager.IMPORTANCE_LOW,
            callType = 1,
        )
        assertEquals(Verdict.Ignore, classify(f))
    }

    @Test
    fun `silent exemptions are configurable`() {
        val strict = config.copy(silentExemptCallStates = emptySet())
        val f = facts(
            "com.google.android.dialer",
            channel = "phone_missed_call",
            importance = NotificationManager.IMPORTANCE_MIN,
        )
        assertEquals(Verdict.Ignore, Classifier.classify(f, strict))
    }

    // --- the phone icon ----------------------------------------------------

    @Test
    fun `missed calls are red`() {
        assertEquals(
            Verdict.Call(PhoneState.MISSED),
            classify(facts("com.google.android.dialer", "phone_missed_call")),
        )
        assertEquals(
            Verdict.Call(PhoneState.MISSED),
            classify(facts("com.whatsapp", "calls", category = Notification.CATEGORY_MISSED_CALL)),
        )
    }

    @Test
    fun `an incoming call rings`() {
        // CallStyle, the modern signal.
        assertEquals(
            Verdict.Call(PhoneState.RINGING),
            classify(facts("com.google.android.dialer", "phone_incoming_call", callType = 1)),
        )
        // A full-screen intent, for apps that set no call type.
        assertEquals(
            Verdict.Call(PhoneState.RINGING),
            classify(facts("com.whatsapp", "calls", fullScreen = true, ongoing = true)),
        )
        // Answer + decline buttons, the last resort.
        assertEquals(
            Verdict.Call(PhoneState.RINGING),
            classify(facts("org.telegram.messenger", "calls", actions = listOf("answer", "decline"))),
        )
    }

    @Test
    fun `an ongoing call is green`() {
        assertEquals(
            Verdict.Call(PhoneState.ONGOING),
            classify(facts("com.google.android.dialer", "phone_ongoing_call", callType = 2)),
        )
        // Live, but with nothing to answer.
        assertEquals(
            Verdict.Call(PhoneState.ONGOING),
            classify(facts("com.whatsapp", "calls", ongoing = true, clearable = false)),
        )
    }

    @Test
    fun `a hangup-only action set is not treated as ringing`() {
        // "Hang up" alone means the call is already connected; it takes both an
        // accept and a reject for the notification to be an incoming ring.
        val f = facts("com.whatsapp", "calls", ongoing = true, actions = listOf("hang up", "mute"))
        assertEquals(Verdict.Call(PhoneState.ONGOING), classify(f))
    }

    @Test
    fun `a call channel is not swallowed by a broad chat pattern`() {
        // Instagram's chat patterns include "direct"; its video-call channel must
        // still reach the phone icon, because calls are matched first.
        val f = facts("com.instagram.android", "videochat_incoming", fullScreen = true)
        assertEquals(Verdict.Call(PhoneState.RINGING), classify(f))
    }

    @Test
    fun `sms is never routed to the phone icon`() {
        // The SMS rule carries no call channels, and the dialler rule owns calls.
        val f = facts("com.google.android.apps.messaging", "sms_received")
        assertEquals(Verdict.Chat, classify(f))
    }

    // --- runtime package resolution ---------------------------------------

    @Test
    fun `the resolved default sms and dialler apps are matched`() {
        val resolved = config.resolve { which ->
            when (which.name) {
                "DEFAULT_SMS" -> "com.oem.messages"
                else -> "com.oem.phone"
            }
        }
        assertEquals(
            Verdict.Chat,
            Classifier.classify(facts("com.oem.messages", "sms"), resolved),
        )
        assertEquals(
            Verdict.Call(PhoneState.MISSED),
            Classifier.classify(facts("com.oem.phone", "missed_call"), resolved),
        )
        // Static fallbacks keep working alongside the resolved ones.
        assertEquals(
            Verdict.Chat,
            Classifier.classify(facts("com.google.android.apps.messaging", "sms"), resolved),
        )
    }
}
