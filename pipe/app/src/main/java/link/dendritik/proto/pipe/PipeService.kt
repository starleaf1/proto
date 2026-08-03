package link.dendritik.proto.pipe

import android.app.AlarmManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.SystemClock
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import link.dendritik.proto.pipe.battery.PhoneBattery
import link.dendritik.proto.pipe.calendar.CalendarSource
import link.dendritik.proto.pipe.calendar.CalendarWatcher
import link.dendritik.proto.pipe.pebble.PebbleSender
import link.dendritik.proto.pipe.protocol.Protocol

/**
 * The process keeper, and the owner of everything with a lifecycle.
 *
 * This is a foreground service because nothing else can hold the process open for
 * what the watch now needs. The previous design piggy-backed on a bound
 * `NotificationListenerService`, which the system kept alive for its own reasons;
 * with the shade no longer being read there is no such host, and calendar
 * observation, the phone's battery and the liveness heartbeat all need to survive
 * the app being closed.
 *
 * Three change signals feed one action — re-scan and reconcile:
 *  - the calendar changed (an edit here, or a sync landing from the server)
 *  - the watch reconnected, which forces a full flush rather than a diff
 *  - time passed, so the six-hour window slid; nothing "changed" but the answer did
 *
 * The third of those is also the liveness heartbeat, and there is exactly one alarm
 * for both — see [tick].
 */
class PipeService : Service() {

    private lateinit var sender: PebbleSender
    private lateinit var calendar: CalendarSource
    private lateinit var watcher: CalendarWatcher
    private lateinit var battery: PhoneBattery

    private val alarms by lazy { getSystemService(Context.ALARM_SERVICE) as AlarmManager }
    private var tickReceiver: BroadcastReceiver? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        startInForeground()

        sender = PebbleSender(applicationContext)
        calendar = CalendarSource(applicationContext)
        battery = PhoneBattery(applicationContext) { sender.submitBattery(it) }
        watcher = CalendarWatcher(applicationContext) { reconcile(flush = false) }

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

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_TICK) tick()
        return START_STICKY
    }

    override fun onDestroy() {
        PipeStatus.running = false
        stopTickAlarm()
        tickReceiver?.let { runCatching { unregisterReceiver(it) } }
        tickReceiver = null
        battery.stop()
        watcher.stop()
        sender.stop()
        super.onDestroy()
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
     * carries [link.dendritik.proto.pipe.protocol.Protocol.KEY_HEARTBEAT]. So the beat
     * is only owed when the re-scan found nothing to say.
     *
     * These were two alarms at the same period, which was worse than merely redundant:
     * Doze throttles `setAndAllowWhileIdle` per *app*, so the pair competed for one
     * budget and made each other late — a heartbeat that could not keep the cadence it
     * declared.
     */
    private fun tick() {
        if (!reconcile(flush = false)) sender.beat()
    }

    // ---------------------------------------------------------------------------
    // Foreground notification
    // ---------------------------------------------------------------------------

    private fun startInForeground() {
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL, "proto pipe", NotificationManager.IMPORTANCE_MIN)
                    .apply { description = "Keeps the watch's calendar in sync." },
            )
        }
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val note: Notification = NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle("proto")
            .setContentText("Syncing your calendar to the watch")
            .setPriority(NotificationCompat.PRIORITY_MIN)
            .setOngoing(true)
            .setShowWhen(false)
            .setContentIntent(open)
            .build()
        ServiceCompat.startForeground(
            this, NOTE_ID, note,
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            } else {
                0
            },
        )
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
            this, rcv, IntentFilter(ACTION_TICK), ContextCompat.RECEIVER_NOT_EXPORTED,
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
        this, 1,
        Intent(ACTION_TICK).setPackage(packageName),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    companion object {
        private const val TAG = "PipeService"
        private const val CHANNEL = "pipe"
        private const val NOTE_ID = 1
        private const val TICK_MS = Protocol.HEARTBEAT_IDLE_S * 1000L
        const val ACTION_TICK = "link.dendritik.proto.pipe.TICK"

        fun start(context: Context) {
            val intent = Intent(context, PipeService::class.java)
            runCatching { ContextCompat.startForegroundService(context, intent) }
                .onFailure { Log.w(TAG, "could not start service", it) }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, PipeService::class.java))
        }
    }
}
