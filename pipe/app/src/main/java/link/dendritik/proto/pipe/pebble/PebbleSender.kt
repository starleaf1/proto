package link.dendritik.proto.pipe.pebble

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import com.getpebble.android.kit.Constants
import com.getpebble.android.kit.PebbleKit
import com.getpebble.android.kit.util.PebbleDictionary
import link.dendritik.proto.pipe.protocol.IconState
import link.dendritik.proto.pipe.protocol.PhoneState
import link.dendritik.proto.pipe.protocol.Protocol

/**
 * Pushes [IconState] to the watch, obeying the two delivery rules in
 * `docs/protocol.md`: send on change rather than on a timer, and coalesce to the
 * latest value instead of queueing every intermediate one.
 *
 * Coalescing matters more than it looks. Dismissing a stack of chats produces one
 * `onNotificationRemoved` per notification within a few milliseconds; without the
 * debounce that is one Bluetooth round trip each, all but the last already stale.
 *
 * The one exception to send-on-change is the **heartbeat**. The watch hides its icon
 * row when it stops hearing from us, because a companion that has crashed or lost
 * notification access is otherwise indistinguishable from one with no news. So when
 * nothing has changed for a while we re-send the current state anyway, purely as
 * proof of life, at the cadence declared in [Protocol.KEY_HEARTBEAT]. Every message
 * carries that key, so ordinary traffic doubles as a heartbeat and the explicit one
 * only fires during genuine silence.
 *
 * Not thread-safe by design — every caller is the listener's main thread.
 */
class PebbleSender(private val context: Context) {

    private val handler = Handler(Looper.getMainLooper())
    private val alarms = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
    private var pending: IconState = IconState.IDLE
    private var lastSent: IconState? = null
    private var linkReceiver: BroadcastReceiver? = null
    private var beatReceiver: BroadcastReceiver? = null

    private val flush = Runnable { transmit() }
    private val beat = Runnable { transmit(force = true) }

    /** Note the newest state; it reaches the watch once the burst settles. */
    fun submit(state: IconState) {
        pending = state
        handler.removeCallbacks(flush)
        handler.postDelayed(flush, DEBOUNCE_MS)
    }

