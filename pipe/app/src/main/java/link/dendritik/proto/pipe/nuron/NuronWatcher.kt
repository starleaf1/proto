package link.dendritik.proto.pipe.nuron

import android.content.Context
import android.database.ContentObserver
import android.os.Handler
import android.os.Looper

/**
 * Tells us when Nuron's projection has changed.
 *
 * One signal, not two. The calendar needs a broadcast as well as an observer
 * because a sync landing from a server is delivered as
 * `ACTION_PROVIDER_CHANGED` rather than through the provider — but Nuron's
 * projection is only ever rewritten by Nuron's own `SyncWorker`, which calls
 * `notifyChange` at the end of every reconciliation. There is no second path in,
 * so there is no second path to listen on.
 *
 * A `ContentObserver` is also cheaper than the alternative here: an explicit
 * broadcast would need a second permission on Nuron's side and a
 * `RECEIVER_EXPORTED` registration on ours, for a notification the resolver
 * already delivers to holders of the read permission.
 *
 * As with the calendar, this does not fire for the case where nothing changed
 * but the window slid forward. That remains the service's periodic re-scan.
 */
class NuronWatcher(
    private val context: Context,
    private val onChanged: () -> Unit,
) {
    private val handler = Handler(Looper.getMainLooper())
    private var observer: ContentObserver? = null

    fun start() {
        if (observer != null) return
        val obs = object : ContentObserver(handler) {
            override fun onChange(selfChange: Boolean) = onChanged()
        }
        runCatching {
            context.contentResolver.registerContentObserver(
                NuronContract.CONTENT_URI, true, obs,
            )
            observer = obs
        }
    }

    fun stop() {
        observer?.let { runCatching { context.contentResolver.unregisterContentObserver(it) } }
        observer = null
    }
}
