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
import androidx.annotation.RequiresApi
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat

/**
 * Android 15. Where `dataSync` acquired a six-hour budget per 24 hours, an `onTimeout`
 * callback when it runs out, and a refusal to be launched from `BOOT_COMPLETED` at all.
 *
 * Only apps *targeting* 15 or higher are subject to it, and this one targets 36, so the
 * whole of this applies from here up. Note what that implies about who it can affect:
 * API 35 is well past [MIN_COMPANION_SDK], so every device that can hit the cap could
 * also have been on the companion host, which has no cap. The population is exactly the
 * users who declined the pairing dialog — which is why the diagnostics screen answers a
 * timeout by pointing at that dialog.
 */
const val FGS_TIMEOUT_SDK = 35

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
 * All the actual work is [PipeEngine]'s. This class is the notification, and — since
 * Android 15 — the three ways the platform can take that notification away. Which host
 * is in use is decided in one place — see [chooseHost].
 */
class PipeService : Service() {

    private var engine: PipeEngine? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        if (!startInForeground()) {
            // Nothing to hold open, and holding a plain background service open is not a
            // thing Android permits. Stand down and let the diagnostics screen say why.
            PipeStatus.capped = true
            stopSelf()
            return
        }
        PipeStatus.capped = false
        engine = PipeEngine(applicationContext)
        engine?.start()
        PipeStatus.host = Host.FOREGROUND
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int =
        START_STICKY

    override fun onDestroy() {
        engine?.stop()
        engine = null
        super.onDestroy()
    }

    /**
     * Android 15+: the six hours are up.
     *
     * We have "a few seconds" to call [stopSelf] and the system throws
     * `RemoteServiceException: "A foreground service of type dataSync did not stop within
     * its timeout"` if we do not, so this does exactly one thing and does it first.
     * By the time it is called we are already not a foreground service.
     *
     * The two-argument overload, deliberately: the one-argument `onTimeout(int)` is
     * Android 14's and the platform calls it only for `shortService`, which this is not.
     * Overriding that one instead would compile, read as careful, and never fire.
     *
     * Recovery is the user opening the app, which is the documented way the budget
     * resets — `MainActivity.onResume` already calls `maybeStart` on every resume, so the
     * host comes back on its own the next time they look at it. Nothing is re-armed from
     * here: the budget is spent, and a service that immediately restarted would be
     * refused, or would burn what little is left in a loop.
     */
    @RequiresApi(FGS_TIMEOUT_SDK)
    override fun onTimeout(startId: Int, fgsType: Int) {
        Log.w(TAG, "dataSync budget exhausted (type $fgsType); standing down")
        PipeStatus.capped = true
        stopSelf()
    }

    // ---------------------------------------------------------------------------
    // Foreground notification
    // ---------------------------------------------------------------------------

    /** False when the platform refused to make us a foreground service. */
    private fun startInForeground(): Boolean {
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
        // On Android 15+ this throws `ForegroundServiceStartNotAllowedException` — an
        // `IllegalStateException` — when the dataSync budget is already spent, with
        // "Time limit already exhausted for foreground service type dataSync". It is
        // reached from `BootReceiver` and from a system restart of a START_STICKY
        // service, neither of which is the user in the app, so it cannot be assumed
        // away: uncaught here it would take `onCreate` down with it.
        return runCatching {
            ServiceCompat.startForeground(
                this, NOTE_ID, note,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
                } else {
                    0
                },
            )
        }.onFailure { Log.w(TAG, "the platform would not let us go foreground", it) }
            .isSuccess
    }

    companion object {
        private const val TAG = "PipeService"
        private const val CHANNEL = "pipe"
        private const val NOTE_ID = 1

        fun start(context: Context) {
            val intent = Intent(context, PipeService::class.java)
            runCatching { ContextCompat.startForegroundService(context, intent) }
                .onFailure {
                    Log.w(TAG, "could not start service", it)
                    PipeStatus.capped = true
                }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, PipeService::class.java))
        }
    }
}