    /**
     * Start listening for the watch coming and going, and for our own heartbeat alarm.
     *
     * The watchface holds no persistent state — every count resets to zero when it
     * launches or the watch reboots — so a reconnect means whatever we last sent is
     * gone and must be re-sent even though it has not changed. A disconnect means the
     * opposite: stand down completely, because the watch detects Bluetooth loss by
     * itself and has already hidden the row.
     */
    fun start() {
        if (linkReceiver != null) return

        val link = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (intent.action == Constants.INTENT_PEBBLE_DISCONNECTED) {
                    Log.i(TAG, "watch disconnected; standing down")
                    cancelHeartbeat()
                    lastSent = null
                } else {
                    Log.i(TAG, "watch connected; re-sending current state")
                    lastSent = null      // force a send even if nothing changed
                    submit(pending)
                }
            }
        }
        val linkFilter = IntentFilter(Constants.INTENT_PEBBLE_CONNECTED).apply {
            addAction(Constants.INTENT_PEBBLE_DISCONNECTED)
        }
        // PebbleKit's own registerPebbleConnectedReceiver() predates Android 14 and
        // omits the exported flag, which is a hard SecurityException at targetSdk 34+.
        // Register it ourselves: the broadcast comes from the Pebble app, not us.
        register(link, linkFilter, exported = true)
        linkReceiver = link

        // The heartbeat alarm, by contrast, is ours alone — nobody outside this app has
        // any business firing it. Registering it at runtime rather than in the manifest
        // is also the semantically correct choice: a manifest receiver would restart a
        // dead process just to announce that it is alive, which is the one thing the
        // heartbeat must never be able to claim.
        val bt = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) = beat.run()
        }
        register(bt, IntentFilter(ACTION_HEARTBEAT), exported = false)
        beatReceiver = bt
    }

    fun stop() {
        handler.removeCallbacks(flush)
        cancelHeartbeat()
        linkReceiver = unregister(linkReceiver)
        beatReceiver = unregister(beatReceiver)
    }

    /** True if the Pebble app reports a paired, connected watch. */
    fun isWatchConnected(): Boolean = runCatching {
        PebbleKit.isWatchConnected(context)
    }.getOrDefault(false)

    private fun transmit(force: Boolean = false) {
        val state = pending
        if (!force && state == lastSent) return

        // No Bluetooth, no heartbeat. The watch learns about link loss from its own
        // connection service and hides the row without our help, so a proof of life
        // has nothing to prove and nobody to reach. INTENT_PEBBLE_CONNECTED starts
        // us again; until then, treat whatever the watch held as gone.
        if (!isWatchConnected()) {
            cancelHeartbeat()
            lastSent = null
            return
        }

        val period = heartbeatPeriodFor(state)
        val dict = PebbleDictionary().apply {
            // Addressed by numeric id: Android never sees the watch's key names.
            addInt32(Protocol.KEY_UNREAD_COUNT, state.unreadCount.coerceAtLeast(0))
            addInt32(Protocol.KEY_MISSED_COUNT, state.missedCount.coerceAtLeast(0))
            addInt32(Protocol.KEY_PHONE_STATE, state.phone.wire)
            addInt32(Protocol.KEY_HEARTBEAT, period)
        }
        try {
            PebbleKit.sendDataToPebble(context, Protocol.APP_UUID, dict)
            lastSent = state
            Log.i(
                TAG,
                "sent unread=${state.unreadCount} missed=${state.missedCount} " +
                    "phone=${state.phone} next=${period}s",
            )
        } catch (e: IllegalArgumentException) {
            // Leave lastSent alone so the next change retries this value. Still arm the
            // heartbeat: it is now the retry path as well, and the watch's 2.5-period
            // grace absorbs a single miss without blanking anything.
            Log.w(TAG, "send failed, will retry on next change or heartbeat", e)
        }
        scheduleHeartbeat(period)
    }

    /**
     * How long we may stay silent in this state. See [Protocol.HEARTBEAT_LIVE_S] for
     * why a live call is the only state that earns the fast tier.
     */
    private fun heartbeatPeriodFor(state: IconState): Int =
        when (state.phone) {
            PhoneState.ONGOING, PhoneState.RINGING -> Protocol.HEARTBEAT_LIVE_S
            else -> Protocol.HEARTBEAT_IDLE_S
        }

    private fun scheduleHeartbeat(periodS: Int) {
        cancelHeartbeat()
        val delayMs = periodS * 1000L
        if (periodS <= Protocol.HEARTBEAT_LIVE_S) {
            // Fast tier. A plain handler post is free and exact, and it only fails if
            // the CPU suspends — which, during a live call, it does not.
            handler.postDelayed(beat, delayMs)
        } else {
            // Slow tier. setAndAllowWhileIdle is the only scheduler that survives Doze
            // without SCHEDULE_EXACT_ALARM, which Android 14 no longer grants by default
            // and which Play reserves for actual alarm-clock apps. It must be a _WAKEUP
            // alarm: the non-waking variant would sit until the device stirred for some
            // other reason, which can be hours, and the watch would blank meanwhile.
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
     * Kept implicit-but-package-scoped rather than explicit, since the receiver is
     * registered at runtime and so has no component name to target.
     */
    private fun heartbeatIntent(): PendingIntent = PendingIntent.getBroadcast(
        context,
        0,
        Intent(ACTION_HEARTBEAT).setPackage(context.packageName),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    private fun register(receiver: BroadcastReceiver, filter: IntentFilter, exported: Boolean) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val flag = if (exported) Context.RECEIVER_EXPORTED else Context.RECEIVER_NOT_EXPORTED
            context.registerReceiver(receiver, filter, flag)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            context.registerReceiver(receiver, filter)
        }
    }

    private fun unregister(receiver: BroadcastReceiver?): BroadcastReceiver? {
        receiver?.let { runCatching { context.unregisterReceiver(it) } }
        return null
    }

    private companion object {
        const val TAG = "PipeSender"

        /**
         * Long enough to swallow a dismiss-all burst, short enough that the watch
         * still feels immediate.
         */
        const val DEBOUNCE_MS = 250L

        const val ACTION_HEARTBEAT = "link.dendritik.proto.pipe.HEARTBEAT"
    }
}
