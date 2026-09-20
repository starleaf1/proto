package link.dendritik.proto.pipe

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The tick's backoff ladder.
 *
 * Worth testing for the reason `PipeHostTest` covers `chooseHost`: the wrong answer is
 * invisible from this side. The base period is picked against Doze's while-idle budget,
 * so a ladder that returned anything shorter would breach it silently and surface only
 * as a watchface alerting overnight against a phone that looked fine.
 */
class BackoffTest {

    private val base = 600_000L
    private val cap = 3_600_000L

    @Test
    fun `no misses is the base period`() {
        assertEquals(base, backoffDelayMs(base, 0, cap))
    }

    @Test
    fun `each consecutive miss doubles the delay`() {
        assertEquals(1_200_000L, backoffDelayMs(base, 1, cap))
        assertEquals(2_400_000L, backoffDelayMs(base, 2, cap))
    }

    @Test
    fun `the ladder stops at the cap`() {
        assertEquals(cap, backoffDelayMs(base, 3, cap))
        assertEquals(cap, backoffDelayMs(base, 4, cap))
    }

    /** The Doze-budget invariant: backoff may only ever lengthen. */
    @Test
    fun `it never returns less than the base period`() {
        for (misses in 0..64) {
            assertTrue(
                "misses=$misses",
                backoffDelayMs(base, misses, cap) >= base,
            )
        }
    }

    /**
     * A doubling loop rather than a shift, so no miss count can wrap into a short delay.
     * A `shl` would have: 600_000 shl 64 is 600_000 again on a Long.
     */
    @Test
    fun `an absurd miss count cannot overflow into a short delay`() {
        assertEquals(cap, backoffDelayMs(base, 64, cap))
        assertEquals(cap, backoffDelayMs(base, Int.MAX_VALUE, cap))
    }

    @Test
    fun `a negative miss count is treated as none`() {
        assertEquals(base, backoffDelayMs(base, -1, cap))
    }

    /** A cap below the base clamps rather than inverting the ladder. */
    @Test
    fun `a cap under the base wins`() {
        assertEquals(60_000L, backoffDelayMs(base, 0, 60_000L))
        assertEquals(60_000L, backoffDelayMs(base, 5, 60_000L))
    }
}
