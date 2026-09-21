package link.dendritik.proto.pipe

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * What became of the last re-sync the user asked for.
 *
 * A flush is invisible from the phone — the bytes leave and nothing here changes — so
 * the button needs somewhere to say what happened. [REQUESTED] is set by the activity
 * before the broadcast goes out and is the state that survives if no engine is alive to
 * answer it, which is the one failure the button cannot rule out in advance.
 */
enum class Resync { IDLE, REQUESTED, SENT, NOTHING_SENT }

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

    /** Entries actually sent — the merge of both sources, capped at MergePolicy.MAX_MERGED. */
    var eventCount by mutableStateOf(0)

    /**
     * Per-source counts, before the merge and the cap.
     *
     * They earn their place on the diagnostics screen the same way `watchPresent`
     * does: when the watch shows fewer markers than expected there are three
     * unlike causes — one source is empty, the merge capped, or the send failed —
     * and none of them is distinguishable from the other end of a Bluetooth link.
     */
    var calendarEventCount by mutableStateOf(0)
    var nuronEventCount by mutableStateOf(0)

    /**
     * Whether Nuron answered, and if not, how long we have been holding its last
     * answer. "unreadable (holding, 2/3)" is the one line that explains markers
     * which are present but should not be, and it is invisible from anywhere else.
     */
    var nuronState by mutableStateOf("never read")

    /** Which host is holding the engine open. */
    var host by mutableStateOf(Host.FOREGROUND)

    /**
     * Wall clock of the last send that reached the watch, or 0 if none ever has.
     *
     * Wall clock rather than `elapsedRealtime` because the only consumer renders it as a
     * time of day. [PebbleSender][link.dendritik.proto.pipe.pebble.PebbleSender] keeps
     * its own monotonic stamp for the beat guard, which is a different question.
     */
    var lastSyncAtMs by mutableStateOf(0L)

    /** The outcome of the manual re-sync button; see [Resync]. */
    var resync by mutableStateOf(Resync.IDLE)

    /**
     * Whether Android has capped the foreground host.
     *
     * Set when the `dataSync` budget runs out under us, and when the platform refuses to
     * let us become a foreground service because it is already spent. Not persisted, like
     * everything here — but it does not need to be, because the condition is re-derived
     * the moment [PipeService] next tries to start, and clearing it is what a successful
     * start does.
     *
     * The one row on the screen that states a platform limit rather than our own state,
     * and it earns that: with the notification gone and the engine down, the alternative
     * is an app that has silently stopped working six hours after the user last looked.
     */
    var capped by mutableStateOf(false)

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
