package link.dendritik.proto.pipe.weather

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.Build
import android.os.CancellationSignal
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * The switch, and the signal that it moved. Off until the user turns it on: this is the
 * one feature that sends anything about the phone off the phone — its rough position, to
 * a forecast server — and that is not a thing to start doing on an update.
 */
class WeatherPrefs(context: Context) {

    private val prefs = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)
    private var listener: SharedPreferences.OnSharedPreferenceChangeListener? = null

    fun enabled(): Boolean = prefs.getBoolean(KEY_ENABLED, false)

    fun setEnabled(on: Boolean) = prefs.edit { putBoolean(KEY_ENABLED, on) }

    fun watch(onChanged: () -> Unit) {
        if (listener != null) return
        val l = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == KEY_ENABLED) onChanged()
        }
        prefs.registerOnSharedPreferenceChangeListener(l)
        listener = l
    }

    fun unwatch() {
        listener?.let { prefs.unregisterOnSharedPreferenceChangeListener(it) }
        listener = null
    }

    private companion object {
        const val FILE = "weather"
        const val KEY_ENABLED = "enabled"
    }
}

/**
 * Where the phone is, roughly, and the grants that allow asking.
 *
 * Coarse only: a forecast grid is kilometres across, and a fine fix is not something
 * this app has any business holding. Background as well, because the question is asked
 * on the periodic tick with the screen off, which is exactly when Android withholds a
 * foreground-only grant.
 */
object WeatherLocation {

    fun granted(context: Context): Boolean {
        fun has(p: String) =
            ContextCompat.checkSelfPermission(context, p) == PackageManager.PERMISSION_GRANTED
        val coarse = has(Manifest.permission.ACCESS_COARSE_LOCATION)
        val background = Build.VERSION.SDK_INT < Build.VERSION_CODES.Q ||
            has(Manifest.permission.ACCESS_BACKGROUND_LOCATION)
        return coarse && background
    }

    /**
     * The newest fix any provider has, or a fresh one if that is over [STALE_MS] old.
     * Blocking, up to [FIX_TIMEOUT_MS]; call it off the main thread.
     *
     * Re-checks the grant first, as `CalendarSource` does before every query — it can be
     * revoked while the engine runs — and catches the `SecurityException` anyway, the
     * check and the call not being atomic.
     */
    @SuppressLint("MissingPermission")
    fun current(context: Context): Location? {
        if (!granted(context)) return null
        val lm = context.getSystemService(Context.LOCATION_SERVICE) as LocationManager
        return try {
            val last = lm.getProviders(true)
                .mapNotNull { runCatching { lm.getLastKnownLocation(it) }.getOrNull() }
                .maxByOrNull { it.time }
            if (last != null && System.currentTimeMillis() - last.time < STALE_MS) return last
            fresh(lm) ?: last
        } catch (e: SecurityException) {
            null
        }
    }

    @SuppressLint("MissingPermission")
    private fun fresh(lm: LocationManager): Location? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return null
        if (!lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) return null
        val done = CountDownLatch(1)
        var fix: Location? = null
        val cancel = CancellationSignal()
        lm.getCurrentLocation(LocationManager.NETWORK_PROVIDER, cancel, Runnable::run) {
            fix = it
            done.countDown()
        }
        if (!done.await(FIX_TIMEOUT_MS, TimeUnit.MILLISECONDS)) cancel.cancel()
        return fix
    }

    private const val STALE_MS = 60 * 60 * 1000L
    private const val FIX_TIMEOUT_MS = 5_000L
}

/**
 * Fetches the forecast and turns it into alerts, off the main thread.
 *
 * Everything that touches the framework or the network is here, and everything that
 * decides anything is in [HazardPolicy] and [WxHolder], which are pure. The callback is
 * always called, exactly once, on the main thread — including for a timeout, a missing
 * grant and a switch that is off — so a caller holding a broadcast open with `goAsync`
 * can rely on it to finish.
 */
class WeatherSource(private val context: Context) {

    private val prefs = WeatherPrefs(context)
    private val policy = HazardPolicy()
    private val main = Handler(Looper.getMainLooper())
    private var executor: ExecutorService? = null

    /** The callback of the fetch in flight, if one is. Main thread only. */
    private var pending: ((WxScan) -> Unit)? = null

    /** Whether a fetch could say anything but [WxScan.Absent]. */
    fun enabled(): Boolean = prefs.enabled() && WeatherLocation.granted(context)

    fun start() {
        if (executor == null) executor = Executors.newSingleThreadExecutor()
    }

    /**
     * Stops, and answers a fetch in flight with [WxScan.Unknown] — its task may never
     * run once the executor is shut down, and a caller holding a broadcast open is
     * waiting on that callback to finish it.
     */
    fun stop() {
        executor?.shutdownNow()
        executor = null
        val cb = pending
        pending = null
        cb?.invoke(WxScan.Unknown("stopped"))
    }

    /** Returns false, and calls nothing, if one is already running or the source is stopped. */
    fun fetch(onResult: (WxScan) -> Unit): Boolean {
        val ex = executor ?: return false
        if (pending != null) return false
        pending = onResult
        ex.execute {
            val scan = try {
                scan()
            } catch (e: Exception) {
                Log.w(TAG, "forecast failed", e)
                WxScan.Unknown(e.javaClass.simpleName)
            }
            main.post {
                // Null if stop() already answered it.
                val cb = pending
                pending = null
                cb?.invoke(scan)
            }
        }
        return true
    }

    private fun scan(): WxScan {
        if (!enabled()) return WxScan.Absent
        val at = WeatherLocation.current(context) ?: return WxScan.Unknown("no location")
        val json = get(OpenMeteo.url(at.latitude, at.longitude))
        val nowS = (System.currentTimeMillis() / 1000).toInt()
        val alerts = policy.alerts(OpenMeteo.parse(json), nowS)
        Log.i(TAG, "forecast: ${alerts.size} alert(s)")
        return WxScan.Alerts(alerts, nowS)
    }

    private fun get(url: String): String {
        val conn = URL(url).openConnection() as HttpURLConnection
        try {
            conn.connectTimeout = TIMEOUT_MS
            conn.readTimeout = TIMEOUT_MS
            val code = conn.responseCode
            if (code != HttpURLConnection.HTTP_OK) throw java.io.IOException("HTTP $code")
            return conn.inputStream.bufferedReader().use { it.readText() }
        } finally {
            conn.disconnect()
        }
    }

    private companion object {
        const val TAG = "WeatherSource"
        const val TIMEOUT_MS = 5_000
    }
}
