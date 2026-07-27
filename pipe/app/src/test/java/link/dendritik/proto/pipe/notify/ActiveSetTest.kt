package link.dendritik.proto.pipe.notify

import link.dendritik.proto.pipe.protocol.IconState
import link.dendritik.proto.pipe.protocol.PhoneState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Aggregation and, most importantly, the return-to-faded-on-dismissal path. */
class ActiveSetTest {

    @Test
    fun `chats accumulate and the envelope fades when the last one is dismissed`() {
        val set = ActiveSet()
        set.put("a", Verdict.Chat)
        set.put("b", Verdict.Chat)
        assertEquals(2, set.snapshot().unreadCount)

        set.remove("a")
        assertEquals(1, set.snapshot().unreadCount)

        set.remove("b")
        assertEquals(IconState.IDLE, set.snapshot())
    }

    @Test
    fun `the phone icon fades once every call notification is gone`() {
        val set = ActiveSet()
        set.put("call", Verdict.Call(PhoneState.MISSED))
        assertEquals(PhoneState.MISSED, set.snapshot().phone)
        assertEquals(1, set.snapshot().missedCount)

        set.remove("call")
        assertEquals(PhoneState.IDLE, set.snapshot().phone)
        assertEquals(0, set.snapshot().missedCount)
    }

    @Test
    fun `re-posting the same key updates rather than double counts`() {
        val set = ActiveSet()
        set.put("k", Verdict.Chat)
        set.put("k", Verdict.Chat)
        assertEquals(1, set.snapshot().unreadCount)

        // A missed call that becomes a ringing call — same notification, new state.
        set.put("c", Verdict.Call(PhoneState.MISSED))
        set.put("c", Verdict.Call(PhoneState.RINGING))
        assertEquals(PhoneState.RINGING, set.snapshot().phone)
        assertEquals(0, set.snapshot().missedCount)
    }

    @Test
    fun `an ignore verdict drops a key that was previously counted`() {
        val set = ActiveSet()
        set.put("k", Verdict.Chat)
        // e.g. a ranking update silenced the channel.
        assertTrue(set.put("k", Verdict.Ignore))
        assertEquals(IconState.IDLE, set.snapshot())
    }

    @Test
    fun `put reports whether the snapshot actually changed`() {
        val set = ActiveSet()
        assertTrue(set.put("k", Verdict.Chat))
        // Same key, same verdict: the watch already knows.
        assertFalse(set.put("k", Verdict.Chat))
        assertFalse(set.put("ignored", Verdict.Ignore))
    }

    @Test
    fun `remove reports whether anything was tracked`() {
        val set = ActiveSet()
        set.put("k", Verdict.Chat)
        assertTrue(set.remove("k"))
        assertFalse(set.remove("k"))
        assertFalse(set.remove("never-seen"))
    }

    @Test
    fun `ringing outranks ongoing which outranks missed`() {
        assertEquals(
            PhoneState.RINGING,
            ActiveSet.fold(
                listOf(
                    Verdict.Call(PhoneState.MISSED),
                    Verdict.Call(PhoneState.ONGOING),
                    Verdict.Call(PhoneState.RINGING),
                )
            ).phone,
        )
        assertEquals(
            PhoneState.ONGOING,
            ActiveSet.fold(
                listOf(Verdict.Call(PhoneState.MISSED), Verdict.Call(PhoneState.ONGOING))
            ).phone,
        )
    }

    @Test
    fun `missed calls are counted even while a newer call outranks them`() {
        val state = ActiveSet.fold(
            listOf(
                Verdict.Call(PhoneState.MISSED),
                Verdict.Call(PhoneState.MISSED),
                Verdict.Call(PhoneState.RINGING),
            )
        )
        assertEquals(PhoneState.RINGING, state.phone)
        assertEquals(2, state.missedCount)
    }

    @Test
    fun `reset replaces the whole set as after a listener reconnect`() {
        val set = ActiveSet()
        set.put("stale", Verdict.Chat)
        set.reset(
            mapOf(
                "fresh" to Verdict.Chat,
                "call" to Verdict.Call(PhoneState.ONGOING),
                "dropped" to Verdict.Ignore,
            )
        )
        val state = set.snapshot()
        assertEquals(1, state.unreadCount)
        assertEquals(PhoneState.ONGOING, state.phone)
    }

    @Test
    fun `an empty set is the all-faded state`() {
        assertEquals(IconState.IDLE, ActiveSet.fold(emptyList()))
        assertEquals(0, IconState.IDLE.unreadCount)
        assertEquals(PhoneState.IDLE, IconState.IDLE.phone)
    }
}
