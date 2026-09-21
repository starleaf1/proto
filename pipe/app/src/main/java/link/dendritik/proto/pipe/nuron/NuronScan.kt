package link.dendritik.proto.pipe.nuron

import link.dendritik.proto.pipe.calendar.EventFacts

/**
 * The result of asking Nuron what it has, with "nothing" and "no answer" kept apart.
 *
 * ## Why this is three cases and not a list
 *
 * A source that returns a bare list forces every caller to read an empty one as
 * a fact. For the calendar that is harmless — the provider is part of the system
 * and an empty answer really does mean an empty afternoon. For a second app it
 * is not: Nuron can be uninstalled, have its permission revoked, be mid-upgrade,
 * or simply not have run its first sync yet, and every one of those produces
 * zero rows. Treating them as "the user's day is clear" takes every Nuron marker
 * off the watch, and the watch has no way to know it was told something false.
 *
 * This is the same distinction `QuestResolver` draws in the Nuron app itself
 * between a quest that is *deleted* and a quest that is merely *unresolved*, and
 * it is here for the same reason: a cache miss must never be mistaken for a
 * deletion.
 */
sealed interface NuronScan {

    /**
     * Nuron answered. The list may legitimately be empty — that is a clear
     * window, and REMOVEs for anything that has left it are correct.
     */
    data class Facts(val events: List<EventFacts>) : NuronScan

    /**
     * Nuron is not installed, or has not granted the read permission. A stable,
     * durable zero: contributing nothing is the right answer and will stay the
     * right answer until something changes.
     */
    data object Absent : NuronScan

    /**
     * Nuron is there but could not be read — the provider threw, or it answered
     * with rows but a reconciliation stamp of zero, meaning it has never built
     * its projection. Nothing can be concluded, so nothing should be deleted.
     */
    data object Unknown : NuronScan
}
