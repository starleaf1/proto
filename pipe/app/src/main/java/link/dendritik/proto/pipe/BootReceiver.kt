package link.dendritik.proto.pipe

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log

/**
 * Whether a `dataSync` foreground service may be launched in response to [action].
 *
 * Pure, and unit-tested, for the reason [chooseHost] is: the wrong answer is invisible
 * from this side, and wrong in both directions. Say yes where Android would refuse and
 * the throw lands in `PipeService.start`'s `runCatching`, leaving a log line and a
 * companion that never came back; say no where it would have worked and an app update
 * quietly stops restoring the host on the devices that still need it.
 *
 * Android 15 forbids the `BOOT_COMPLETED` case specifically — refused outright, not
 * throttled or deferred. `MY_PACKAGE_REPLACED` is a different broadcast and is not
 * covered by the rule, which is the whole reason this takes the action rather than just
 * the SDK level.
 */
fun canStartFallbackFrom(sdkInt: Int, action: String?): Boolean =
    sdkInt < FGS_TIMEOUT_SDK || action != Intent.ACTION_BOOT_COMPLETED

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
            Host.FOREGROUND -> startFallback(context, intent.action)
        }
    }

    /**
     * On the releases that refuse it, a reboot simply cannot restore the fallback host.
     * What is left is the user opening the app — which is also what resets the six-hour
     * budget, so a reboot and a timeout have the same recovery for the same reason.
     *
     * Nothing is flagged on [PipeStatus] here. This process is about to go away, nothing
     * persists it, and the screen already answers the question with `Engine running: no`.
     */
    private fun startFallback(context: Context, action: String?) {
        if (!canStartFallbackFrom(Build.VERSION.SDK_INT, action)) {
            Log.i(TAG, "boot cannot start a dataSync host on API $FGS_TIMEOUT_SDK+")
            return
        }
        PipeService.start(context)
    }

    private companion object {
        const val TAG = "BootReceiver"
    }
}
