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
}
