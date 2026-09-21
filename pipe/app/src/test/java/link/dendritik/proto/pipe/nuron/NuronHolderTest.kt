package link.dendritik.proto.pipe.nuron

import link.dendritik.proto.pipe.calendar.EventFacts
import link.dendritik.proto.pipe.protocol.EventKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NuronHolderTest {

    private fun facts(vararg ids: Int) =
        ids.map { EventFacts(it, 1_000_000, 0, EventKind.TASK) }

    @Test
    fun `a good scan is passed through and remembered`() {
        val h = NuronHolder()
        assertEquals(facts(-1, -2), h.accept(NuronScan.Facts(facts(-1, -2))))
    }

    @Test
    fun `a transient failure holds the last set rather than wiping markers`() {
        // One thrown binder call must not clear the watch. Without this the
        // visible symptom is markers that flicker once and come back.
        val h = NuronHolder()
        h.accept(NuronScan.Facts(facts(-1, -2)))
        assertEquals(facts(-1, -2), h.accept(NuronScan.Unknown))
        assertEquals(facts(-1, -2), h.accept(NuronScan.Unknown))
        assertEquals(facts(-1, -2), h.accept(NuronScan.Unknown))
    }

    @Test
    fun `a persistent failure eventually lets go`() {
        // The opposite failure, and the worse one: a permanently broken provider
        // must not pin stale markers on the wrist for ever. A wrong marker reads
        // as a fact; a missing one ages out.
        val h = NuronHolder()
        h.accept(NuronScan.Facts(facts(-1)))
        repeat(3) { h.accept(NuronScan.Unknown) }
        assertTrue(h.accept(NuronScan.Unknown).isEmpty())
        assertTrue(h.accept(NuronScan.Unknown).isEmpty())
    }

    @Test
    fun `a good scan resets the stale counter`() {
        val h = NuronHolder()
        h.accept(NuronScan.Facts(facts(-1)))
        h.accept(NuronScan.Unknown)
        h.accept(NuronScan.Unknown)
        h.accept(NuronScan.Facts(facts(-1)))
        // The budget is fresh again, so three more failures still hold.
        repeat(3) { assertEquals(facts(-1), h.accept(NuronScan.Unknown)) }
    }

    @Test
    fun `Absent drops immediately`() {
        // Uninstalled or unpermitted is durable and will not resolve itself.
        // Holding markers for an app that is gone is indefensible.
        val h = NuronHolder()
        h.accept(NuronScan.Facts(facts(-1)))
        assertTrue(h.accept(NuronScan.Absent).isEmpty())
    }

    @Test
    fun `a reconciled empty is respected immediately`() {
        val h = NuronHolder()
        h.accept(NuronScan.Facts(facts(-1, -2)))
        assertTrue(h.accept(NuronScan.Facts(emptyList())).isEmpty())
    }
}
