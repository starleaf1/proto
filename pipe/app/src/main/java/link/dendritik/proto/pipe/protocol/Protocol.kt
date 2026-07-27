package link.dendritik.proto.pipe.protocol

import java.util.UUID

/**
 * The wire contract with the watchface. Mirrors `docs/protocol.md`; a change here
 * is a change there and in `watchface/package.json`, in the same commit.
 *
 * Android addresses AppMessage keys by **numeric id** — it never sees the string
 * names the watch's C code uses — so these integers must match
 * `watchface/build/appinfo.json`. Ids are positional from 10000 in the order the
 * names appear in `messageKeys`, so keys are only ever appended.
 */
object Protocol {
    val APP_UUID: UUID = UUID.fromString("f2fc68a6-9636-4694-929b-73c11c33f0e4")

    const val KEY_UNREAD_COUNT = 10000
    const val KEY_MISSED_COUNT = 10001
    const val KEY_PHONE_STATE = 10002
    const val KEY_HEARTBEAT = 10003

    /**
     * Seconds until we next expect to check in, sent with every message so the watch
     * can size its own liveness watchdog (it allows 2.5 of these before blanking the
     * icon row). Two tiers, because staleness is not equally harmful in every state
     * and Android is not equally willing to wake us in every state.
     *
     * A live call is the only state that lies loudly when it goes stale — a phantom
     * ringing handset — and it is also the one state where the device is definitely
     * interactive, so a plain [android.os.Handler] fires on time and costs nothing.
     *
     * Everything else, including a lit envelope, rides the slow tier. It has to: in
     * Doze the system throttles `setAndAllowWhileIdle` to roughly one alarm per 9-15
     * minutes per app, so a nominally faster cadence would not be delivered and the
     * watch would blank a perfectly correct icon every night. A stale unread count is
     * a much smaller lie than a row that flickers.
     */
    const val HEARTBEAT_LIVE_S = 30
    const val HEARTBEAT_IDLE_S = 900
}

/**
 * State of the watch's phone icon. The phone decides this; the watch decides what
 * each value looks like on its own display (three of the seven target platforms
 * are black-and-white, so no colour ever crosses the wire).
 *
 * [wire] values are the protocol's, not the ordinals — do not renumber.
 * [precedence] resolves a phone that is doing several things at once: a call
 * ringing right now matters more than one in progress, which matters more than
 * one already missed.
 */
enum class PhoneState(val wire: Int, val precedence: Int) {
    IDLE(0, 0),
    MISSED(3, 1),
    ONGOING(1, 2),
    RINGING(2, 3),
}

/**
 * Everything the watch renders from the phone, as one snapshot. The sender only
 * transmits when this value differs from the last one the watch acknowledged.
 */
data class IconState(
    val unreadCount: Int = 0,
    val missedCount: Int = 0,
    val phone: PhoneState = PhoneState.IDLE,
) {
    companion object {
        val IDLE = IconState()
    }
}
