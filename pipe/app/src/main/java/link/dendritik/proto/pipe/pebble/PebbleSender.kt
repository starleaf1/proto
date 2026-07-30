package link.dendritik.proto.pipe.pebble

import android.app.AlarmManager
import android.app.PendingIntent
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
 * life, at the cadence declared in [Protocol.KEY_HEARTBEAT]. Every message carries
 * that key, so ordinary traffic doubles as a heartbeat and the explicit one only
 * fires during genuine silence.
 *
 * Not thread-safe: every caller is [link.dendritik.proto.pipe.PipeService] on the
 * main thread.
 */
class PebbleSender(private val context: Context) {

    private val handler = Handler(Looper.getMainLooper())
    private val alarms = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager

    /** Called when the watch reconnects, so the host can order a full re-sync. */
    var onWatchConnected: (() -> Unit)? = null

    private var nav: NavState = NavState.NONE
    private var sentNav: NavState? = null
    private var battery: Int = -1
    private var sentBattery: Int? = null

    /** What we believe the watch's event table holds, so a scan can be diffed. */
    private var sentEvents: Map<Int, EventFacts> = emptyMap()

    private var linkReceiver: BroadcastReceiver? = null
    private var beatReceiver: BroadcastReceiver? = null

    private val flushScalars = Runnable { transmitScalars(force = false) }
    private val beat = Runnable { transmitScalars(force = true) }

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------

    fun start() {
        if (linkReceiver != null) return

        val link = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (intent.action == Constants.INTENT_PEBBLE_DISCONNECTED) {
                    Log.i(TAG, "watch disconnected; standing down")
                    cancelHeartbeat()
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

        val bt = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) = beat.run()
        }
        ContextCompat.registerReceiver(
            context, bt, IntentFilter(ACTION_HEARTBEAT), ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        beatReceiver = bt
    }

    fun stop() {
        handler.removeCallbacks(flushScalars)
        cancelHeartbeat()
        linkReceiver = unregister(linkReceiver)
        beatReceiver = unregister(beatReceiver)
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
     * Reconciles the watch's event table with [events].
     *
     * With [flush] the watch is told to drop everything first and the whole scan goes
     * over — that is the reconnect path, and the only one that recovers from a
     * watchface relaunch. Otherwise only the difference is sent.
     */
    fun syncCalendar(events: List<EventFacts>, flush: Boolean) {
        if (!isWatchConnected()) {
            cancelHeartbeat()
            return
        }
        val records = if (flush) {
            events.map { EventBlob.Record(it, EventOp.UPSERT) }
        } else {
            EventDiff.diff(sentEvents, events)
        }
        if (records.isEmpty() && !flush) return

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
            if (!send(dict)) return
        }
        sentEvents = events.associateBy { it.id }
        Log.i(
            TAG,
            "calendar ${if (flush) "flush" else "delta"}: " +
                "${records.size} record(s) in ${chunks.size} message(s)",
        )
        scheduleHeartbeat(period)
    }

    /** Note the newest scalar state; it reaches the watch once the burst settles. */
    private fun schedule() {
        handler.removeCallbacks(flushScalars)
        handler.postDelayed(flushScalars, DEBOUNCE_MS)
    }

    private fun transmitScalars(force: Boolean) {
        if (!isWatchConnected()) {
            cancelHeartbeat()
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
        scheduleHeartbeat(period)
    }

    private fun send(dict: PebbleDictionary): Boolean = try {
        PebbleKit.sendDataToPebble(context, Protocol.APP_UUID, dict)
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

    private fun scheduleHeartbeat(periodS: Int) {
        cancelHeartbeat()
        val delayMs = periodS * 1000L
        if (periodS <= Protocol.HEARTBEAT_LIVE_S) {
            // Fast tier. A plain handler post is free and exact, and it only fails if
            // the CPU suspends — which, mid-navigation, it does not.
            handler.postDelayed(beat, delayMs)
        } else {
            // Slow tier. setAndAllowWhileIdle is the only scheduler that survives Doze
            // without SCHEDULE_EXACT_ALARM, which Android 14 no longer grants by
            // default and which Play reserves for actual alarm-clock apps. It must be
            // a _WAKEUP alarm: the non-waking variant would sit until the device
            // stirred for some other reason, which can be hours.
            alarms.setAndAllowWhileIdle(
                AlarmManager.ELAPSED_REALTIME_WAKEUP,
                SystemClock.elapsedRealtime() + delayMs,
                heartbeatIntent(),
            )
        }
    }

    private fun cancelHeartbeat() {
        handler.removeCallbacks(beat)
        alarms.cancel(heartbeatIntent())
    }

    /**
     * Implicit but package-scoped: the receiver is registered at runtime and so has no
     * component name to target. Deliberately not a manifest receiver — that would
     * restart a dead process just to announce that it is alive, which is the one thing
     * a heartbeat must never be able to claim.
     */
    private fun heartbeatIntent(): PendingIntent = PendingIntent.getBroadcast(
        context, 0,
        Intent(ACTION_HEARTBEAT).setPackage(context.packageName),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    private fun unregister(receiver: BroadcastReceiver?): BroadcastReceiver? {
        receiver?.let { runCatching { context.unregisterReceiver(it) } }
        return null
    }

    private companion object {
        const val TAG = "PebbleSender"
        const val DEBOUNCE_MS = 250L
        const val ACTION_HEARTBEAT = "link.dendritik.proto.pipe.HEARTBEAT"
    }
}
