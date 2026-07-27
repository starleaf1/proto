package link.dendritik.proto.pipe.pebble

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.getpebble.android.kit.Constants
import com.getpebble.android.kit.PebbleKit
import com.getpebble.android.kit.util.PebbleDictionary
import link.dendritik.proto.pipe.protocol.IconState
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
 * Not thread-safe by design — every caller is the listener's main thread.
 */
class PebbleSender(private val context: Context) {

    private val handler = Handler(Looper.getMainLooper())
    private var pending: IconState = IconState.IDLE
    private var lastSent: IconState? = null
    private var connectReceiver: BroadcastReceiver? = null

    private val flush = Runnable { transmit() }

    /** Note the newest state; it reaches the watch once the burst settles. */
    fun submit(state: IconState) {
        pending = state
        handler.removeCallbacks(flush)
        handler.postDelayed(flush, DEBOUNCE_MS)
    }

    /**
     * Start listening for the watch coming back. The watchface holds no persistent
     * state — every count resets to zero when it launches or the watch reboots — so
     * a reconnect means whatever we last sent is gone and must be re-sent even
     * though it has not changed.
     */
    fun start() {
        if (connectReceiver != null) return
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                Log.i(TAG, "watch connected; re-sending current state")
                lastSent = null          // force a send even if nothing changed
                submit(pending)
            }
        }
        val filter = IntentFilter(Constants.INTENT_PEBBLE_CONNECTED)
        // PebbleKit's own registerPebbleConnectedReceiver() predates Android 14 and
        // omits the exported flag, which is a hard SecurityException at targetSdk 34+.
        // Register it ourselves: the broadcast comes from the Pebble app, not us.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            context.registerReceiver(receiver, filter)
        }
        connectReceiver = receiver
    }

    fun stop() {
        handler.removeCallbacks(flush)
        connectReceiver?.let {
            runCatching { context.unregisterReceiver(it) }
            connectReceiver = null
        }
    }

    /** True if the Pebble app reports a paired, connected watch. */
    fun isWatchConnected(): Boolean = runCatching {
        PebbleKit.isWatchConnected(context)
    }.getOrDefault(false)

    private fun transmit() {
        val state = pending
        if (state == lastSent) return

        val dict = PebbleDictionary().apply {
            // Addressed by numeric id: Android never sees the watch's key names.
            addInt32(Protocol.KEY_UNREAD_COUNT, state.unreadCount.coerceAtLeast(0))
            addInt32(Protocol.KEY_MISSED_COUNT, state.missedCount.coerceAtLeast(0))
            addInt32(Protocol.KEY_PHONE_STATE, state.phone.wire)
        }
        try {
            PebbleKit.sendDataToPebble(context, Protocol.APP_UUID, dict)
            lastSent = state
            Log.i(TAG, "sent unread=${state.unreadCount} missed=${state.missedCount} phone=${state.phone}")
        } catch (e: IllegalArgumentException) {
            // Leave lastSent alone so the next change retries this value.
            Log.w(TAG, "send failed, will retry on next change", e)
        }
    }

    private companion object {
        const val TAG = "PipeSender"

        /**
         * Long enough to swallow a dismiss-all burst, short enough that the watch
         * still feels immediate.
         */
        const val DEBOUNCE_MS = 250L
    }
}
