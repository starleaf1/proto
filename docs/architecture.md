# Architecture

`proto` is three programs that cooperate over Bluetooth — two watchfaces and the one
companion that feeds them both:

```
                                                  ┌──────────────────────────────────┐
                                               ┌─►│ watchface-digital/               │
┌──────────────────────────────┐  AppMessage   │  │ a timeline down the left edge,   │
│ Phone companion              │  Heartbeat,   │  │ four hours of it or six, plus a  │
│ (pipe/, Android)             │  CalEvents,   │  │ clock and three rows beside it   │
│                              │  CalFlags,    │  └──────────────────────────────────┘
│ reads the calendar and the   │───────────────┤
│ phone's battery, and         │  Nav*, Phone  │  ┌──────────────────────────────────┐
│ addresses every message to   │  Battery      └─►│ watchface-analog/                │
│ both UUIDs                   │                  │ a six-hour timeline round the    │
└──────────────────────────────┘  on change,      │ rim of an analog dial, with      │
                                  plus a          │ the date and the countdown       │
                                  liveness beat   │ at the centre                    │
                                                  └──────────────────────────────────┘
```

The two faces are independent apps with their own UUIDs, built and installed separately.
They share the protocol and a design vocabulary — an appointment is a band, a task is a
blunt wedge, a running thing is twice the depth of an upcoming one, red is spent on now
— and no code that knows about pixels. The three modules that do not (`events`, `wire`,
`wbatt`) exist in both by copy.

The dividing line: **the phone decides meaning, the watch decides pixels.** The
companion resolves which entries exist, when they start, how long they run, and
whether one is an appointment or a reminder. The watch turns that into bands,
wedges and colours. No colour crosses the wire, because the companion cannot know
which watch model is on the other end — and one of the three is black-and-white.

## What the faces answer

Not "how many things are waiting for me" — that was the previous design, and it needed
a notification listener to count things. Both of these answer **"what are my next few
hours, and is anything wrong right now"**. They differ in what they make cheap to read.

Shared by both:

- **Appointments are bands** spanning their duration; **tasks and reminders are blunt
  wedges** at their exact position, merged only where two would overlap.
- **A running thing is twice the depth of an upcoming one.** Depth, not density and not
  hue, because depth is a shape and works on a display with one ink.
- **The countdown** counts down to whatever is next, or up through whatever is running —
  and the sign in front of the digits is what says which: `-` before it starts, `+`
  once it has.
- **Nav, then warnings**, each drawn only when it has something to say.
- **Nothing else is drawn.** An idle face is the timeline, the clock and the date.

`watchface-digital/` — **now is fixed and the calendar moves:**

- **The strip is a timeline down the left edge**, later always lower, notched every
  fifteen minutes with the hours thicker. Four hours on the rectangles — one above the
  pointer, three below — and six on `gabbro`, one above and five below.
- **The pointer never moves.** The ruler scrolls past it: at the quarter mark of a
  straight strip, and at 330° of `gabbro`'s half-turn arc.
- **`gabbro`'s six hours over half a turn is thirty degrees to the hour**, which is the
  analog face's rate and an analog clock's own hour spacing. What the two do with it is
  opposite: there the entry sits at its own clock angle and the hand moves; here the rule
  is fixed and the scale slides under it, so a three o'clock meeting is *not* at the 3.
- **The clock is plain digits**, honouring the 12/24-hour setting — level with the
  pointer on the rectangles, and below it on `gabbro`, where the chord beside the mark is
  narrower than the digits are wide.
- The date, the countdown, nav and the warnings stack downward beside the strip.

`watchface-analog/` — **the calendar is fixed and now moves:**

- **The ring is a six-hour timeline round the rim** — one hour behind, five ahead — with
  each entry at the clock angle of its own time, so a three o'clock meeting is at the 3.
  A rail spans the window and stops at an end cap; past it, the ring is not empty, it is
  not being shown. With nothing inside the window the rail goes too, leaving a plain
  analog clock rather than an empty scale.
- **There is no now mark.** Six hours over half a turn is thirty degrees to the hour,
  which is the hour hand's own rate, so the hand points at the present position on the
  timeline by construction. It stops three pixels short of the ring and never enters it:
  the markers have one lane and the hand does not compete for it.
