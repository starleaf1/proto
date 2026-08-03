package link.dendritik.proto.pipe

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.SystemClock
import androidx.core.content.ContextCompat
import link.dendritik.proto.pipe.battery.PhoneBattery
import link.dendritik.proto.pipe.calendar.CalendarSource
import link.dendritik.proto.pipe.calendar.CalendarWatcher
import link.dendritik.proto.pipe.pebble.PebbleSender
import link.dendritik.proto.pipe.protocol.Protocol

/**
 * Everything with a lifecycle, and none of the reasons a process stays alive.
 *
 * The engine does not care which host is holding it open — [PipeService] with a
 * foreground notification, or [PipeCompanionService] which the system binds for as long
 * as the associated watch is nearby. Both do exactly the same two things: construct one
 * of these and call [start], then [stop].
 *
 * Three change signals feed one action — re-scan and reconcile:
 *  - the calendar changed (an edit here, or a sync landing from the server)
 *  - the watch reconnected, which forces a full flush rather than a diff
 *  - time passed, so the six-hour window slid; nothing "changed" but the answer did
 *
 * The third of those is also the liveness heartbeat, and there is exactly one alarm for
 * both — see [tick].
 *
 * Not thread-safe. Every host calls it on the main thread.
 */
class PipeEngine(private val context: Context) {

    private val sender = PebbleSender(context)
    private val calendar = CalendarSource(context)
    private val battery = PhoneBattery(context) { sender.submitBattery(it) }
    private val watcher = CalendarWatcher(context) { reconcile(flush = false) }

    private val alarms = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
    private var tickReceiver: BroadcastReceiver? = null

    /** Idempotent: a host may re-announce a watch it has already told us about. */
    private var started = false

    fun start() {
        if (started) return
        started = true

        sender.onWatchConnected = { reconcile(flush = true) }
        sender.start()
        watcher.start()
        battery.start()
        startTickAlarm()

        // A flush, not a diff: we have no idea what the watch is holding at this
        // point, and a watchface that relaunched while we were dead has nothing.
        reconcile(flush = true)
        PipeStatus.running = true
    }

    fun stop() {
        if (!started) return
        started = false

        PipeStatus.running = false
        stopTickAlarm()
        tickReceiver?.let { runCatching { context.unregisterReceiver(it) } }
        tickReceiver = null
        battery.stop()
        watcher.stop()
        sender.stop()
    }

    // ---------------------------------------------------------------------------
    // The one action
    // ---------------------------------------------------------------------------

    /** Returns whether anything reached the watch. */
    private fun reconcile(flush: Boolean): Boolean {
        val events = calendar.query(System.currentTimeMillis())
        PipeStatus.eventCount = events.size
        PipeStatus.calendarGranted = calendar.hasPermission()
        PipeStatus.watchConnected = sender.isWatchConnected()
        return sender.syncCalendar(events, flush)
    }

    /**
     * The periodic wake-up: one alarm doing both of the jobs that need one.
     *
     * Re-scanning is the obvious one — nothing signals "the window moved". Proving we
     * are alive is the other, and a payload already proves it, because every message
     * carries [Protocol.KEY_HEARTBEAT]. So the beat is only owed when the re-scan found
     * nothing to say.
     *
     * These were two alarms at the same period, which was worse than merely redundant:
     * Doze throttles `setAndAllowWhileIdle` per *app*, so the pair competed for one
     * budget and made each other late — a heartbeat that could not keep the cadence it
     * declared.
     */
    fun tick() {
        if (!reconcile(flush = false)) sender.beat()
    }

    // ---------------------------------------------------------------------------
    // The tick
    // ---------------------------------------------------------------------------

    /**
     * The app's only `AlarmManager` wake-up.
     *
     * Its period is the declared heartbeat cadence, because that is the tighter of the
     * two constraints it serves; for the window it is generous, the dial quantising to
     * twelve minutes. `setAndAllowWhileIdle` is the only scheduler that survives Doze
     * without `SCHEDULE_EXACT_ALARM`, which Android 14 no longer grants by default and
     * which Play reserves for actual alarm-clock apps. It must be a `_WAKEUP` alarm:
     * the non-waking variant would sit until the device stirred for some other reason,
     * which can be hours.
     *
     * The receiver is registered at runtime rather than in the manifest. A manifest
     * receiver would let the system restart a dead process just to announce that it is
     * alive — which is the one thing a liveness heartbeat must never be able to claim.
     */
    private fun startTickAlarm() {
        if (tickReceiver != null) return
        val rcv = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                tick()
                armTick()   // one-shot, re-armed, so a missed wake-up cannot compound
            }
        }
        ContextCompat.registerReceiver(
            context, rcv, IntentFilter(ACTION_TICK), ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        tickReceiver = rcv
        armTick()
    }

    private fun armTick() {
        alarms.setAndAllowWhileIdle(
            AlarmManager.ELAPSED_REALTIME_WAKEUP,
            SystemClock.elapsedRealtime() + TICK_MS,
            tickIntent(),
        )
    }

    private fun stopTickAlarm() = alarms.cancel(tickIntent())

    private fun tickIntent(): PendingIntent = PendingIntent.getBroadcast(
        context, 1,
        Intent(ACTION_TICK).setPackage(context.packageName),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    private companion object {
        const val TICK_MS = Protocol.HEARTBEAT_IDLE_S * 1000L
        const val ACTION_TICK = "link.dendritik.proto.pipe.TICK"
    }
}
