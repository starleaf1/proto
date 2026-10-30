package link.dendritik.proto.pipe.pebble

import link.dendritik.proto.pipe.protocol.Maneuver
import link.dendritik.proto.pipe.protocol.NavState
import link.dendritik.proto.pipe.protocol.RoundaboutShape

/**
 * Packs the maneuver's qualifiers into the `int32` `Protocol.KEY_NAV_DETAIL` carries.
 *
 * ```
 * bits 0-3   exit, 1-15; 0 not said
 * bits 4-6   roundabout shape
 * bit  7     traffic side, 0 left-hand
 * ```
 * Bits above 7 are reserved and sent as zero.
 *
 * The exit and the shape mean nothing off a roundabout and are sent as zero there, so
 * the watch never sees a stale exit number under a plain turn. The traffic side goes
 * out on every maneuver: it is cheap, and the u-turn reads it too.
 */
object NavDetail {
    const val MAX_EXIT = 15

    fun pack(nav: NavState): Int {
        val side = nav.traffic.wire shl 7
        if (nav.maneuver != Maneuver.ROUNDABOUT) return side
        // An exit the field cannot hold is sent as "not said" rather than clamped: a
        // clamped 15 would be a number on the watch that is not the exit to take.
        val exit = if (nav.exit in 1..MAX_EXIT) nav.exit else 0
        return exit or (nav.roundabout.wire shl 4) or side
    }
}