- **The twelve dial ticks are the ring's scale as well as the clock's.** Graduating the
  ring separately would put two scales in two concentric lanes.
- **The date and the countdown sit in a disc at the centre**, drawn over the hands. It
  covers the half of each hand that carries no reading, and it is the only place on that
  face where text is safe from a hand at every minute of the day.

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

### `watchface-digital/` — the left-edge timeline

One window, one layer, one update proc, split across seven small modules.

| File | Owns |
| --- | --- |
| `proto.c` | Lifecycle, the service handlers, and the paint order. |
| `geometry.{c,h}` | The track, and the vertical layout. |
| `theme.h` | The whole palette and the four system-font choices. |
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
unchanged. On `gabbro` the angle sweeps half a turn, twelve o'clock round to six. One
code path, two shapes.

The cosine correction the dial needed is gone, and its absence is the point of the shape
rather than an omission. A depth in pixels is only perpendicular to the boundary if the
ray is the boundary's normal; on a rectangle traced around its perimeter it is not, and a
band measured a third thinner at the corners. A circle's ray is its normal, and so is a
vertical edge's, so both of the strip's shapes are square to their own boundary and a
pixel count is already perpendicular.

**Bands** are one representation doing all the work: `uint8_t coverage[]`, one byte per
*minute* of the visible window — 240 of them on the rectangles and 360 on `gabbro` —
holding the most prominent thing happening then.
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

**The clock lines up with the pointer's body**, not with the arc point its apex touches —
identical on a rectangle, and on an arc the ray runs down and to the right, so the mark's
body sits some way below the point it touches.

On `gabbro` it now lines up with neither. Half a turn puts the rule 30° back from twelve
o'clock, near the top of the glass, where the chord left beside the strip is about 60 px
against the ~100 `"00:00"` measures — and no font size recovers it, the chord being
narrower still further up. So the clock drops to the highest row that will hold it and
hangs off the mark rather than sitting beside it. The floor is solved from the clock's
own measured width through `fit_row`'s width rule rather than tuned, so it follows a font
change by itself, and it falls straight down rather than along the ray because `fit_row`
gives every round row the same centre column — `y` is the only free variable there. The
same half turn curls the hour-label lane over the top of the glass and into that row,
which is why `gabbro` drops the label nearest now exactly as `flint` drops the one under
its wedge, and loses the same nothing: the clock beside the gap is naming that very hour.

### `watchface-analog/` — the analog dial

One window, one layer, one update proc, split across the same shape of module set.

| Module | Owns |
| --- | --- |
| `proto.c` | Lifecycle, service handlers, paint order, the demo seed. |
| `dial.{c,h}` | The rail, the bands, the wedges, the ticks and the hands. |
| `geometry.{c,h}` | The radial ladder, the angle mapping, the disc's rows. |
| `theme.h` | The palette and the two system-font choices. |
| `events.{c,h}` | The event table, the live window, linger rules, countdown choice. |
| `slots.{c,h}` | The countdown and the two notification pairs, and every glyph. |
| `wire.{c,h}` | The AppMessage inbox and the two watchdogs. |
| `wbatt.{c,h}` | The watch's own hours-remaining estimate. |

`events`, `wire` and `wbatt` are the digital face's files, copied. They carry no
rendering knowledge, and they only *read* the window constants rather than defining
them — which is what let the digital face's window become per-platform without the copies
drifting. It is 1 h and 5 h here; there it is 1 h and 3 h on the rectangles and the same
1 h and 5 h on `gabbro`.

**Everything is polar, and there is one angular scale.** `angle_of_time()` maps a
wall-clock instant to a dial angle — `((minutes % 720) * TRIG_MAX_ANGLE) / 720`, into
trig units with no intermediate degrees, because a whole degree is two minutes of the
ring and 2.2 px of `gabbro`'s rim. Six hours is 360 minutes over half a turn, so the ring
runs at exactly the hour hand's rate and the hand needs no explanation.

No cosine correction anywhere: every marker is square to a true circle on all three
platforms, so a depth in pixels is already perpendicular to the boundary. The old dial
that preceded the digital face did need one — that was a rectangle's angled ray.

