package link.dendritik.proto.pipe.nuron

import android.content.Context
import android.content.pm.PackageManager
import android.util.Log
import androidx.core.content.ContextCompat

/**
 * Reads the next few hours of quests out of Nuron's `QuestFactsProvider`.
 *
 * The second source, beside `CalendarSource`. It contributes the same shape of
 * [link.dendritik.proto.pipe.calendar.EventFacts] and nothing else — Nuron sends
 * a position and a duration, never a title, which is the same axiom the calendar
 * side already holds to and is enforced on that end by the provider's schema
 * having no column to put one in.
 *
 * Everything interesting is in [NuronCursor]; this class is the framework
 * boundary and holds only the resolver call and the permission check.
 */
class NuronSource(private val context: Context) {

    /**
     * Re-checked before every query rather than once at startup, exactly as
     * `CalendarSource.hasPermission` is: a grant can be revoked while the
     * service runs, and the check and the query are not atomic anyway — which is
     * why [scan] also catches [SecurityException].
     */
    fun hasPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, NuronContract.PERMISSION_READ) ==
            PackageManager.PERMISSION_GRANTED

    fun isInstalled(): Boolean =
        runCatching {
            context.packageManager.getPackageInfo(NuronContract.PACKAGE, 0)
        }.isSuccess

    fun scan(): NuronScan {
        // Not installed, or installed and not permitted: a durable zero either
        // way. Absent rather than Unknown, because this will not resolve itself
        // on the next tick and holding stale markers indefinitely would be worse
        // than dropping them.
        if (!isInstalled() || !hasPermission()) return NuronScan.Absent

        return try {
            context.contentResolver.query(
                NuronContract.CONTENT_URI, null, null, null, null,
            ).use { c -> NuronCursor.read(c, c?.extras) }
        } catch (e: SecurityException) {
            // The grant can go between the check above and the query.
            Log.w(TAG, "nuron read denied", e)
            NuronScan.Absent
        } catch (e: Exception) {
            // The provider threw, or Nuron is mid-upgrade and its process is
            // gone. Nothing can be concluded, so nothing should be deleted.
            Log.w(TAG, "nuron read failed", e)
            NuronScan.Unknown
        }
    }

    private companion object {
        const val TAG = "NuronSource"
    }
}
