# Architecture

`proto` is two programs that cooperate over Bluetooth:

```
┌────────────────────────────┐            AppMessage             ┌──────────────────────────┐
│  Phone companion           │  Heartbeat, CalEvents, CalFlags,  │  Pebble watchface        │
│  (pipe/ — Android)         │  NavManeuver/Distance/Unit,       │  (watchface/)            │
│  reads the calendar and    │  PhoneBattery                     │  draws a four-hour       │
│  the phone's battery       │  (phone → watch)                  │  timeline down the left  │
│                            │  ───────────────────────────────► │  edge, plus a clock and  │
│                            │   on change, plus a liveness beat │  three rows beside it    │
└────────────────────────────┘                                   └──────────────────────────┘
```

The dividing line: **the phone decides meaning, the watch decides pixels.** The
companion resolves which entries exist, when they start, how long they run, and
whether one is an appointment or a reminder. The watch turns that into bands,
wedges and colours. No colour crosses the wire, because the companion cannot know
which watch model is on the other end — and one of the three is black-and-white.

## What the face answers

Not "how many things are waiting for me" — that was the previous design, and it needed
a notification listener to count things. This one answers **"what are my next few
hours, and is anything wrong right now"**.

- **The strip is a four-hour timeline down the left edge**, later always lower: one hour
  above the pointer, three below, notched every fifteen minutes with the hours thicker.
  Appointments are bands spanning their duration; tasks and reminders are wedges at their
  exact position, merged only where two would overlap.
- **The pointer never moves.** It sits at the quarter mark and the ruler scrolls past it.
- **The clock is plain digits**, level with the pointer, honouring the 12/24-hour setting.
- **The countdown** counts down to whatever is next, or up through whatever is running —
  and a progress bar under the digits is what says which.
- **Nav, then warnings**, stacked below, each drawn only when it has something to say.
- **Nothing else is drawn.** An idle face is the strip, the clock, and the date.

## Certainty, restated

The previous design expressed a loss of certainty by *hiding* its phone-fed icons: it
had counts, a count cannot be checked, and a stale one is a silent lie.

Calendar entries are not counts. An entry is timestamped, so it ages out on its own,
and a marker that is hours old has already scrolled off the strip. That changes the right
answer: **uncertainty becomes something the face states rather than something it
silently omits.** "Companion disconnected" is the bottom row's first priority, and the
calendar keeps drawing above it.

The watch still owns three things outright — the time, the date and its own battery —
and those are never gated on anything.

## Components

### `watchface/` — the Pebble watchapp

One window, one layer, one update proc, split across seven small modules.

| File | Owns |
| --- | --- |
| `proto.c` | Lifecycle, the service handlers, and the paint order. |
| `geometry.{c,h}` | The track, and the vertical layout. |
| `theme.h` | The whole palette and the three font choices. |
| `events.{c,h}` | The event table, the live window, the linger rules, and which entry the countdown should show. |
| `strip.{c,h}` | Bands, notches, markers, the pointer. |
| `slots.{c,h}` | The three conditional rows, and every glyph. |
| `wire.{c,h}` | The AppMessage inbox and the two watchdogs. |
| `wbatt.{c,h}` | The watch's own hours-remaining estimate. |

**The track** is the one abstraction the renderer needs. `track_at(lo, u)` maps a
position in the visible window — seconds from its top — to a point on the boundary plus
the ray angle there. On a rectangle that angle is a constant 270°, which is to say a
left-edge strip *is* the old dial's nine-o'clock ray: `step_in` moves inward toward the
content, `step_side` moves along the track, and every primitive written for a ring works
unchanged. On `gabbro` the angle sweeps a quarter turn down the left arc. One code path,
two shapes.

The cosine correction the dial needed is gone, and its absence is the point of the shape
rather than an omission. A depth in pixels is only perpendicular to the boundary if the
ray is the boundary's normal; on a rectangle traced around its perimeter it is not, and a
band measured a third thinner at the corners. A circle's ray is its normal, and so is a
vertical edge's, so both of the strip's shapes are square to their own boundary and a
pixel count is already perpendicular.

**Bands** are one representation doing all the work: `uint8_t coverage[240]`, one byte
per *minute* of the visible window, holding the most prominent thing happening then.
Overlapping appointments flatten because they write the same array, and `max()` makes a
merged band inherit the more urgent member's weight. A minute is under a pixel of track
on all three displays, so quantising to one costs nothing visible.

Prominence never depends on colour, because `flint` has none. A running appointment is
drawn to about two thirds of the notch zone's depth, an upcoming one to a third — both
stopping short of the notch inner-ends, so the strip reads as a ruler carrying a marker
rather than as a coloured bar with ticks on it. A grouped marker is a deeper spike.
Colour, where there is any, is layered on top of distinctions that already work without
it.

A linear track pays for one of those distinctions outright: **overdue is above the
pointer and upcoming is below it**, always. The dial could not say that — every point on
a twelve-hour ring is both past and future — and it spent fill on the difference,
drawing `flint`'s upcoming marker hollow. Position says it now, so every marker is solid
everywhere, which is also what makes one legible where it crosses a band in the same ink.

The notches' place in the stack is the one thing that differs by display. On `emery` and
`gabbro` they are drawn last, ink over everything, so an unbroken ruler runs across the
bands and the markers both. `flint` has no hue to carry the band, so a running one fills
the zone in the same ink as the notches; there they invert to background where a running
band crosses them, and they stay underneath the markers so that cut never splits a
marker in half.

**The pointer** is why any of it is readable: positions on the strip need a "now" to be
measured against. It reaches *out* at the track where markers reach *in* from it, which
is what keeps the two apart, and it is drawn last, after everything, because the clock is
pinned to it and the clock's background knockout would otherwise erase it.

