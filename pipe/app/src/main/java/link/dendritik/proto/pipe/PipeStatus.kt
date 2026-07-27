package link.dendritik.proto.pipe

import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import link.dendritik.proto.pipe.protocol.IconState

/**
 * What the listener is currently telling the watch, exposed for the UI to show.
 *
 * A plain observable singleton rather than a repository: the listener service and
 * the activity share one process, and the listener's callbacks already arrive on
 * the main thread, so Compose state is both safe and enough. Diagnostics only —
 * nothing reads this to make a decision.
 */
object PipeStatus {
    var state by mutableStateOf(IconState.IDLE)
    var listenerConnected by mutableStateOf(false)
}
