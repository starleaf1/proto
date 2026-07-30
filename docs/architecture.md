# Architecture

`proto` is two programs that cooperate over Bluetooth:

```
┌────────────────────────────┐            AppMessage             ┌──────────────────────────┐
│  Phone companion           │  Heartbeat, CalEvents, CalFlags,  │  Pebble watchface        │
│  (pipe/ — Android)         │  NavManeuver/Distance/Unit,       │  (watchface/)            │
│  reads the calendar and    │  PhoneBattery                     │  draws a six-hour        │
│  the phone's battery       │  (phone → watch)                  │  timeline on the dial,   │
│                            │  ───────────────────────────────► │  plus two single-line    │
│                            │   on change, plus a liveness beat │  slots inside it         │
└────────────────────────────┘                                   └──────────────────────────┘
```

The dividing line: **the phone decides meaning, the watch decides pixels.** The
companion resolves which entries exist, when they start, how long they run, and
whether one is an appointment or a reminder. The watch turns that into arcs,
triangles and colours. No colour crosses the wire, because the companion cannot know
which watch model is on the other end — and one of the three is black-and-white.

## What the face answers

Not "how many things are waiting for me" — that was the previous design, and it needed
a notification listener to count things. This one answers **"what are my next few
hours, and is anything wrong right now"**.

- **The dial is a twelve-hour clock face used as a timeline.** Appointments in the
  next six hours are arcs spanning their duration, at their real clock position.
  Tasks and reminders are triangles at the notch they fall nearest.
- **The top slot alerts**, one thing at a time, highest priority only.
- **The bottom slot counts down** to whatever is next, or up through whatever is
  running.
- **Nothing else is drawn.** An idle face is a dial, the minute, and the date.

## Certainty, restated

The previous design expressed a loss of certainty by *hiding* its phone-fed icons: it
had counts, a count cannot be checked, and a stale one is a silent lie.

Calendar entries are not counts. An entry is timestamped, so it ages out on its own,
and a marker that is six hours old has already left the window. That changes the right
answer: **uncertainty becomes something the face states rather than something it
silently omits.** "Companion disconnected" is the top slot's first priority, and the
calendar keeps drawing underneath it.

The watch still owns three things outright — the time, the date and its own battery —
and those are never gated on anything.

## Components

### `watchface/` — the Pebble watchapp

One window, one layer, one update proc, split across seven small modules.

| File | Owns |
| --- | --- |
| `proto.c` | Lifecycle, the service handlers, and the paint order. |
| `geometry.{c,h}` | The dial's trigonometry and the vertical layout. |
| `theme.h` | The whole palette and the three font choices. |
| `events.{c,h}` | The event table, the live window, the linger rules, and which entry the bottom slot should show. |
| `dial.{c,h}` | Bands, notches, markers, the hour index. |
| `slots.{c,h}` | Both slots, and every glyph. |
| `wire.{c,h}` | The AppMessage inbox and the two watchdogs. |
| `wbatt.{c,h}` | The watch's own hours-remaining estimate. |

**The dial** is one representation doing all the work: `uint8_t coverage[360]`, one
byte per degree, holding the most prominent thing happening at that degree.
Overlapping appointments flatten because they write the same array, and `max()` makes
a merged band inherit the more urgent member's weight. Round and rectangular displays
share one code path.

Prominence never depends on colour, because `flint` has none. A running appointment is
drawn to about two thirds of the notch zone's depth, an upcoming one to a third — both
stopping short of the notch inner-ends, so the ring reads as a dial carrying a marker
rather than as a coloured arc with ticks on it. A grouped marker is a deeper spike. Colour, where there is any, is layered on top of distinctions that
already work without it — and where it is doing the work, the shape it stands in for
is freed up. Point markers are solid wedges on the colour platforms, amber against
orange saying upcoming against overdue; on `flint` the upcoming one is hollow, because
there the two are the same ink and fill is all that is left to separate them.

The notch ring's place in that stack is the one thing that does differ by display.
On `emery` and `gabbro` it is drawn last, ink over everything, so an unbroken grid
runs across the bands and the markers both. `flint` has no hue to carry the band, so
a running one fills the zone in the same ink as the notches; there they invert to
background where a running band crosses them, and they stay underneath the markers so
that cut never splits a marker in half.

