package link.dendritik.proto.pipe.pebble

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import androidx.core.content.ContextCompat
import com.getpebble.android.kit.Constants
import com.getpebble.android.kit.PebbleKit
import com.getpebble.android.kit.util.PebbleDictionary
import link.dendritik.proto.pipe.calendar.EventFacts
import link.dendritik.proto.pipe.protocol.EventOp
import link.dendritik.proto.pipe.protocol.NavState
import link.dendritik.proto.pipe.protocol.Protocol

/**
 * Everything that reaches the watch goes through here.
 *
 * Send on change, never on a timer — with one exception. The watch raises a
 * companion-down alert when it stops hearing from us, because a companion that has
 * crashed or lost its permissions is otherwise indistinguishable from one with no
 * news. So when nothing has changed for a while we speak anyway, purely as proof of
 * life, at the cadence declared in [Protocol.KEY_HEARTBEAT].
 *
 * **A payload is a life sign.** Every message carries that key, so this class never
 * schedules a beat of its own on the slow tier — the host's periodic tick already
 * wakes us to re-scan the window, and [beat] is what it calls when that scan turned
 * out to have nothing to say. One wake-up, one message. The fast navigation tier is
 * the exception: 30 s is far below any tick a host can promise, so it posts to
 * [handler], which is free and exact in the one state where the device is definitely
 * interactive.
 *
 * Not thread-safe: every caller is the host on the main thread.
 */
class PebbleSender(private val context: Context) {

    private val handler = Handler(Looper.getMainLooper())

    /** Called when the watch reconnects, so the host can order a full re-sync. */
    var onWatchConnected: (() -> Unit)? = null

    private var nav: NavState = NavState.NONE
    private var sentNav: NavState? = null
    private var battery: Int = -1
    private var sentBattery: Int? = null

    /** What we believe the watch's event table holds, so a scan can be diffed. */
    private var sentEvents: Map<Int, EventFacts> = emptyMap()

    /** When we last got a message out, so [beat] can tell whether one is owed. */
    private var lastSentAtMs = 0L

    private var linkReceiver: BroadcastReceiver? = null

    private val flushScalars = Runnable { transmitScalars(force = false) }
    private val fastBeat = Runnable { transmitScalars(force = true) }

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------

