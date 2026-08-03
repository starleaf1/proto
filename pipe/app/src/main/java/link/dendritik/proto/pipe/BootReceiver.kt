package link.dendritik.proto.pipe

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/**
 * Re-establishes whichever host this device uses.
 *
 * Both need it, for the same underlying reason and in different ways. Nothing rebinds a
 * foreground service the user has never opened the app to start; and a
 * `startObservingDevicePresence` request does not reliably outlive a reboot or an app
 * update, so the companion host has to ask again — after which the system does the rest,
 * binding us the next time the watch is nearby.
 *
 * `MY_PACKAGE_REPLACED` matters as much as `BOOT_COMPLETED` here: an update kills the
 * process and takes the observation request with it, and nothing else would notice.
 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            Intent.ACTION_BOOT_COMPLETED, Intent.ACTION_MY_PACKAGE_REPLACED -> Unit
            else -> return
        }
        when (CompanionHost.hostFor(context)) {
            // Start nothing: asking to be told about the watch is the whole job, and
            // starting a foreground service here would be the notification we are
            // avoiding.
            Host.COMPANION -> CompanionHost.startObserving(context)
            Host.FOREGROUND -> PipeService.start(context)
        }
    }
}
