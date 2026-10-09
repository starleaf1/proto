package link.dendritik.proto.pipe.calendar

import android.Manifest
import android.content.ContentUris
import android.content.Context
import android.content.pm.PackageManager
import android.provider.CalendarContract
import android.util.Log
import androidx.core.content.ContextCompat
import link.dendritik.proto.pipe.protocol.EventKind
import link.dendritik.proto.pipe.protocol.WireIds

/**
 * Reads the next few hours out of `CalendarContract`.
 *
 * The window matches what the watch can draw — six hours forward, three back to cover
 * the linger a passed marker gets, which is as far back as the deepest face looks
 * (the digital face on the round display, three hours either side of now) — so a
 * scan is small and a diff between two scans is smaller. Whole-day entries are excluded in the query: they have no position on
 * a twelve-hour dial. So is every calendar the user has not chosen to send — see
 * [CalendarChoice].
 *
 * Framework types stop here. Everything downstream sees [EventFacts], which is what
 * lets the packing and diffing logic be unit-tested with no device.
 */
class CalendarSource(private val context: Context) {

    fun hasPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.READ_CALENDAR) ==
            PackageManager.PERMISSION_GRANTED

    /**
     * Every calendar on the phone, for the settings screen and for turning a choice into
     * row ids. Empty without the grant, which the screen reads as nothing to choose from.
     */
    fun calendars(): List<CalendarInfo> {
        if (!hasPermission()) return emptyList()
        val out = mutableListOf<CalendarInfo>()
        try {
            context.contentResolver.query(
                CalendarContract.Calendars.CONTENT_URI, CALENDAR_PROJECTION, null, null,
                CALENDAR_SORT,
            )?.use { c ->
                while (c.moveToNext()) {
                    val id = c.getLong(0)
                    val account = c.getString(2)
                    out += CalendarInfo(
                        id = id,
                        key = CalendarChoice.key(id, c.getString(1), account, c.getString(3)),
                        name = c.getString(4) ?: account ?: "Calendar $id",
                        account = account.orEmpty(),
                        color = c.getInt(5),
                    )
                }
            }
        } catch (e: SecurityException) {
            Log.w(TAG, "calendar list denied", e)
            return emptyList()
        }
        return out
    }

    /**
     * The window, from the calendars [chosen] allows — every calendar when it is `null`.
     *
     * The filter is in the query, not applied afterwards, so an unticked calendar's
     * entries are never read, let alone sent.
     */
    fun query(nowMs: Long, chosen: Set<String>?): List<EventFacts> {
        if (!hasPermission()) return emptyList()

        // Row ids are resolved afresh on every scan rather than cached: they are what
        // changes under a re-added account, and the keys are what does not.
        val ids = CalendarChoice.sentIds(calendars(), chosen)
        if (ids != null && ids.isEmpty()) return emptyList()
        val selection = if (ids == null) SELECTION else {
            // Longs straight from the provider, so inlining them cannot inject anything.
            "$SELECTION AND ${CalendarContract.Instances.CALENDAR_ID} IN (${ids.joinToString(",")})"
        }

        val begin = nowMs - WINDOW_BACK_MS
        val end = nowMs + WINDOW_AHEAD_MS
        val uri = CalendarContract.Instances.CONTENT_URI.buildUpon().let {
            ContentUris.appendId(it, begin)
            ContentUris.appendId(it, end)
            it.build()
        }

        val out = mutableListOf<EventFacts>()
        try {
            context.contentResolver.query(uri, PROJECTION, selection, null, SORT)?.use { c ->
                while (c.moveToNext()) {
                    val eventId = c.getLong(0)
                    val beginMs = c.getLong(1)
                    val endMs = c.getLong(2)
                    val durationMin = ((endMs - beginMs) / 60_000L)
                        .coerceIn(0L, Int.MAX_VALUE.toLong())
                        .toInt()
                    out += EventFacts(
                        id = instanceId(eventId, beginMs),
                        startUtcS = (beginMs / 1000L).toInt(),
                        durationMin = durationMin,
                        // Duration is the only signal available: Android exposes no
                        // tasks provider, so a zero-length entry is what a reminder
                        // looks like from here. `kind` is carried on the wire
                        // separately from the duration so that adding a real provider
                        // later needs no protocol change — see docs/protocol.md.
                        kind = if (durationMin == 0) EventKind.TASK else EventKind.APPOINTMENT,
                    )
                }
            }
        } catch (e: SecurityException) {
            // The grant can be revoked between the check above and the query.
            Log.w(TAG, "calendar read denied", e)
            return emptyList()
        }
        return out
    }

    /**
     * A stable key for one *instance*.
     *
     * It has to be the instance and not the event: a weekly meeting is one event row
     * and many instances, and keying on the event id alone would make each occurrence
     * silently overwrite the last. It also has to be stable across scans and across
     * process restarts, because it is the identity the watch removes entries by —
     * hence an explicit mix rather than [Any.hashCode], whose contract does not
     * promise stability between runs. Minutes, not milliseconds, so sub-minute jitter
     * in a provider's reported start cannot re-key an entry.
     *
     * The result is masked to 31 bits. That is not cosmetic: bit 31 now marks the
     * Nuron half of the id space, and this hash sets it for roughly half of all
     * inputs, so without the mask the two sources would overlap on half their ids
     * and silently overwrite each other. See [WireIds].
     *
     * Masking re-keys every existing entry once. That costs nothing, because
     * `PebbleSender.sentEvents` lives in memory: an app update restarts the
     * process, `PipeEngine.start()` flushes, and the watch drops its whole table
     * before the new ids arrive.
     */
    private fun instanceId(eventId: Long, beginMs: Long): Int {
        var h = FNV_OFFSET
        for (value in longArrayOf(eventId, beginMs / 60_000L)) {
            var v = value
            repeat(8) {
                h = (h xor (v and 0xFF).toInt()) * FNV_PRIME
                v = v ushr 8
            }
        }
        return WireIds.calendar(h)
    }

    private companion object {
        const val TAG = "CalendarSource"

        const val WINDOW_AHEAD_MS = 6 * 60 * 60 * 1000L
        const val WINDOW_BACK_MS = 3 * 60 * 60 * 1000L

        const val FNV_OFFSET = -0x7EE3623B   // 2166136261 as a signed Int
        const val FNV_PRIME = 16777619

        val PROJECTION = arrayOf(
            CalendarContract.Instances.EVENT_ID,
            CalendarContract.Instances.BEGIN,
            CalendarContract.Instances.END,
        )

        // STATUS is nullable, and `NULL != 2` is NULL in SQL — which is not true, so a
        // bare inequality would silently drop every entry whose status is unset.
        val SELECTION = "${CalendarContract.Instances.ALL_DAY} = 0" +
            " AND (${CalendarContract.Instances.STATUS} IS NULL" +
            " OR ${CalendarContract.Instances.STATUS} != ${CalendarContract.Events.STATUS_CANCELED})"

        const val SORT = "${CalendarContract.Instances.BEGIN} ASC"

        val CALENDAR_PROJECTION = arrayOf(
            CalendarContract.Calendars._ID,
            CalendarContract.Calendars.ACCOUNT_TYPE,
            CalendarContract.Calendars.ACCOUNT_NAME,
            CalendarContract.Calendars._SYNC_ID,
            CalendarContract.Calendars.CALENDAR_DISPLAY_NAME,
            CalendarContract.Calendars.CALENDAR_COLOR,
        )

        const val CALENDAR_SORT = "${CalendarContract.Calendars.ACCOUNT_NAME} ASC, " +
            "${CalendarContract.Calendars.CALENDAR_DISPLAY_NAME} ASC"
    }
}
