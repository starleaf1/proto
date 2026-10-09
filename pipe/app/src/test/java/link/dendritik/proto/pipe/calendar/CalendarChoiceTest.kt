package link.dendritik.proto.pipe.calendar

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CalendarChoiceTest {

    private fun cal(id: Long, key: String = "k$id") = CalendarInfo(id, key, "c$id", "a", 0)

    private val work = cal(1, "work")
    private val home = cal(2, "home")
    private val all = listOf(work, home)

    @Test
    fun `no choice yet sends every calendar and does not filter`() {
        // The state an update lands in. Anything else would change the wrist unasked.
        assertTrue(CalendarChoice.isSent("anything", null))
        assertNull(CalendarChoice.sentIds(all, null))
    }

    @Test
    fun `the first untick keeps everything else ticked`() {
        val chosen = CalendarChoice.toggle(all, null, "work", send = false)
        assertEquals(setOf("home"), chosen)
        assertEquals(listOf(2L), CalendarChoice.sentIds(all, chosen))
    }

    @Test
    fun `a calendar that appears after a choice is not sent`() {
        // The direction a privacy control must fail in.
        val chosen = CalendarChoice.toggle(all, null, "work", send = false)
        val added = cal(3, "new")
        assertFalse(CalendarChoice.isSent("new", chosen))
        assertEquals(listOf(2L), CalendarChoice.sentIds(all + added, chosen))
    }

    @Test
    fun `unticking everything is an empty filter, not no filter`() {
        var chosen: Set<String>? = null
        for (c in all) chosen = CalendarChoice.toggle(all, chosen, c.key, send = false)
        assertEquals(emptyList<Long>(), CalendarChoice.sentIds(all, chosen))
    }

    @Test
    fun `a ticked calendar missing for one scan is still ticked when it returns`() {
        val chosen = setOf("work", "home")
        // An account mid-sync lists nothing; a toggle made meanwhile must not prune it.
        val during = CalendarChoice.toggle(listOf(home), chosen, "home", send = false)
        assertTrue(CalendarChoice.isSent("work", during))
    }

    @Test
    fun `the choice follows the key, not the row id`() {
        // Re-adding an account re-creates its calendars under fresh row ids.
        val chosen = setOf("work")
        assertEquals(listOf(41L), CalendarChoice.sentIds(listOf(cal(41, "work")), chosen))
    }

    @Test
    fun `a synced calendar is keyed by account and server id`() {
        val a = CalendarChoice.key(1, "com.google", "me@example.com", "team@group")
        assertEquals(a, CalendarChoice.key(99, "com.google", "me@example.com", "team@group"))
        assertNotEquals(a, CalendarChoice.key(1, "com.google", "you@example.com", "team@group"))
    }

    @Test
    fun `a local calendar falls back to its row id`() {
        assertEquals(CalendarChoice.key(7, "LOCAL", "x", null), CalendarChoice.key(7, null, null, null))
        assertNotEquals(CalendarChoice.key(7, null, null, null), CalendarChoice.key(8, null, null, null))
    }
}
