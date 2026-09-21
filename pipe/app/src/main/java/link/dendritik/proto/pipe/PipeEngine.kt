package link.dendritik.proto.pipe

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.SystemClock
import android.util.Log
import androidx.core.content.ContextCompat
import link.dendritik.proto.pipe.battery.PhoneBattery
import link.dendritik.proto.pipe.calendar.CalendarSource
import link.dendritik.proto.pipe.calendar.CalendarWatcher
import link.dendritik.proto.pipe.nuron.NuronHolder
import link.dendritik.proto.pipe.nuron.NuronSource
import link.dendritik.proto.pipe.nuron.NuronWatcher
import link.dendritik.proto.pipe.pebble.MergePolicy
import link.dendritik.proto.pipe.pebble.PebbleSender
import link.dendritik.proto.pipe.protocol.Protocol

/**
 * The delay before the next tick: [baseMs] doubled once per consecutive miss, never past
 * [capMs].
 *
 * Pure, and unit-tested, for exactly the reason [chooseHost] is — the wrong answer here
 * is invisible. The base period is chosen against a platform budget (Doze allows a
 * while-idle alarm no more than once per nine minutes per app), so a ladder that ever
 * returned *less* than the base would breach that budget silently, and the symptom would
 * be a watchface raising a companion-down alert overnight against a phone that looked
 * perfectly healthy. The one invariant worth stating: the result is always in
 * `[min(baseMs, capMs), capMs]`.
 *
 * It doubles by a loop rather than a shift so that no miss count, however wild, can
 * overflow into a short delay, and the loop exits the moment the cap is reached rather
 * than spinning out the remaining count — so an absurd `misses` costs three iterations,
 * not two billion.
 */
fun backoffDelayMs(baseMs: Long, misses: Int, capMs: Long): Long {
    var delay = baseMs
    var remaining = misses
    while (remaining > 0 && delay < capMs) {
        delay *= 2
        remaining--
    }
    return delay.coerceAtMost(capMs)
}

/**
 * Everything with a lifecycle, and none of the reasons a process stays alive.
 *
 * The engine does not care which host is holding it open — [PipeService] with a
 * foreground notification, or [PipeCompanionService] which the system binds for as long
 * as the associated watch is nearby. Both do exactly the same two things: construct one
 * of these and call [start], then [stop].
 *
 * Four change signals feed one action — re-scan and reconcile:
 *  - the calendar changed (an edit here, or a sync landing from the server)
 *  - the watch reconnected, which forces a full flush rather than a diff
 *  - time passed, so the six-hour window slid; nothing "changed" but the answer did
 *  - the user pressed re-sync, which is the only one of the four that is not a change
 *
 * The third of those is also the liveness heartbeat, and there is exactly one alarm for
 * both — see [tick]. The fourth arrives as a broadcast, because nothing outside a host
 * holds an engine to call, and it is the only one that leaves the alarm alone — see
 * [resync].
 *
 * Not thread-safe. Every host calls it on the main thread.
 */
class PipeEngine(private val context: Context) {

    private val sender = PebbleSender(context)
    private val calendar = CalendarSource(context)
    private val battery = PhoneBattery(context) { sender.submitBattery(it) }
    private val watcher = CalendarWatcher(context) { reconcile(flush = false) }

    // The second source. It contributes the same EventFacts as the calendar and
    // is merged into one table by MergePolicy, because the watch has one table
    // and FLUSH semantics: two independent senders would each drop the other's
    // entries on every flush.
    private val nuron = NuronSource(context)
    private val nuronHolder = NuronHolder()
    private val nuronWatcher = NuronWatcher(context) { reconcile(flush = false) }

    private val alarms = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
    private var receiver: BroadcastReceiver? = null

    /** Idempotent: a host may re-announce a watch it has already told us about. */
    private var started = false

    /**
     * Consecutive flushes that did not reach the watch, and therefore the backoff
     * exponent for the next tick.
     *
     * Cleared by any successful send from any path — a delta off a calendar edit and the
     * flush on reconnect are both proof the link works, and neither of them is a tick.
     */
    private var misses = 0

    fun start() {
        if (started) return
        started = true
        misses = 0          // a new session starts at the base period, not the old one's

        sender.onWatchConnected = { reconcile(flush = true) }
        sender.start()
        watcher.start()
        nuronWatcher.start()
        battery.start()
        startTick()

        // A flush, not a diff: we have no idea what the watch is holding at this
        // point, and a watchface that relaunched while we were dead has nothing.
        reconcile(flush = true)
        PipeStatus.running = true
    }

    fun stop() {
        if (!started) return
        started = false

        PipeStatus.running = false
        stopTickAlarm()
        receiver?.let { runCatching { context.unregisterReceiver(it) } }
        receiver = null
        battery.stop()
        watcher.stop()
        nuronWatcher.stop()
        sender.stop()
    }

    // ---------------------------------------------------------------------------
    // The one action
    // ---------------------------------------------------------------------------

