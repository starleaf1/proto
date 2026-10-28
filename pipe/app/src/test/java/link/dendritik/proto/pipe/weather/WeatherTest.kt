package link.dendritik.proto.pipe.weather

import link.dendritik.proto.pipe.protocol.Hazard
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class OpenMeteoTest {

    @Test
    fun `parses both series, nulls and all`() {
        val json = """
            {"hourly": {"time": [1000, 4600],
                        "weather_code": [95, null],
                        "precipitation": [1.5, 2.0],
                        "snowfall": [0.0, null],
                        "wind_gusts_10m": [40.0, 61.2],
                        "apparent_temperature": [12.0, 11.5]},
             "minutely_15": {"time": [1000], "weather_code": [3],
                             "precipitation": [0.2], "snowfall": [0],
                             "wind_gusts_10m": [30], "apparent_temperature": [12]}}
        """.trimIndent()
        val f = OpenMeteo.parse(json)
        assertEquals(3600, f.hourly.stepS)
        assertEquals(listOf(1000, 4600), f.hourly.timesS)
        assertEquals(listOf(95, null), f.hourly.code)
        assertEquals(listOf(0.0, null), f.hourly.snowCm)
        assertEquals(900, f.quarter!!.stepS)
    }

    @Test
    fun `a forecast without the fifteen-minute series is still a forecast`() {
        val f = OpenMeteo.parse("""{"hourly": {"time": [0], "weather_code": [0]}}""")
        assertNull(f.quarter)
        assertEquals(listOf<Double?>(null), f.hourly.precipMm)
    }

    @Test(expected = Exception::class)
    fun `an error body is not a forecast`() {
        OpenMeteo.parse("""{"error": true, "reason": "bad"}""")
    }

    @Test
    fun `coordinates leave rounded to two decimals, in any locale`() {
        val url = OpenMeteo.url(51.507351, -0.127758)
        assertTrue(url, url.contains("latitude=51.51&longitude=-0.13"))
        assertTrue(url.contains("timeformat=unixtime"))
    }
}

class WxHolderTest {

    private val storm = WxAlert(Hazard.THUNDER, 10_000, 60)    // ends at 13_600

    @Test
    fun `nothing heard yet leaves the key out`() {
        val h = WxHolder()
        assertNull(h.current(0))
        h.accept(WxScan.Unknown("no location"))
        assertNull(h.current(0))
    }

    @Test
    fun `a forecast is sent, less what has ended`() {
        val h = WxHolder()
        h.accept(WxScan.Alerts(listOf(storm), 9_000))
        assertEquals(listOf(storm), h.current(9_000))
        assertEquals(emptyList<WxAlert>(), h.current(13_600))
    }

    @Test
    fun `a failed fetch holds the last forecast for three hours, then lets go`() {
        val h = WxHolder()
        h.accept(WxScan.Alerts(listOf(WxAlert(Hazard.SNOW, 20_000, 600)), 9_000))
        h.accept(WxScan.Unknown("HTTP 503"))
        assertEquals(1, h.current(9_000 + 3 * 3600)!!.size)
        assertEquals(emptyList<WxAlert>(), h.current(9_001 + 3 * 3600))
    }

    @Test
    fun `switched off is a clear sky at once`() {
        val h = WxHolder()
        h.accept(WxScan.Alerts(listOf(storm), 9_000))
        h.accept(WxScan.Absent)
        assertEquals(emptyList<WxAlert>(), h.current(9_000))
    }
}

class WxScheduleTest {

    @Test
    fun `the first fetch is due at once`() = assertTrue(WxSchedule.due(1_000, 0, 0))

    @Test
    fun `a success holds for half an hour`() {
        assertFalse(WxSchedule.due(1_000 + 29 * 60, 1_000, 1_000))
        assertTrue(WxSchedule.due(1_000 + 30 * 60, 1_000, 1_000))
    }

    @Test
    fun `a failure retries a tick later, not sooner`() {
        assertFalse(WxSchedule.due(1_000 + 8 * 60, 1_000, 0))
        assertTrue(WxSchedule.due(1_000 + 9 * 60, 1_000, 0))
    }
}
