package link.dendritik.proto.pipe

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/**
 * Brings the service back after a reboot.
 *
 * Needed because [PipeService] is now the app's only process keeper. The previous
 * design relied on the system re-binding a notification listener, which happened for
 * free; nothing rebinds a foreground service the user has never opened the app to
 * start.
 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return
        PipeService.start(context)
    }
}
