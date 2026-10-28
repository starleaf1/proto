package link.dendritik.proto.pipe.weather

import link.dendritik.proto.pipe.protocol.Hazard

/**
 * What counts as severe, and when it starts. Phone policy, all of it in one place: the
 * watch is told a hazard and a time, never a millimetre or a kilometre an hour.
 *
 * ## One hazard at a time
 *
 * Each step of the forecast is given at most one hazard, by [PRECEDENCE], so alerts
 * never overlap. That is what lets the watch's slot show "the" alert rather than pick
 * between several, and it is what keeps onsets far enough apart on the timeline to
 * draw. A thunderstorm with heavy rain is a thunderstorm.
 *
 * ## The timestamps
 *
 * Open-Meteo stamps its sums and maxima at the *end* of the interval they cover —
 * precipitation, snowfall and gusts at 15:00 describe 14:00 to 15:00 — while the
 * weather code and the temperature at 15:00 are 15:00's. Read naively every onset lands
 * an hour late, and on the analog face the hour hand would pass the glyph an hour
 * before the rain. So each step here is the interval *starting* at a time: its instants
 * from the sample at the start, its aggregates from the sample at the end.
 *
 * ## The two resolutions
 *
 * The hourly series decides *whether* there is an alert. The fifteen-minute series,
 * where it exists, only moves an alert's two ends, by at most an hour each way, and
 * never makes one shorter than [MIN_REFINED_S]. It never creates one: outside Central
 * Europe, France and North America that series is interpolated from the hourly one, and
 * the API does not say which you got.
 */
class HazardPolicy(private val heat: Boolean = false) {

    fun alerts(f: Forecast, nowS: Int): List<WxAlert> {
        val runs = merge(steps(f.hourly))
        val quarter = f.quarter?.let { steps(it) }.orEmpty()
        val refined = runs.map { refine(it, quarter) }

        // Refining can push two neighbours of different hazard into each other; the
        // earlier keeps its end, and a later one squeezed to nothing is dropped.
        val out = mutableListOf<Run>()
        for (r in refined.sortedBy { it.startS }) {
            val start = maxOf(r.startS, out.lastOrNull()?.endS ?: Int.MIN_VALUE)
            if (r.endS - start >= MIN_ALERT_S) out += r.copy(startS = start)
        }
        return out
            .filter { it.endS > nowS && it.startS <= nowS + HORIZON_S }
            .take(MAX_ALERTS)
            .map { WxAlert(it.hazard, it.startS, (it.endS - it.startS) / 60) }
    }

    internal data class Run(val hazard: Hazard, val startS: Int, val endS: Int)

    /** Every interval the series covers, with its hazard or none. */
    internal fun steps(s: Series): List<Run?> {
        val at = s.timesS.withIndex().associate { (i, t) -> t to i }
        val starts = (s.timesS + s.timesS.map { it - s.stepS }).toSortedSet()
        return starts.map { start ->
            val i = at[start]                 // instants: the sample at the start
            val j = at[start + s.stepS]       // aggregates: the sample at the end
            classify(
                stepS = s.stepS,
                code = i?.let { s.code[it] },
                apparentC = i?.let { s.apparentC[it] },
                precipMm = j?.let { s.precipMm[it] },
                snowCm = j?.let { s.snowCm[it] },
                gustKmh = j?.let { s.gustKmh[it] },
            )?.let { Run(it, start, start + s.stepS) }
        }
    }

    internal fun classify(
        stepS: Int,
        code: Int?,
        apparentC: Double?,
        precipMm: Double?,
        snowCm: Double?,
        gustKmh: Double?,
    ): Hazard? {
        // The rate thresholds are per hour and the sums are per step.
        val perStep = stepS / 3600.0
        val found = mutableSetOf<Hazard>()
        if (code in ICE_CODES) found += Hazard.ICE
        if (code in THUNDER_CODES) found += Hazard.THUNDER
        if (code in SNOW_CODES || (snowCm ?: 0.0) >= SNOW_CM_PER_H * perStep) found += Hazard.SNOW
        if (code in RAIN_CODES || (precipMm ?: 0.0) >= RAIN_MM_PER_H * perStep) found += Hazard.RAIN
        if ((gustKmh ?: 0.0) >= GUST_KMH) found += Hazard.GUSTS
        if (heat && (apparentC ?: Double.NEGATIVE_INFINITY) >= HEAT_APPARENT_C) found += Hazard.HEAT
        return PRECEDENCE.firstOrNull { it in found }
    }

    /**
     * Contiguous steps of one hazard become one run, across a single quiet step. A
     * forecast that dips below a threshold for one hour in the middle of an afternoon
     * of storms has not forecast two storms, and two glyphs an hour apart is a
     * collision the faces would have to resolve by dropping one anyway.
     */
    internal fun merge(steps: List<Run?>): List<Run> {
        val out = mutableListOf<Run>()
        var gap = 0
        for (s in steps) {
            if (s == null) { gap++; continue }
            val last = out.lastOrNull()
            if (last != null && last.hazard == s.hazard && gap <= 1 &&
                s.startS - last.endS <= gap * (s.endS - s.startS)
            ) {
                out[out.lastIndex] = last.copy(endS = s.endS)
            } else {
                out += s
            }
            gap = 0
        }
        return out
    }

    /** Move a run's ends to where the finer series puts them, within an hour each way. */
    internal fun refine(run: Run, quarter: List<Run?>): Run {
        val same = quarter.filterNotNull().filter { it.hazard == run.hazard }
        val start = same.firstOrNull { it.startS >= run.startS - 3600 && it.startS < run.startS + 3600 }
            ?.startS ?: run.startS
        val end = same.lastOrNull { it.endS > run.endS - 3600 && it.endS <= run.endS + 3600 }
            ?.endS ?: run.endS
        return if (end - start >= MIN_REFINED_S) run.copy(startS = start, endS = end) else run
    }

    companion object {
        val PRECEDENCE = listOf(
            Hazard.ICE, Hazard.THUNDER, Hazard.SNOW, Hazard.RAIN, Hazard.GUSTS, Hazard.HEAT,
        )

        // WMO codes, as Open-Meteo reports them.
        val ICE_CODES = setOf(56, 57, 66, 67)        // freezing drizzle, freezing rain
        val THUNDER_CODES = setOf(95, 96, 99)        // thunderstorm, with hail
        val SNOW_CODES = setOf(73, 75, 86)           // moderate and heavy snow, heavy showers
        val RAIN_CODES = setOf(65, 82)               // heavy rain, violent showers

        // First guesses, and named as such. Each is about where the national services
        // start issuing their lowest tier of warning, which is "severe" in the sense
        // this face means: worth knowing about before you go out.
        const val RAIN_MM_PER_H = 4.0
        const val SNOW_CM_PER_H = 1.0
        const val GUST_KMH = 60.0
        const val HEAT_APPARENT_C = 35.0

        /** As far ahead as the analog face's ring; the digital face shows the first three hours. */
        const val HORIZON_S = 11 * 3600

        /** What the watch's table and the blob hold. */
        const val MAX_ALERTS = 8

        const val MIN_ALERT_S = 15 * 60
        const val MIN_REFINED_S = 30 * 60
    }
}
