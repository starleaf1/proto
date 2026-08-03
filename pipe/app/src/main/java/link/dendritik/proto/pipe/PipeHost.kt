package link.dendritik.proto.pipe

import android.companion.CompanionDeviceManager
import android.content.Context
import android.os.Build
import android.util.Log
import androidx.annotation.RequiresApi

/** Which of the two hosts is holding [PipeEngine] open. */
enum class Host { COMPANION, FOREGROUND }

/**
 * The one decision, in one place, so the two hosts can never both be running.
 *
 * Pure on purpose. Whether the companion path is trustworthy on a given Android release
 * is the sort of thing that gets revised after a night on a real device, and this is
 * where that revision happens — raise [MIN_COMPANION_SDK] and every caller follows.
 */
fun chooseHost(sdkInt: Int, hasAssociation: Boolean): Host =
    if (sdkInt >= MIN_COMPANION_SDK && hasAssociation) Host.COMPANION else Host.FOREGROUND

/**
 * Android 12. `CompanionDeviceService` and `startObservingDevicePresence` both arrive
 * here; below it there is no way to have the system bind us on the watch's presence, so
 * the notification is unavoidable.
 */
const val MIN_COMPANION_SDK = 31

/**
 * `CompanionDeviceManager`, and the small amount of version-shear around it.
 *
 * Named `CompanionHost` rather than the obvious `Companion`, which would be shadowed by
 * the implicit companion object of any class that has one.
 *
 * Associating the watch is what buys the notification-free host: the system binds
 * [PipeCompanionService] for as long as an associated device is nearby, which is the
 * same kind of arrangement the old design got for free from a bound
 * `NotificationListenerService`. Everything here is a no-op below
 * [MIN_COMPANION_SDK] and every call is wrapped — the association is user state that can
 * be revoked from system settings while we are not looking.
 */
object CompanionHost {

    /** MAC addresses of every watch the user has associated with us. */
    fun associations(context: Context): List<String> {
        if (Build.VERSION.SDK_INT < MIN_COMPANION_SDK) return emptyList()
        val cdm = manager(context) ?: return emptyList()
        return runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                cdm.myAssociations.mapNotNull { it.deviceMacAddress?.toString() }
            } else {
                @Suppress("DEPRECATION")
                cdm.associations
            }
        }.onFailure { Log.w(TAG, "could not read associations", it) }
            .getOrDefault(emptyList())
    }

    fun hostFor(context: Context): Host =
        chooseHost(Build.VERSION.SDK_INT, associations(context).isNotEmpty())

    /**
     * Asks the system to bind us whenever an associated watch is nearby.
     *
     * Idempotent, and deliberately re-armed rather than assumed: an observation request
     * does not reliably outlive a reboot or an app update, which is why [BootReceiver]
     * listens for both.
     */
    fun startObserving(context: Context) {
        if (Build.VERSION.SDK_INT < MIN_COMPANION_SDK) return
        val cdm = manager(context) ?: return
        for (address in associations(context)) {
            runCatching {
                @Suppress("DEPRECATION")
                cdm.startObservingDevicePresence(address)
            }.onFailure { Log.w(TAG, "could not observe $address", it) }
        }
    }

    /** Forgets every association, so the foreground service takes over again. */
    fun forget(context: Context) {
        if (Build.VERSION.SDK_INT < MIN_COMPANION_SDK) return
        val cdm = manager(context) ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            for (info in runCatching { cdm.myAssociations }.getOrDefault(emptyList())) {
                info.deviceMacAddress?.let { mac ->
                    runCatching {
                        @Suppress("DEPRECATION")
                        cdm.stopObservingDevicePresence(mac.toString())
                    }
                }
                runCatching { cdm.disassociate(info.id) }
                    .onFailure { Log.w(TAG, "could not disassociate ${info.id}", it) }
            }
            return
        }
        for (address in associations(context)) {
            runCatching {
                @Suppress("DEPRECATION")
                cdm.stopObservingDevicePresence(address)
            }
            runCatching {
                @Suppress("DEPRECATION")
                cdm.disassociate(address)
            }.onFailure { Log.w(TAG, "could not disassociate $address", it) }
        }
    }

    @RequiresApi(MIN_COMPANION_SDK)
    private fun manager(context: Context): CompanionDeviceManager? =
        context.getSystemService(CompanionDeviceManager::class.java)

    private const val TAG = "Companion"
}
