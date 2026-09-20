package link.dendritik.proto.pipe.protocol

import java.util.UUID

/**
 * The wire contract with the watchfaces. Mirrors `docs/protocol.md`; a change here
 * is a change there and in both `package.json`s, in the same commit.
 *
 * Android addresses AppMessage keys by **numeric id** — it never sees the string
 * names the watch's C code uses — so these integers must match
 * `watchface-digital/build/appinfo.json`. Ids are positional from 10000 in the order
 * the names appear in `messageKeys`, so keys are only ever appended. Both faces
 * declare the same array in the same order, which is what lets one set of ids serve
 * them both.
 */
object Protocol {
    /**
     * Every watchface this companion feeds, addressed unconditionally.
     *
     * The firmware keys installed apps by UUID, so two faces cannot share one, and
     * there is no inbound channel — the protocol runs phone → watch only — so the
     * companion has no way to learn which face is on screen. `isWatchConnected()`
     * reports the *Pebble app's* link state and says nothing about the foreground
     * watchapp.
     *
     * Broadcasting to all of them is the answer rather than a limitation: a message
     * addressed to a UUID nothing is running is simply NACKed and dropped, which costs
     * one Bluetooth round trip and no state. Choosing would cost a protocol.
     */
    val APP_UUIDS: List<UUID> = listOf(
        UUID.fromString("f2fc68a6-9636-4694-929b-73c11c33f0e4"),   // watchface-digital
        UUID.fromString("bd9bd299-527e-4236-8206-27be3dc9781c"),   // watchface-analog
    )

    const val KEY_HEARTBEAT = 10000
    const val KEY_CAL_EVENTS = 10001
    const val KEY_CAL_FLAGS = 10002
    const val KEY_NAV_MANEUVER = 10003
    const val KEY_NAV_DISTANCE = 10004
    const val KEY_NAV_UNIT = 10005
    const val KEY_PHONE_BATTERY = 10006

    /** First message of a fresh sync: the watch drops everything it holds. */
    const val CAL_FLUSH = 0x1

    /** More messages belong to this sync; set on all but the last. */
    const val CAL_MORE = 0x2

    /**
     * Seconds until we next expect to check in, sent with every message so the watch
     * can size its own liveness watchdog (it allows 2.5 of these before it declares
     * us gone). Two tiers, because staleness is not equally harmful in every state
     * and Android is not equally willing to wake us in every state.
     *
     * Active navigation is the only state that lies loudly when it goes stale — a
     * phantom "turn right in 250 m" is worse than no instruction — and it is also
     * the one state where the device is definitely interactive, so a plain
     * [android.os.Handler] fires on time and costs nothing.
     *
     * Everything else rides the slow tier. Its period is bounded from below by the
     * platform, not by taste: Doze allows `setAndAllowWhileIdle` to fire "no more than
     * once per nine minutes, per app", which the power-management tables state as seven
     * while-idle alarms an hour. 600 s is six an hour — inside that budget with one
     * alarm of headroom, and comfortably inside the ten an hour the working-set standby
     * bucket allows. Going faster would not be delivered, and an undelivered cadence is
     * worse than a slow one: the watch would raise a companion-down alert every night on
     * a promise the phone cannot keep.
     *
     * A calendar entry is timestamped and ages out on its own, which is a far smaller
     * lie than an alert that flickers.
     *
     * The slow tier has no scheduler of its own, because that throttle counts alarms
     * per *app*: it is the period of the host's one periodic tick, which re-scans the
     * window and sends a bare heartbeat only when the scan had nothing to say. See
     * [PipeEngine.tick][link.dendritik.proto.pipe.PipeEngine.tick].
     *
     * **This is the value the watch is told, always, even while the tick is backing
     * off.** A backed-off period could only be declared by a message that got through,
     * and a message getting through is what ends the backoff — so the base is the only
     * period the watch can coherently be given. When the retries are failing the watch
     * is *supposed* to notice. See [PipeEngine.resync] and `docs/protocol.md`.
     */
    const val HEARTBEAT_LIVE_S = 30
    const val HEARTBEAT_IDLE_S = 600
}

/**
 * A turn instruction's shape. The phone decides which maneuver this is; the watch
 * owns the arrow, because it is the only side that knows whether the display it is
 * drawing on has colour at all.
 *
 * [wire] values are the protocol's, not the ordinals — do not renumber.
 */
enum class Maneuver(val wire: Int) {
    NONE(0),
    STRAIGHT(1),
    LEFT(2),
    RIGHT(3),
    SLIGHT_LEFT(4),
    SLIGHT_RIGHT(5),
    SHARP_LEFT(6),
    SHARP_RIGHT(7),
    UTURN(8),
    ROUNDABOUT(9),
    ARRIVE(10),
}

enum class DistanceUnit(val wire: Int) {
    METRES(0),
    KILOMETRES(1),
    FEET(2),
    MILES(3),
}

/**
 * The next turn.
 *
 * [distanceTenths] is tenths of [unit], not whole units. A watch face has room for
 * about five characters, and "0.3 MI" needs the fraction while "250 M" does not —
 * sending tenths lets the watch decide which of those to render without the phone
 * having to format anything.
 */
data class NavState(
    val maneuver: Maneuver = Maneuver.NONE,
    val distanceTenths: Int = 0,
    val unit: DistanceUnit = DistanceUnit.METRES,
) {
    val active: Boolean get() = maneuver != Maneuver.NONE

    companion object {
        val NONE = NavState()
    }
}

/** What a record in a [KEY_CAL_EVENTS] blob asks the watch to do. */
enum class EventOp(val wire: Int) {
    UPSERT(0),
    REMOVE(1),
}

/**
 * Appointment or point-in-time entry.
 *
 * The distinction is what the watch draws: an appointment spans its duration as a
 * band on the dial, a task or reminder becomes a triangle at the notch it falls
 * nearest. Carried separately from the duration so that a real tasks provider can
 * be added later without changing the wire format — see `docs/protocol.md`.
 */
enum class EventKind(val wire: Int) {
    APPOINTMENT(0),
    TASK(1),
}
