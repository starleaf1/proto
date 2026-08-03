package link.dendritik.proto.pipe

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat

/**
 * The fallback host: a foreground service, which costs the user a permanent
 * notification.
 *
 * Used where [PipeCompanionService] cannot be — below API 31, or when the watch has not
 * been associated — because something has to hold the process open. Calendar
 * observation, the phone's battery and the periodic tick all need to survive the
 * activity, and with the notification shade no longer being read there is no bound
 * system service to piggy-back on.
 *
 * All the actual work is [PipeEngine]'s. This class is the notification and nothing
 * else. Which host is in use is decided in one place — see [chooseHost].
 */
class PipeService : Service() {

    private lateinit var engine: PipeEngine

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        startInForeground()
        engine = PipeEngine(applicationContext)
        engine.start()
        PipeStatus.host = Host.FOREGROUND
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int =
        START_STICKY

    override fun onDestroy() {
        engine.stop()
        super.onDestroy()
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

    companion object {
        private const val TAG = "PipeService"
        private const val CHANNEL = "pipe"
        private const val NOTE_ID = 1

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
