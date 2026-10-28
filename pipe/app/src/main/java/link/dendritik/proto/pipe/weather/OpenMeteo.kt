package link.dendritik.proto.pipe.weather

import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

/**
 * The request and the response, and nothing that touches the network. See
 * [OpenMeteoClient] for that.
 *
 * Open-Meteo because it needs no key and no account, which means nothing to provision,
 * leak or rotate. Its free tier is for non-commercial use; this asks once every half
 * hour, which is about fifty calls a day against a limit in the thousands.
 */
object OpenMeteo {

    private const val BASE = "https://api.open-meteo.com/v1/forecast"
    private const val VARS = "weather_code,precipitation,snowfall,wind_gusts_10m,apparent_temperature"

    /**
     * The next twelve hours and the last one, at both resolutions.
     *
     * The hour behind is there for the aggregates: the sample that says how hard it is
     * raining *now* is stamped at the end of the current hour, and the one before it is
     * what says whether an alert is already running. The units are spelled out rather
     * than left to their defaults, because the thresholds are written in them.
     *
     * The coordinates are rounded to two decimals, about a kilometre — finer than any
     * forecast grid is, and as much as this request ever needs to say about where the
     * phone is.
     */
    fun url(lat: Double, lon: Double): String = String.format(
        Locale.ROOT,
        "%s?latitude=%.2f&longitude=%.2f&hourly=%s&minutely_15=%s" +
            "&forecast_hours=12&past_hours=1&forecast_minutely_15=48&past_minutely_15=4" +
            "&timeformat=unixtime&timezone=GMT" +
            "&wind_speed_unit=kmh&precipitation_unit=mm&temperature_unit=celsius",
        BASE, lat, lon, VARS, VARS,
    )

    /** Throws on anything that is not a forecast; the caller reports that as unknown. */
    fun parse(json: String): Forecast {
        val root = JSONObject(json)
        val hourly = series(root.getJSONObject("hourly"), 3600)
            ?: throw IllegalArgumentException("no hourly times")
        val quarter = root.optJSONObject("minutely_15")?.let { series(it, 900) }
        return Forecast(hourly, quarter)
    }

    private fun series(o: JSONObject, stepS: Int): Series? {
        val times = o.optJSONArray("time") ?: return null
        val n = times.length()
        if (n == 0) return null
        return Series(
            stepS = stepS,
            timesS = List(n) { times.getLong(it).toInt() },
            code = ints(o.optJSONArray("weather_code"), n),
            precipMm = doubles(o.optJSONArray("precipitation"), n),
            snowCm = doubles(o.optJSONArray("snowfall"), n),
            gustKmh = doubles(o.optJSONArray("wind_gusts_10m"), n),
            apparentC = doubles(o.optJSONArray("apparent_temperature"), n),
        )
    }

    // A missing array and a null entry both read as "not forecast", which the policy
    // treats as no hazard — never as a zero it could compare.
    private fun ints(a: JSONArray?, n: Int): List<Int?> =
        List(n) { i -> if (a == null || i >= a.length() || a.isNull(i)) null else a.getInt(i) }

    private fun doubles(a: JSONArray?, n: Int): List<Double?> =
        List(n) { i -> if (a == null || i >= a.length() || a.isNull(i)) null else a.getDouble(i) }
}
