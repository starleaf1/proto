package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.protocol.Maneuver
import link.dendritik.proto.pipe.protocol.NavState
import link.dendritik.proto.pipe.protocol.RoundaboutShape
import link.dendritik.proto.pipe.protocol.TrafficSide
import org.junit.Assert.assertEquals
import org.junit.Test

/** The nav qualifiers, unpacked the way `wire.c`'s `handle_nav` unpacks them. */
class NavDetailTest {

    private fun exit(v: Int) = v and 0xF
    private fun shape(v: Int) = (v shr 4) and 0x7
    private fun rightHand(v: Int) = (v shr 7) and 0x1

    @Test
    fun `fields sit at the documented bits`() {
        val v = NavDetail.pack(
            NavState(
                maneuver = Maneuver.ROUNDABOUT,
                exit = 2,
                roundabout = RoundaboutShape.MIDDLE,
                traffic = TrafficSide.RIGHT,
            ),
        )
        assertEquals(162, v)                     // the worked example in docs/protocol.md
        assertEquals(2, exit(v))
        assertEquals(RoundaboutShape.MIDDLE.wire, shape(v))
        assertEquals(1, rightHand(v))
        assertEquals(0, v ushr 8)                // reserved
    }

    @Test
    fun `left-hand traffic is zero`() {
        // The watch's artwork is drawn for left-hand traffic, so an all-default detail
        // has to mean exactly that.
        assertEquals(0, NavDetail.pack(NavState(maneuver = Maneuver.UTURN)))
        assertEquals(1, rightHand(NavDetail.pack(NavState(Maneuver.UTURN, traffic = TrafficSide.RIGHT))))
    }

    @Test
    fun `exit and shape are dropped off a roundabout`() {
        val v = NavDetail.pack(
            NavState(
                maneuver = Maneuver.LEFT,
                exit = 3,
                roundabout = RoundaboutShape.LATE,
                traffic = TrafficSide.RIGHT,
            ),
        )
        assertEquals(0, exit(v))
        assertEquals(0, shape(v))
        assertEquals(1, rightHand(v))
    }

    @Test
    fun `an exit the field cannot hold is not said`() {
        for (bad in listOf(-1, 0, 16, 40)) {
            val v = NavDetail.pack(NavState(maneuver = Maneuver.ROUNDABOUT, exit = bad))
            assertEquals("exit $bad", 0, exit(v))
        }
        assertEquals(15, exit(NavDetail.pack(NavState(maneuver = Maneuver.ROUNDABOUT, exit = 15))))
    }

    @Test
    fun `every shape round-trips`() {
        for (s in RoundaboutShape.values()) {
            val v = NavDetail.pack(NavState(maneuver = Maneuver.ROUNDABOUT, exit = 1, roundabout = s))
            assertEquals(s.wire, shape(v))
        }
    }
}