**Bands** use the same representation as the strip's: `uint8_t coverage[361]`, one byte
per minute of the window. Per *degree* would be the tempting symmetry and is the wrong
way round — two minutes share a degree, and `gabbro` would lose a pixel of placement.

**Every angle is a base plus a span, never a fresh absolute.** The window is half a turn
and rides with the hour hand, so it straddles twelve o'clock for six hours out of every
twelve. Reducing both ends mod a turn independently puts the end behind the start, and
`graphics_fill_radial` draws nothing when the start is the larger — a third of the day
would render blank. `fill_ring_band()` normalises the start, carries the span, and splits
at the boundary.

**The hands** are tapered quads with a background halo *and a stroked spine*, told apart
by width because `flint` has nothing else. The spine is not decoration: a quad three
pixels across does not survive `gpath_draw_filled`, which rasterises by scanline and
drops whole scanlines from a slanted sliver — measured on `flint`, the minute hand did
not draw at all for sixteen minutes of every hour, at slopes rather than directions. A
stroked line is a different rasteriser and is continuous at every angle. The minute hand runs to the rim; the hour hand stops three
pixels short of the ring, so the marker lane stays the markers' alone.
The halo is a grown filled polygon, never a wide stroked outline — a miter at a sharp
vertex overshoots far enough to cut a background-coloured slot through a band.

**The disc** is drawn last of the dial, over both hands, and its size is solved rather
than chosen: each row is a rectangle inscribed in a circle, so the binding corner is the
far one and the radius is the largest of those corners' distances plus a pad. That comes
out at 34 px on `flint`, 44 on `emery` and 65 on `gabbro` from the same four lines, and
it moves on its own when a font size does — which is how dropping the countdown's
progress bar shrank it, and how moving from subsetted TTFs to the firmware's own Gothic
moved it again. Gothic is wider per row and shorter per row than the condensed face it
replaced, and on `emery` those cancel exactly.

### `pipe/` — the Android companion

Kotlin + Jetpack Compose, `namespace link.dendritik.proto.pipe`.

| Piece | Role |
| --- | --- |
| `PipeEngine` | Everything with a lifecycle, and no opinion about what keeps the process alive. |
| `PipeCompanionService` | The host. Bound by the system while the associated watch is nearby. |
| `PipeService` | The fallback host: a foreground service, the notification that costs, and Android 15's six-hour cap on it. |
| `PipeHost` | `chooseHost`, and the `CompanionDeviceManager` calls around it. |
| `CalendarSource` | Queries `CalendarContract.Instances` over the window the watch can draw. |
| `CalendarWatcher` | `ContentObserver` plus `ACTION_PROVIDER_CHANGED`. |
| `PhoneBattery` | `ACTION_BATTERY_CHANGED`, filtered to whole-percent changes. |
| `EventBlob` / `EventDiff` | Pure. Packing and diffing, covered by JVM unit tests. |
| `PebbleSender` | Debounce, coalesce, dedup, chunk, and the heartbeat the tick owes. |
| `BootReceiver` | Restarts the service after a reboot. |
| `MainActivity` | The three grants the hosts cannot get themselves, the pairing dialog, the diagnostics and the manual re-sync. |
| `PipeStatus` | Observable diagnostics. Written by everything, read only by the screen. |

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

**The fallback is now the worse host for a second reason.** Since Android 15 a `dataSync`
foreground service may run six hours in any 24, after which the platform calls
`Service.onTimeout` and kills the process with a `RemoteServiceException` if it does not
stand down; it then refuses to start another until the user next opens the app, and will
not launch one from `BOOT_COMPLETED` at all. `PipeService` handles all three, and the
recovery is the one the platform documents — foregrounding the app resets the budget, and
`maybeStart` already runs on every resume. Nothing retries in the background, because the
budget is spent and a self-restarting service would only be refused again.

Note who this can reach: API 35 is well above `MIN_COMPANION_SDK`, so every device that
can hit the cap could have been on the companion host instead. The affected population is
exactly the users who declined the pairing dialog, which is why the pairing card is where
the app says so.

## Data flow