    fun start() {
        if (linkReceiver != null) return

        val link = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (intent.action == Constants.INTENT_PEBBLE_DISCONNECTED) {
                    Log.i(TAG, "watch disconnected; standing down")
                    cancelFastBeat()
                    sentNav = null
                    sentBattery = null
                    // Forget what the watch holds. It persists nothing across a
                    // watchface relaunch, so on reconnect the only correct move is a
                    // full flush; pretending we still know its table would send a
                    // diff against a phantom.
                    sentEvents = emptyMap()
                } else {
                    Log.i(TAG, "watch connected; re-syncing")
                    onWatchConnected?.invoke()
                }
            }
        }
        val filter = IntentFilter(Constants.INTENT_PEBBLE_CONNECTED).apply {
            addAction(Constants.INTENT_PEBBLE_DISCONNECTED)
        }
        // PebbleKit's own registerPebbleConnectedReceiver() predates Android 14 and
        // omits the exported flag, which is a hard SecurityException at targetSdk 34+.
        // Register it ourselves: the broadcast comes from the Pebble app, not us.
        ContextCompat.registerReceiver(context, link, filter, ContextCompat.RECEIVER_EXPORTED)
        linkReceiver = link
    }

    fun stop() {
        handler.removeCallbacks(flushScalars)
        cancelFastBeat()
        linkReceiver = unregister(linkReceiver)
    }

    /** True if the Pebble app reports a paired, connected watch. */
    fun isWatchConnected(): Boolean =
        runCatching { PebbleKit.isWatchConnected(context) }.getOrDefault(false)

    // ---------------------------------------------------------------------------
    // Submissions
    // ---------------------------------------------------------------------------

    fun submitNav(next: NavState) {
        nav = next
        schedule()
    }

    fun submitBattery(percent: Int) {
        battery = percent
        schedule()
    }

    /**
     * Speak with nothing new to say, as proof of life.
     *
     * Called by the host's periodic tick when the re-scan it just did produced no
     * calendar traffic — a tick that sent a delta has already proven we are alive, and
     * a second message would say the same thing twice.
     *
     * The guard covers the near miss: a real change sent seconds *before* the tick
     * also already proved it, and the watch resets its watchdog on any message
     * arriving, not on the `Heartbeat` key specifically. It is deliberately a small
     * window rather than a full period. Suppressing for a whole period could put two
     * periods between messages — the tick cadence is fixed, so a beat skipped at
     * `k × 900 s` moves the next one to `(k+1) × 900 s` — and 1800 s plus a Doze-slipped
     * tick would cross the watch's 2.5-period grace and raise a false alert. With the
     * guard the worst case is 900 s + 60 s against a 2250 s grace.
     */
    fun beat() {
        val silentFor = SystemClock.elapsedRealtime() - lastSentAtMs
        if (lastSentAtMs != 0L && silentFor < BEAT_GUARD_MS) {
            Log.i(TAG, "beat skipped; spoke ${silentFor / 1000}s ago")
            return
        }
        transmitScalars(force = true)
    }

    /**
     * Reconciles the watch's event table with [events].
     *
     * With [flush] the watch is told to drop everything first and the whole scan goes
     * over — that is the reconnect path, and the only one that recovers from a
     * watchface relaunch. Otherwise only the difference is sent.
     *
     * Returns whether anything actually reached the watch, so a caller driving the
     * periodic tick knows whether it still owes a [beat].
     */
    fun syncCalendar(events: List<EventFacts>, flush: Boolean): Boolean {
        if (!isWatchConnected()) {
            cancelFastBeat()
            return false
        }
        val records = if (flush) {
            events.map { EventBlob.Record(it, EventOp.UPSERT) }
        } else {
            EventDiff.diff(sentEvents, events)
        }
        if (records.isEmpty() && !flush) return false

        val chunks = EventBlob.chunk(records)
        val period = heartbeatPeriod()
        chunks.forEachIndexed { i, blob ->
            var flags = 0
            if (flush && i == 0) flags = flags or Protocol.CAL_FLUSH
            if (i < chunks.lastIndex) flags = flags or Protocol.CAL_MORE
            val dict = PebbleDictionary().apply {
                addBytes(Protocol.KEY_CAL_EVENTS, blob)
                addInt32(Protocol.KEY_CAL_FLAGS, flags)
                addInt32(Protocol.KEY_HEARTBEAT, period)
            }
            // Bail on the first failure rather than pressing on: the remaining chunks
            // belong to a sync the watch will never see the start of, and leaving
            // sentEvents untouched makes the next pass redo the whole thing.
            if (!send(dict)) return false
        }
        sentEvents = events.associateBy { it.id }
        Log.i(
            TAG,
            "calendar ${if (flush) "flush" else "delta"}: " +
                "${records.size} record(s) in ${chunks.size} message(s)",
        )
        scheduleFastBeat()
        return true
    }

    /** Note the newest scalar state; it reaches the watch once the burst settles. */
    private fun schedule() {
        handler.removeCallbacks(flushScalars)
        handler.postDelayed(flushScalars, DEBOUNCE_MS)
    }

    private fun transmitScalars(force: Boolean) {
        if (!isWatchConnected()) {
            cancelFastBeat()
            sentNav = null
            sentBattery = null
            return
        }
        // A heartbeat re-sends the current values rather than an empty message, so a
        // beat arriving after a dropped send doubles as the retry.
        if (!force && nav == sentNav && battery == sentBattery) return

        val period = heartbeatPeriod()
        val dict = PebbleDictionary().apply {
            addInt32(Protocol.KEY_NAV_MANEUVER, nav.maneuver.wire)
            addInt32(Protocol.KEY_NAV_DISTANCE, nav.distanceTenths.coerceAtLeast(0))
            addInt32(Protocol.KEY_NAV_UNIT, nav.unit.wire)
            addInt32(Protocol.KEY_PHONE_BATTERY, battery.coerceIn(-1, 100))
            addInt32(Protocol.KEY_HEARTBEAT, period)
        }
        if (send(dict)) {
            sentNav = nav
            sentBattery = battery
        }
        scheduleFastBeat()
    }

    private fun send(dict: PebbleDictionary): Boolean = try {
        PebbleKit.sendDataToPebble(context, Protocol.APP_UUID, dict)
        lastSentAtMs = SystemClock.elapsedRealtime()
        true
    } catch (e: IllegalArgumentException) {
        // What PebbleKit throws for a malformed or oversized dictionary. Nothing is
        // marked as sent: the next change or the next heartbeat retries, and the
        // watch's 2.5-period grace absorbs a single miss without raising an alert.
        Log.w(TAG, "send failed, will retry on next change or heartbeat", e)
        false
    }

    // ---------------------------------------------------------------------------
    // Heartbeat
    // ---------------------------------------------------------------------------

    /**
     * How long we may stay silent. Active navigation is the only state that earns the
     * fast tier — see [Protocol.HEARTBEAT_LIVE_S].
     */
    private fun heartbeatPeriod(): Int =
        if (nav.active) Protocol.HEARTBEAT_LIVE_S else Protocol.HEARTBEAT_IDLE_S

    /**
     * Arms the fast tier's beat, and only the fast tier's.
     *
     * A plain handler post is free and exact, and it only fails if the CPU suspends —
     * which, mid-navigation, it does not. The slow tier deliberately has nothing here:
     * it would need an `AlarmManager` wake-up, the host already owns one at the same
     * period to re-scan the window, and Doze throttles `setAndAllowWhileIdle` per
     * *app* rather than per alarm — so a second alarm would not add a second wake-up,
     * it would make both of them late.
     */
    private fun scheduleFastBeat() {
        cancelFastBeat()
        if (!nav.active) return
        handler.postDelayed(fastBeat, Protocol.HEARTBEAT_LIVE_S * 1000L)
    }

    private fun cancelFastBeat() = handler.removeCallbacks(fastBeat)

    private fun unregister(receiver: BroadcastReceiver?): BroadcastReceiver? {
        receiver?.let { runCatching { context.unregisterReceiver(it) } }
        return null
    }

    private companion object {
        const val TAG = "PebbleSender"
        const val DEBOUNCE_MS = 250L

        /** How recently a payload counts as having already served as the beat. */
        const val BEAT_GUARD_MS = 60_000L
    }
}
