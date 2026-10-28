package link.dendritik.proto.pipe.weather

import link.dendritik.proto.pipe.protocol.Hazard
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HazardPolicyTest {

    private val h = 3600
    private val t0 = 1_761_600_000 - 1_761_600_000 % 3600   // on the hour

    /**
     * An hourly series of [n] samples from [t0], quiet unless a builder says otherwise.
     * Index i is the sample stamped t0 + i hours.
     */
    private fun hourly(
        n: Int = 13,
        code: (Int) -> Int? = { 0 },
        precip: (Int) -> Double? = { 0.0 },
        snow: (Int) -> Double? = { 0.0 },
        gust: (Int) -> Double? = { 10.0 },
        apparent: (Int) -> Double? = { 15.0 },
        step: Int = h,
        start: Int = t0,
    ) = Series(
        stepS = step,
        timesS = List(n) { start + it * step },
        code = List(n, code),
        precipMm = List(n, precip),
        snowCm = List(n, snow),
        gustKmh = List(n, gust),
        apparentC = List(n, apparent),
    )

    private val policy = HazardPolicy()

    @Test
    fun `an hourly sum describes the hour before its stamp`() {
        // 6 mm stamped t0+3h is rain from t0+2h to t0+3h — not from t0+3h.
        val f = Forecast(hourly(precip = { if (it == 3) 6.0 else 0.0 }), null)
        val a = policy.alerts(f, t0)
        assertEquals(listOf(WxAlert(Hazard.RAIN, t0 + 2 * h, 60)), a)
    }

    @Test
    fun `a weather code describes the hour it is stamped at`() {
        val f = Forecast(hourly(code = { if (it == 4) 95 else 0 }), null)
        assertEquals(listOf(WxAlert(Hazard.THUNDER, t0 + 4 * h, 60)), policy.alerts(f, t0))
    }

    @Test
    fun `below every threshold is no alert`() {
        val f = Forecast(hourly(precip = { 3.9 }, snow = { 0.9 }, gust = { 59.0 }, code = { 63 }), null)
        assertTrue(policy.alerts(f, t0).isEmpty())
    }

    @Test
    fun `one hazard per hour, by precedence`() {
        // Thunder and heavy rain and gusts in the same hour is a thunderstorm.
        val f = Forecast(
            hourly(
                code = { if (it == 2) 95 else 0 },
                precip = { if (it == 3) 10.0 else 0.0 },
                gust = { if (it == 3) 80.0 else 10.0 },
            ),
            null,
        )
        assertEquals(listOf(WxAlert(Hazard.THUNDER, t0 + 2 * h, 60)), policy.alerts(f, t0))
    }

    @Test
    fun `contiguous hours merge, across one quiet hour but not two`() {
        val rainy = setOf(2, 3, 5, 8)          // stamps; the hours are one earlier
        val f = Forecast(hourly(precip = { if (it in rainy) 5.0 else 0.0 }), null)
        assertEquals(
            listOf(
                WxAlert(Hazard.RAIN, t0 + 1 * h, 4 * 60),   // 1..5, the 3..4 gap bridged
                WxAlert(Hazard.RAIN, t0 + 7 * h, 60),       // after two quiet hours
            ),
            policy.alerts(f, t0),
        )
    }

    @Test
    fun `different hazards do not merge across a gap`() {
        val f = Forecast(
            hourly(
                precip = { if (it == 2) 5.0 else 0.0 },
                gust = { if (it == 4) 70.0 else 10.0 },
            ),
            null,
        )
        assertEquals(
            listOf(WxAlert(Hazard.RAIN, t0 + h, 60), WxAlert(Hazard.GUSTS, t0 + 3 * h, 60)),
            policy.alerts(f, t0),
        )
    }

    @Test
    fun `the quarter series moves the ends of an hourly alert`() {
        val hourly = hourly(precip = { if (it == 4 || it == 5) 6.0 else 0.0 })   // 3h..5h
        // Quarter sums stamped at the end of their fifteen minutes: rain 3h30..4h45.
        val q = hourly(
            n = 30, step = 900,
            precip = { val end = t0 + (it + 1) * 900; if (end > t0 + 3 * h + 1800 && end <= t0 + 4 * h + 2700) 2.0 else 0.0 },
        ).let { s -> s.copy(timesS = s.timesS.map { it + 900 }) }
        val a = policy.alerts(Forecast(hourly, q), t0)
        assertEquals(listOf(WxAlert(Hazard.RAIN, t0 + 3 * h + 1800, 75)), a)
    }

    @Test
    fun `the quarter series never creates an alert`() {
        val q = hourly(n = 30, step = 900, precip = { if (it == 10) 5.0 else 0.0 })
        assertTrue(policy.alerts(Forecast(hourly(), q), t0).isEmpty())
    }

    @Test
    fun `a running alert is kept and an ended one is not`() {
        val f = Forecast(hourly(precip = { if (it == 1 || it == 4) 5.0 else 0.0 }), null)
        // Rain 0..1h has ended; 3..4h is running at 3h30.
        assertEquals(
            listOf(WxAlert(Hazard.RAIN, t0 + 3 * h, 60)),
            policy.alerts(f, t0 + 3 * h + 1800),
        )
    }

    @Test
    fun `nothing past eleven hours, and no more than eight`() {
        // Thunder and gusts in alternate hours: adjacent hazards that differ never
        // merge, so every hour is an alert of its own.
        val f = Forecast(
            hourly(
                n = 30,
                code = { if (it % 2 == 0) 95 else 0 },
                gust = { if (it % 2 == 0) 70.0 else 10.0 },   // stamp 2k covers hour 2k-1
            ),
            null,
        )
        val a = policy.alerts(f, t0)
        assertEquals(HazardPolicy.MAX_ALERTS, a.size)
        assertTrue(a.all { it.onsetUtcS <= t0 + HazardPolicy.HORIZON_S })
        assertEquals(a.sortedBy { it.onsetUtcS }, a)
    }

    @Test
    fun `heat is off unless asked for`() {
        val hot = Forecast(hourly(apparent = { 38.0 }), null)
        assertTrue(policy.alerts(hot, t0).isEmpty())
        assertEquals(Hazard.HEAT, HazardPolicy(heat = true).alerts(hot, t0).first().hazard)
    }

    @Test
    fun `a missing value is no hazard, never a zero to compare`() {
        val f = Forecast(hourly(code = { null }, precip = { null }, gust = { null }), null)
        assertTrue(policy.alerts(f, t0).isEmpty())
    }
}
