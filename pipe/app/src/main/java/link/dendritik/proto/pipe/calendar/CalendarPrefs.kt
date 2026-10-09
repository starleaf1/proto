package link.dendritik.proto.pipe.calendar

import android.content.Context
import android.content.SharedPreferences
import androidx.core.content.edit

/**
 * Where the calendar choice is kept, and the signal that it changed.
 *
 * The screen writes it and the engine reads it; they share a process, so the engine
 * hears a change through the listener rather than through a broadcast of our own.
 * A change is one more reason to re-scan, exactly like an edit in the calendar — it is
 * not a forced flush, because a flush replaces the watch's table anyway and an unticked
 * calendar with nothing in the window changes nothing that needs saying.
 */
class CalendarPrefs(context: Context) {

    private val prefs = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    // SharedPreferences holds its listeners weakly, so this field is what keeps one alive.
    private var listener: SharedPreferences.OnSharedPreferenceChangeListener? = null

    /** `null` until the user first changes anything. See [CalendarChoice]. */
    fun chosen(): Set<String>? = prefs.getStringSet(KEY_SENT, null)?.toSet()

    fun setChosen(keys: Set<String>) = prefs.edit { putStringSet(KEY_SENT, keys) }

    fun watch(onChanged: () -> Unit) {
        if (listener != null) return
        val l = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == KEY_SENT) onChanged()
        }
        prefs.registerOnSharedPreferenceChangeListener(l)
        listener = l
    }

    fun unwatch() {
        listener?.let { prefs.unregisterOnSharedPreferenceChangeListener(it) }
        listener = null
    }

    private companion object {
        const val FILE = "calendars"
        const val KEY_SENT = "sent"
    }
}
