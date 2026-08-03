package link.dendritik.proto.pipe

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * What the diagnostics screen shows.
 *
 * Diagnostics only — nothing reads this to make a decision, and nothing here is
 * persisted. It exists so the app can answer "is it working" without the user having
 * to read logcat.
 */
object PipeStatus {
    var running by mutableStateOf(false)
    var watchConnected by mutableStateOf(false)
    var calendarGranted by mutableStateOf(false)
    var eventCount by mutableStateOf(0)

    /** Which host is holding the engine open. */
    var host by mutableStateOf(Host.FOREGROUND)

    /**
     * Whether the system currently reports the associated watch as nearby.
     *
     * This is the one field that earns its keep beyond curiosity: the companion host's
     * whole premise is that the platform binds us on presence, and that premise cannot
     * be verified from a desktop. If this reads `false` with the watch plainly on the
     * wrist, the binding is not working on that device and the answer is to raise
     * [MIN_COMPANION_SDK] or unpair.
     */
    var watchPresent by mutableStateOf(false)
}