**The clock lines up with the pointer's body**, not with the arc point its apex touches.
Identical on a rectangle; on `gabbro` the ray runs down and to the right, so the wedge
sits a dozen pixels below that point and levelling the clock with the apex reads as
floating above it.

### `pipe/` — the Android companion

Kotlin + Jetpack Compose, `namespace link.dendritik.proto.pipe`.

| Piece | Role |
| --- | --- |
| `PipeEngine` | Everything with a lifecycle, and no opinion about what keeps the process alive. |
| `PipeCompanionService` | The host. Bound by the system while the associated watch is nearby. |
| `PipeService` | The fallback host: a foreground service, and the notification that costs. |
| `PipeHost` | `chooseHost`, and the `CompanionDeviceManager` calls around it. |
| `CalendarSource` | Queries `CalendarContract.Instances` over the window the watch can draw. |
| `CalendarWatcher` | `ContentObserver` plus `ACTION_PROVIDER_CHANGED`. |
| `PhoneBattery` | `ACTION_BATTERY_CHANGED`, filtered to whole-percent changes. |
| `EventBlob` / `EventDiff` | Pure. Packing and diffing, covered by JVM unit tests. |
| `PebbleSender` | Debounce, coalesce, dedup, chunk, and the heartbeat the tick owes. |
| `BootReceiver` | Restarts the service after a reboot. |

Framework types stop at `CalendarSource`. Everything below it sees `EventFacts`, which
is what lets the whole wire format be tested with no device and no Robolectric — the
same property the previous design's `Classifier`/`ActiveSet` had, kept deliberately.

**It got its host back.** The previous design piggy-backed on a bound
`NotificationListenerService`, which the system kept alive for its own reasons. With the
shade no longer being read there was no such host, and calendar observation, the phone's
battery and the periodic tick all have to outlive the activity — so the first version of
this redesign paid for a process keeper with a permanent notification.

`CompanionDeviceManager` is the same kind of arrangement, and the one Android actually
built for this shape of app. The user associates their watch once, through a system
dialog that does its own Bluetooth scanning, and from Android 12 the platform binds
`PipeCompanionService` for as long as that watch is nearby. No notification, no
Bluetooth permission of ours, no battery-optimisation exemption to ask for. When the
watch is away nothing runs, which is not a compromise: the watch is the only consumer,
`syncCalendar` already stands down when it cannot see one, and the protocol says a
companion-down verdict does not survive a Bluetooth gap.

**Two hosts, one engine.** `PipeEngine` owns the collaborators, the tick and the
reconcile; it does not know which host is holding it. `PipeService` — the foreground
service, notification and all — remains for Android 11 and earlier, and for anyone who
declines the pairing dialog. Exactly one of the two runs, and `chooseHost(sdkInt,
hasAssociation)` is the only thing that decides which. It is a pure function because the
interesting question about this design — whether the platform's binding really is a
dependable process keeper on a given release — is one only a night on a real device can
answer, and raising its floor should be a one-line change.

The tick is where the two hosts differ least and it matters most: while bound, the
process is warm, so the alarm behaves exactly as it does under the foreground service.
Going notification-free cost nothing on the wire — same keys, same declared cadence, the
watchface untouched.

## Data flow

1. Something happens: the calendar changed, the watch reconnected, or fifteen minutes
   passed and the six-hour window slid forward. All three land on one method,
   `PipeEngine.reconcile`.
2. `CalendarSource` scans `[now − 2 h, now + 6 h]`, excluding whole-day entries — they
   have no position on a timeline and no duration that would fit one — and anything
   cancelled. Duration comes from `END - BEGIN`; a zero-length instance is a reminder.

   The phone's window is deliberately wider than the strip's `[now − 1 h, now + 3 h]`.
   Entries past the horizon sit in the watch's table undrawn and scroll into view as the
   window slides, which costs one message instead of one per quarter hour.
3. `EventDiff` compares the scan against what the companion believes the watch holds.
   A reconnect skips the diff and sends a **flush** instead, because a watchface that
   relaunched holds nothing.
4. `EventBlob` packs the records, twenty-four per message, and `PebbleSender` sends
   them with `FLUSH`/`MORE` framing and a `Heartbeat`.
5. `wire.c` decodes, upserts and removes against a fixed 32-slot table, and marks the
   layer dirty.
6. The next paint recomputes the coverage array from scratch and draws it. Every
   marker's position, prominence and existence is a function of `now`, so the minute
   tick is also what advances the countdown, scrolls the ruler past the pointer, and
   retires whatever has aged out.

Steps 1–6 are the change path, and how fast a change arrives is governed by the
`ContentObserver`: the scan and the send happen synchronously off it, so a calendar edit
reaches the watch in about as long as a `CalendarContract.Instances` query takes.

Underneath that, one periodic tick does the two jobs that need a clock, and it is a
single `setAndAllowWhileIdle` alarm because Doze throttles that call per app rather than
per alarm — a second one at the same period would only make the first late. So the tick
re-scans the slid window, sends the delta if there is one, and otherwise speaks a bare
heartbeat. Proving liveness needs no separate loop, because **any message arriving is the
proof**; the `Heartbeat` key exists so a companion with no news can still say something.
If the watch hears nothing for 2.5 declared periods, the bottom row says so.

**Calendar content never leaves the phone.** Titles, locations, attendees and
descriptions are never read. The watch receives a position, a duration and two enum
bytes per entry.

## Build boundaries

`watchface/` builds with the Pebble SDK (`waf` via the `pebble` CLI); output goes to
`watchface/build/`, which is generated and gitignored. `pipe/` builds with Gradle
(`./gradlew assembleDebug`, JDK 21). Each component is self-contained; the only thing
they share is the protocol.
