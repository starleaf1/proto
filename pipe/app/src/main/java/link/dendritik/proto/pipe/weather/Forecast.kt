package link.dendritik.proto.pipe.weather

import link.dendritik.proto.pipe.protocol.Hazard

/**
 * One alert as it crosses the wire: what, when, for how long. Pure data — the framework
 * boundary for weather, as [EventFacts][link.dendritik.proto.pipe.calendar.EventFacts]
 * is for the calendar. No place name, no description, no numbers from the forecast:
 * the phone sends meaning, and the meaning is a hazard and a time.
 */
data class WxAlert(val hazard: Hazard, val onsetUtcS: Int, val durationMin: Int) {
    val endUtcS: Int get() = onsetUtcS + durationMin * 60
}

/**
 * One forecast series as Open-Meteo returns it: a time per step and a value per
 * variable per step, any of which may be missing.
 *
 * The timestamps are not all the same kind of thing, and [HazardPolicy] depends on the
 * difference. [code] and [apparentC] are *instants* — the weather at [timesS]`[i]`.
 * [precipMm], [snowCm] and [gustKmh] are *aggregates over the step before* —
 * Open-Meteo's "preceding hour sum" and "preceding hour max", or the preceding fifteen
 * minutes in the finer series. A sample stamped 15:00 says how hard it rained from 14:00.
 */
data class Series(
    val stepS: Int,
    val timesS: List<Int>,
    val code: List<Int?>,
    val precipMm: List<Double?>,
    val snowCm: List<Double?>,
    val gustKmh: List<Double?>,
    val apparentC: List<Double?>,
)

/** The hourly series, and the fifteen-minute one where the forecast has it. */
data class Forecast(val hourly: Series, val quarter: Series?)
