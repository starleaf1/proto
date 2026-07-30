package link.dendritik.proto.pipe.battery

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import androidx.core.content.ContextCompat

/**
 * The phone's charge level, as a whole percentage.
 *
 * `ACTION_BATTERY_CHANGED` is sticky and fires often — temperature and voltage move
 * constantly — so this reports only when the integer percentage actually changes.
 * Without that filter the watch would see a message every few seconds and the
 * sender's debounce would be doing all the work.
 *
 * The watch owns the threshold. It receives a percentage, which is a fact, and
 * decides for itself what counts as low: that keeps policy about *rendering* on the
 * side that knows what it is rendering onto.
 */
class PhoneBattery(
    private val context: Context,
    private val onChanged: (Int) -> Unit,
) {
    private var receiver: BroadcastReceiver? = null
    var percent: Int = -1
        private set

    fun start() {
        if (receiver != null) return
        val rcv = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) = handle(intent)
        }
        // Exported: this is a system broadcast. The registration also returns the
        // sticky intent, so the current level arrives without waiting for a change.
        val sticky = ContextCompat.registerReceiver(
            context, rcv, IntentFilter(Intent.ACTION_BATTERY_CHANGED),
            ContextCompat.RECEIVER_EXPORTED,
        )
        receiver = rcv
        sticky?.let(::handle)
    }

    fun stop() {
        receiver?.let { runCatching { context.unregisterReceiver(it) } }
        receiver = null
    }

    private fun handle(intent: Intent) {
        val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
        val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
        if (level < 0 || scale <= 0) return
        val next = (level * 100 / scale).coerceIn(0, 100)
        if (next == percent) return
        percent = next
        onChanged(next)
    }
}