    /** Returns whether anything reached the watch. */
    private fun reconcile(flush: Boolean): Boolean {
        val now = System.currentTimeMillis()
        val calendarEvents = calendar.query(now)
        // The holder is what keeps a transient provider fault from wiping every
        // Nuron marker, and what stops a permanently broken one from pinning
        // stale markers for ever. See NuronHolder.
        val nuronEvents = nuronHolder.accept(nuron.scan())
        val events = MergePolicy.merge(calendarEvents, nuronEvents)

        PipeStatus.eventCount = events.size
        PipeStatus.calendarEventCount = calendarEvents.size
        PipeStatus.nuronEventCount = nuronEvents.size
        PipeStatus.nuronState = nuronHolder.state
        PipeStatus.calendarGranted = calendar.hasPermission()
        PipeStatus.watchConnected = sender.isWatchConnected()
        val sent = sender.syncCalendar(events, flush)
        if (sent) {
            PipeStatus.lastSyncAtMs = System.currentTimeMillis()
            noteSuccess()
        }
        return sent
    }

    // The two writers of [misses], and the only ones. Deliberately asymmetric, because
    // `syncCalendar` returns false for two unlike reasons: it could not send, or a delta
    // came out empty. Only the first is a failure, and only a *flush* can tell them
    // apart — a flush always has something to say, even with no records, so a false from
    // one means the link. An empty delta is the ordinary case: Android's calendar
    // provider fires its observer on all manner of internal churn, and counting those as
    // misses would back the tick off to an hour on a link that never faltered.
    //
    // So: any success resets, wherever it came from, because a delta that landed and a
    // reconnect flush that landed are both proof the link works. Only [flushNow] counts
    // a miss.

    /**
     * Something got through. Clear the backoff, and pull the alarm in if it had one.
     *
     * The re-arm is one-directional on purpose. Lengthening the delay is the tick's own
     * business — it re-arms itself after every firing, by which point [misses] is already
     * up to date — so arming from here only ever shortens a backed-off delay back to the
     * base period. A success between ticks must never push the next one out; that would
     * spend the watch's grace on a message the companion chose not to send.
     */
    private fun noteSuccess() {
        if (misses == 0) return
        misses = 0
        if (receiver != null) {
            Log.i(TAG, "link recovered; back to the base period")
            armTick()
        }
    }

    /** A flush that could not be delivered. The next tick waits twice as long. */
    private fun noteMiss() {
        misses = (misses + 1).coerceAtMost(BACKOFF_MAX_MISSES)
        Log.i(TAG, "flush did not land; next tick in ${tickDelayMs() / 1000}s")
    }

    /**
     * The flush the periodic tick and the manual button both want.
     *
     * Returns whether anything landed, and is the only path that counts a miss — see
     * [noteMiss] for why a flush is the only send that can tell failure from silence.
     *
     * The beat covers the one case a flush cannot: with
     * the watch connected `reconcile(flush = true)` always sends at least one message —
     * a zero-record flush is how "the next six hours are clear" is said — so `false`
     * here with a live link means the send threw, and the beat is the retry. With the
     * watch away [PebbleSender.beat] stands down on its own.
     */
    private fun flushNow(): Boolean {
        val sent = reconcile(flush = true)
        if (!sent) {
            noteMiss()
            sender.beat()
        }
        return sent
    }

    /**
     * The periodic wake-up: one alarm doing both of the jobs that need one.
     *
     * Re-scanning is the obvious one — nothing signals "the window moved". Proving we
     * are alive is the other, and a payload already proves it, because every message
     * carries [Protocol.KEY_HEARTBEAT]. So the beat is only owed when the re-scan sent
     * nothing at all, which now only happens when the watch is unreachable.
     *
     * These were two alarms at the same period, which was worse than merely redundant:
     * Doze throttles `setAndAllowWhileIdle` per *app*, so the pair competed for one
     * budget and made each other late — a heartbeat that could not keep the cadence it
     * declared.
     *
     * **The tick flushes rather than diffing, and that is the one place this companion
     * speaks on a timer with nothing new to say.** Changing watchface does not drop the
     * Bluetooth link, so nothing signals it; every delta sent while a face was off
     * screen went to a UUID that NACKed it. The face restores its own table when it
     * relaunches, so the swap is no longer a blank timeline — but that table is as of
     * the last time that face was on screen. Since there are two faces and switching
     * between them is the ordinary thing to do with them, the periodic re-flush is what
     * makes the swap self-healing. It costs 324 bytes against 15 bytes
     * once a period. The change-driven path in [reconcile] still sends a delta — this
     * exception is the tick's alone. See `docs/protocol.md`.
     *
     * **A tick whose flush did not land backs the next one off**, doubling from the base
     * 600 s to a 3600 s cap — see [tickDelayMs]. It is not a retry of the *message*:
     * nothing is queued and nothing is resent, because the next tick recomputes the
     * whole window anyway and a stale blob is not worth keeping. What backs off is how
     * hard we try. Backoff only lengthens, so it cannot breach the Doze budget the base
     * period is picked against, and while the watch is unreachable it is the difference
     * between six pointless wake-ups an hour and one.
     */
    fun tick() {
        flushNow()
    }

