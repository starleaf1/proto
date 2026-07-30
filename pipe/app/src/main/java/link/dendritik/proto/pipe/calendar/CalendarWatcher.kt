package link.dendritik.proto.pipe.calendar

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.os.Handler
import android.os.Looper
import android.provider.CalendarContract
import androidx.core.content.ContextCompat

/**
 * Tells us when the calendar has changed.
 *
 * Two signals, because neither alone is enough. The [ContentObserver] catches an
 * edit made on the device, which is the common case and arrives immediately.
 * `ACTION_PROVIDER_CHANGED` catches a sync landing from the server, which is how
 * an entry created on a laptop shows up.
 *
 * Neither fires for the third case: nothing changed, but time passed and the
 * six-hour window slid forward. That one is the service's periodic re-scan.
 */
class CalendarWatcher(
    private val context: Context,
    private val onChanged: () -> Unit,
) {
    private val handler = Handler(Looper.getMainLooper())
    private var observer: ContentObserver? = null
    private var receiver: BroadcastReceiver? = null

    fun start() {
        if (observer != null) return

        val obs = object : ContentObserver(handler) {
            override fun onChange(selfChange: Boolean) = onChanged()
        }
        context.contentResolver.registerContentObserver(
            CalendarContract.CONTENT_URI, true, obs,
        )
        observer = obs

        val rcv = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) = onChanged()
        }
        val filter = IntentFilter(Intent.ACTION_PROVIDER_CHANGED).apply {
            addDataScheme("content")
            addDataAuthority(CalendarContract.AUTHORITY, null)
        }
        // Exported: the broadcast comes from the calendar provider, not from us.
        ContextCompat.registerReceiver(context, rcv, filter, ContextCompat.RECEIVER_EXPORTED)
        receiver = rcv
    }

    fun stop() {
        observer?.let { context.contentResolver.unregisterContentObserver(it) }
        observer = null
        receiver?.let { runCatching { context.unregisterReceiver(it) } }
        receiver = null
    }
}
