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
 */
class PipeService : Service() {

    private lateinit var sender: PebbleSender
    private lateinit var calendar: CalendarSource
    private lateinit var watcher: CalendarWatcher
    private lateinit var battery: PhoneBattery

    private val alarms by lazy { getSystemService(Context.ALARM_SERVICE) as AlarmManager }
    private var rescanReceiver: BroadcastReceiver? = null

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
        startRescanAlarm()

        // A flush, not a diff: we have no idea what the watch is holding at this
        // point, and a watchface that relaunched while we were dead has nothing.
        reconcile(flush = true)
        PipeStatus.running = true
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_RESCAN) reconcile(flush = false)
        return START_STICKY
    }

    override fun onDestroy() {
        PipeStatus.running = false
        stopRescanAlarm()
        rescanReceiver?.let { runCatching { unregisterReceiver(it) } }
        rescanReceiver = null
        battery.stop()
        watcher.stop()
        sender.stop()
        super.onDestroy()
    }

    // ---------------------------------------------------------------------------
    // The one action
    // ---------------------------------------------------------------------------

    private fun reconcile(flush: Boolean) {
        val events = calendar.query(System.currentTimeMillis())
        PipeStatus.eventCount = events.size
        PipeStatus.calendarGranted = calendar.hasPermission()
        PipeStatus.watchConnected = sender.isWatchConnected()
        sender.syncCalendar(events, flush)
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
    // Window re-scan
    // ---------------------------------------------------------------------------

    /**
     * Nothing signals "the window moved", so it is polled.
     *
     * Fifteen minutes matches the practical floor for a background wake-up anyway
     * (see [link.dendritik.proto.pipe.protocol.Protocol.HEARTBEAT_IDLE_S]) and is well
     * inside the resolution the dial can draw, which quantises to twelve minutes.
     */
    private fun startRescanAlarm() {
        if (rescanReceiver != null) return
        val rcv = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                reconcile(flush = false)
                armRescan()   // one-shot, re-armed, so a missed wake-up cannot compound
            }
        }
        ContextCompat.registerReceiver(
            this, rcv, IntentFilter(ACTION_RESCAN), ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        rescanReceiver = rcv
        armRescan()
    }

    private fun armRescan() {
        alarms.setAndAllowWhileIdle(
            AlarmManager.ELAPSED_REALTIME_WAKEUP,
            SystemClock.elapsedRealtime() + RESCAN_MS,
            rescanIntent(),
        )
    }

    private fun stopRescanAlarm() = alarms.cancel(rescanIntent())

    private fun rescanIntent(): PendingIntent = PendingIntent.getBroadcast(
        this, 1,
        Intent(ACTION_RESCAN).setPackage(packageName),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    companion object {
        private const val TAG = "PipeService"
        private const val CHANNEL = "pipe"
        private const val NOTE_ID = 1
        private const val RESCAN_MS = 15 * 60 * 1000L
        const val ACTION_RESCAN = "link.dendritik.proto.pipe.RESCAN"

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
