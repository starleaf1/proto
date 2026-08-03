package link.dendritik.proto.pipe

import android.annotation.SuppressLint
import android.companion.CompanionDeviceService
import android.util.Log
import androidx.annotation.RequiresApi

/**
 * The host that costs the user nothing: the system binds this for as long as the
 * associated watch is nearby.
 *
 * This is the arrangement the previous design had by accident and this one lost — a
 * service the platform keeps alive for its own reasons, so calendar observation, the
 * phone's battery and the periodic tick can outlive the activity without a permanent
 * notification. [PipeService] remains for devices that cannot get here.
 *
 * Bluetooth presence and PebbleKit's app-message channel are two different signals, and
 * presence usually wins the race — we are bound before the Pebble app has a channel. No
 * coordination is needed for that: `syncCalendar` no-ops while the watch is not
 * connected, and the `INTENT_PEBBLE_CONNECTED` that follows drives the full flush.
 * Starting the engine "too early" is therefore correct, not a bug to be fixed.
 */
@RequiresApi(MIN_COMPANION_SDK)
class PipeCompanionService : CompanionDeviceService() {

    private var engine: PipeEngine? = null

    /**
     * The `String` overloads, deliberately, though API 33 deprecated them in favour of
     * `AssociationInfo` ones.
     *
     * They are the only form that exists on 31 and 32, and on 33+ the platform's
     * `AssociationInfo` implementation forwards to them for any association backed by a
     * MAC address — which a [android.companion.BluetoothDeviceFilter] association always
     * is. Overriding the newer overloads instead would mean naming a class that does not
     * exist on the two oldest releases this host supports, to gain nothing.
     */
    @Suppress("OVERRIDE_DEPRECATION")
    @SuppressLint("MissingSuperCall")
    override fun onDeviceAppeared(address: String) {
        Log.i(TAG, "watch present: $address")
        PipeStatus.watchPresent = true
        PipeStatus.host = Host.COMPANION
        if (engine == null) engine = PipeEngine(applicationContext)
        engine?.start()
    }

    @Suppress("OVERRIDE_DEPRECATION")
    @SuppressLint("MissingSuperCall")
    override fun onDeviceDisappeared(address: String) {
        Log.i(TAG, "watch gone: $address")
        PipeStatus.watchPresent = false
        // Nothing to keep running for. The watch is the only consumer, syncCalendar
        // already stands down when it cannot see one, and the protocol says a
        // companion-down verdict does not survive a Bluetooth gap — so the beat we are
        // no longer sending cannot be mistaken for a dead companion on reconnect.
        engine?.stop()
    }

    override fun onDestroy() {
        engine?.stop()
        engine = null
        super.onDestroy()
    }

    private companion object {
        const val TAG = "PipeCompanionService"
    }
}