**The hour index** is why any of it is readable: absolute clock positions need a "now"
to be measured against. It is drawn last, after everything, because both slots are
centred — which puts them at twelve and six o'clock — and their background knockout
would otherwise erase the one element everything else is relative to.

**The dial hugs the screen** — a circle on `gabbro`, the rectangular perimeter on
`flint` and `emery`. That means equal spans of time cover unequal arc lengths on the
two rectangular platforms, because a corner is 1.5× further from the centre than an
edge midpoint. The distortion is accepted; the angle, which is what actually encodes
the time, is exact everywhere.

What is *not* allowed to vary is how thick a marker looks. Every depth is a count of
pixels measured perpendicular to the boundary — never a fraction of the distance to it,
and on a rectangle not a fixed distance along the ray either, since a ray leaving near a
corner meets the edge at up to 49° and a constant radial depth would present only two
thirds of itself across that edge. `depth_along_ray` divides the depth by the cosine of
that angle, so the ray distance stretches toward a corner and the band reads as a ribbon
of one thickness all the way round.

### `pipe/` — the Android companion

Kotlin + Jetpack Compose, `namespace link.dendritik.proto.pipe`.

| Piece | Role |
| --- | --- |
| `PipeService` | Foreground service. The process keeper, and the owner of everything with a lifecycle. |
| `CalendarSource` | Queries `CalendarContract.Instances` over the window the watch can draw. |
| `CalendarWatcher` | `ContentObserver` plus `ACTION_PROVIDER_CHANGED`. |
| `PhoneBattery` | `ACTION_BATTERY_CHANGED`, filtered to whole-percent changes. |
| `EventBlob` / `EventDiff` | Pure. Packing and diffing, covered by JVM unit tests. |
| `PebbleSender` | Debounce, coalesce, dedup, chunk, heartbeat. |
| `BootReceiver` | Restarts the service after a reboot. |

Framework types stop at `CalendarSource`. Everything below it sees `EventFacts`, which
is what lets the whole wire format be tested with no device and no Robolectric — the
same property the previous design's `Classifier`/`ActiveSet` had, kept deliberately.

**It needs a foreground service now, and did not before.** The previous design
piggy-backed on a bound `NotificationListenerService`, which the system kept alive for
its own reasons. With the shade no longer being read there is no such host, and
calendar observation, the phone's battery and the liveness heartbeat all have to
outlive the activity. That is the one place this redesign costs the user something: a
permanent notification and four permissions that did not exist before.

## Data flow

1. Something happens: the calendar changed, the watch reconnected, or fifteen minutes
   passed and the six-hour window slid forward. All three land on one method,
   `PipeService.reconcile`.
2. `CalendarSource` scans `[now − 2 h, now + 6 h]`, excluding whole-day entries — they
   have no position on a twelve-hour dial — and anything cancelled. Duration comes
   from `END - BEGIN`; a zero-length instance is a reminder.
3. `EventDiff` compares the scan against what the companion believes the watch holds.
   A reconnect skips the diff and sends a **flush** instead, because a watchface that
   relaunched holds nothing.
4. `EventBlob` packs the records, twenty-four per message, and `PebbleSender` sends
   them with `FLUSH`/`MORE` framing and a `Heartbeat`.
5. `wire.c` decodes, upserts and removes against a fixed 32-slot table, and marks the
   layer dirty.
6. The next paint recomputes the coverage array from scratch and draws it. Every
   marker's position, prominence and existence is a function of `now`, so the minute
   tick is also what advances the countdown and retires whatever has aged out.

Steps 1–6 are the change path. Running underneath it is a much slower loop whose only
job is to prove the companion still exists; if the watch misses 2.5 declared periods
of that, the top slot says so. The two loops are independent — the heartbeat rate has
no bearing on how fast a real change arrives, which is governed by the
`ContentObserver` and a 250 ms debounce.

**Calendar content never leaves the phone.** Titles, locations, attendees and
descriptions are never read. The watch receives a position, a duration and two enum
bytes per entry.

## Build boundaries

`watchface/` builds with the Pebble SDK (`waf` via the `pebble` CLI); output goes to
`watchface/build/`, which is generated and gitignored. `pipe/` builds with Gradle
(`./gradlew assembleDebug`, JDK 21). Each component is self-contained; the only thing
they share is the protocol.