1. Something happens: the calendar changed, the watch reconnected, ten minutes passed
   and the six-hour window slid forward, or the user pressed **Re-sync now** on the
   diagnostics screen. All four land on one method, `PipeEngine.reconcile`.

   The fourth is the only one that is not a change, and the only one that comes from
   outside the engine. Nothing holds a reference to a live `PipeEngine` — both hosts keep
   it in a private field — so the button broadcasts an action the engine registers at
   runtime, alongside the tick's. Runtime registration is load-bearing in both
   directions: the system cannot restart a dead process to service the press, so a press
   with nothing running is dropped rather than answered by an engine that has just come
   up knowing nothing. It sends the same flush the tick sends and deliberately leaves the
   alarm alone, because re-arming on a press would slide the declared cadence a full
   period.
2. `CalendarSource` scans `[now − 2 h, now + 6 h]`, excluding whole-day entries — they
   have no position on a timeline and no duration that would fit one — and anything
   cancelled. Duration comes from `END - BEGIN`; a zero-length instance is a reminder.

   The phone's window is deliberately wider than any face's — `[now − 1 h, now + 3 h]`
   on the digital one's rectangles, and `[now − 1 h, now + 5 h]` on its round display
   and on the analog face. Entries past a face's horizon sit in its table undrawn and
   come into view as the window slides, which costs one message instead of one per
   quarter hour, and one scan serves all of them.
3. `EventDiff` compares the scan against what the companion believes the watch holds.
   A reconnect skips the diff and sends a **flush** instead, because a watchface that
   relaunched holds nothing.
4. `EventBlob` packs the records, twenty-four per message, and `PebbleSender` sends
   them with `FLUSH`/`MORE` framing and a `Heartbeat` — to every UUID in
   `Protocol.APP_UUIDS`, because there is no inbound channel to learn which face is on
   screen. A send to a UUID nothing is running is NACKed and dropped.
5. `wire.c` decodes, upserts and removes against a fixed 32-slot table, and marks the
   layer dirty.
6. The next paint recomputes the coverage array from scratch and draws it. Every
   marker's position, prominence and existence is a function of `now`, so the minute
   tick is also what advances the countdown and retires whatever has aged out — and it
   is the whole of the motion on both faces, the digital one sliding its ruler past a
   pinned pointer and the analog one sweeping its window past markers that hold still.

Steps 1–6 are the change path, and how fast a change arrives is governed by the
`ContentObserver`: the scan and the send happen synchronously off it, so a calendar edit
reaches the watch in about as long as a `CalendarContract.Instances` query takes.

Underneath that, one periodic tick does the two jobs that need a clock, and it is a
single `setAndAllowWhileIdle` alarm because Doze throttles that call per app rather than
per alarm — a second one at the same period would only make the first late. Its 600 s is
the platform's number rather than a preference: Doze allows a while-idle alarm no more
than once per nine minutes per app, so six an hour is the budget with one to spare. So
the tick re-scans the slid window and sends a **flush**, not a delta — the one place this
companion speaks on a timer with nothing new to say. Switching watchface does not drop
the Bluetooth link, so nothing signals it, and every delta sent while a face was off
screen went to a UUID that NACKed it — the face restores its own table at launch, but
only as of the last time it was running; with two faces installed the swap is the
ordinary thing to do, and the re-flush is what makes it self-healing. A bare heartbeat is
what is left for when the send fails outright, and a tick whose flush did not land doubles
the next one's delay — 600, 1200, 2400, capped at 3600 — until something gets through.
Proving liveness needs no separate loop, because **any message arriving is the proof**;
the `Heartbeat` key exists so a companion with no news can still say something. If the
watch hears nothing for three declared periods, the bottom row says so — three because
that is one missed tick plus the backed-off retry behind it, which is the point at which
the silence stops being a throttled scheduler and starts being a dead companion.

**Calendar content never leaves the phone.** Titles, locations, attendees and
descriptions are never read. The watch receives a position, a duration and two enum
bytes per entry.

## Build boundaries

Each watchface builds with the Pebble SDK (`waf` via the `pebble` CLI), from its own
directory and with its own UUID; output goes to `watchface-*/build/`, which is generated
and gitignored. `pipe/` builds with Gradle (`./gradlew assembleDebug`, JDK 21). Each
component is self-contained; the only thing all three share is the protocol, and the
only thing the two faces share beyond it is three presentation-free modules, by copy.