    /**
     * A flush the user asked for, from the button in [MainActivity].
     *
     * Byte-identical to the tick's flush — same keys, same framing, same declared
     * cadence — so it is not a new thing on the wire, only a new reason for the old one.
     *
     * It never pushes the alarm out. The cadence is a promise about when the *next*
     * scheduled message arrives, and re-arming at the base period on a press would slide
     * that a whole period; the watch's grace is three periods, so a button could bring on
     * the very alert it is meant to clear. The one thing a press may do is pull the alarm
     * *in*, via [noteSend], when it succeeds out of a backed-off state — which is a
     * shortening, and the recovery the backoff is waiting for.
     *
     * Nor is a press rate-limited. The reason to press it is that the watch is suspected
     * of holding a stale table, and a button that sometimes declines cannot settle that.
     * The UI debounces itself while the spinner is up, which is a different thing.
     */
    fun resync() {
        Log.i(TAG, "manual re-sync")
        PipeStatus.resync = if (flushNow()) Resync.SENT else Resync.NOTHING_SENT
    }

    // ---------------------------------------------------------------------------
    // The tick
    // ---------------------------------------------------------------------------

    /**
     * The app's only `AlarmManager` wake-up.
     *
     * Its period is the declared heartbeat cadence, because that is the tighter of the
     * two constraints it serves; for the window it is generous, the dial quantising to
     * twelve minutes. `setAndAllowWhileIdle` is the only scheduler that survives Doze
     * without `SCHEDULE_EXACT_ALARM`, which Android 14 no longer grants by default and
     * which Play reserves for actual alarm-clock apps. It must be a `_WAKEUP` alarm:
     * the non-waking variant would sit until the device stirred for some other reason,
     * which can be hours.
     *
     * The receiver is registered at runtime rather than in the manifest. A manifest
     * receiver would let the system restart a dead process just to announce that it is
     * alive — which is the one thing a liveness heartbeat must never be able to claim.
     * [ACTION_RESYNC] rides the same registration, for that same reason turned around: a
     * button press must not be able to resurrect the process either, and a press that
     * finds no engine running is dropped rather than answered by one that has just come
     * up knowing nothing.
     */
    private fun startTick() {
        if (receiver != null) return
        val rcv = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                when (intent.action) {
                    ACTION_TICK -> {
                        tick()
                        armTick()   // one-shot, re-armed, so a missed wake-up cannot compound
                    }
                    ACTION_RESYNC -> resync()
                }
            }
        }
        val filter = IntentFilter(ACTION_TICK).apply { addAction(ACTION_RESYNC) }
        ContextCompat.registerReceiver(
            context, rcv, filter, ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        receiver = rcv
        armTick()
    }

    private fun armTick() {
        alarms.setAndAllowWhileIdle(
            AlarmManager.ELAPSED_REALTIME_WAKEUP,
            SystemClock.elapsedRealtime() + tickDelayMs(),
            tickIntent(),
        )
    }

    /**
     * The base period doubled once per consecutive miss, capped: 600, 1200, 2400, 3600
     * seconds.
     *
     * The cap is the largest period the protocol lets the companion declare, which is
     * not a coincidence — past it the number would be one the watch could not be told
     * even in principle.
     */
    private fun tickDelayMs(): Long = backoffDelayMs(TICK_MS, misses, BACKOFF_CAP_MS)

    private fun stopTickAlarm() = alarms.cancel(tickIntent())

    private fun tickIntent(): PendingIntent = PendingIntent.getBroadcast(
        context, 1,
        Intent(ACTION_TICK).setPackage(context.packageName),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    companion object {

        /**
         * Ask whichever host is holding an engine open for an immediate flush.
         *
         * A broadcast because nothing outside a host has a reference to call: both keep
         * the engine in a private field and [PipeService] does not bind. In-process by
         * construction — the action exists only as a runtime registration — and
         * `setPackage` keeps it there even so.
         */
        fun requestResync(context: Context) {
            context.sendBroadcast(Intent(ACTION_RESYNC).setPackage(context.packageName))
        }

        private const val TAG = "PipeEngine"
        private const val TICK_MS = Protocol.HEARTBEAT_IDLE_S * 1000L

        /** Four rungs: 600, 1200, 2400, then the cap. Further misses change nothing. */
        private const val BACKOFF_MAX_MISSES = 3
        private const val BACKOFF_CAP_MS = 3_600_000L
        private const val ACTION_TICK = "link.dendritik.proto.pipe.TICK"
        private const val ACTION_RESYNC = "link.dendritik.proto.pipe.RESYNC"
    }
}
