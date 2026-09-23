# proto

Two Pebble watchfaces and the phone-side companion that feeds them both, developed
together in one repository. Same calendar, two readings of it.

**`watchface-digital`** reads the left edge of the display as a timeline running
downward, notched every fifteen minutes, with a pointer that never moves — the ruler
slides past it as the clock advances. On the rectangles it is a **four-hour** window,
one hour behind and three ahead, on a straight vertical strip with the pointer at the
quarter mark. On the round display the strip is just as straight but runs the full
height of the display, so the glass cuts off both ends and the ruler reads as carrying
on past the edge (1.5 h behind and 3.5 h ahead on the track, about 1 and 3 visible). It
sits left of centre, only as far left as the clock needs to fit beside the pointer. Appointments are bands spanning
their duration; tasks and reminders are wedges poking inward off the ruler. Beside it a
digital clock, level with the pointer, and under that the date, a countdown, the next
turn and whatever is running out, left-aligned against the strip.

**`watchface-analog`** is a circular analog clock whose rim is a **six-hour timeline** —
one hour behind, five ahead — with each entry at the clock angle of its own time, so a
three o'clock meeting sits at the 3. Six hours is half a turn of a twelve-hour dial, so
the ring advances at thirty degrees to the hour: the hour hand's own rate, which makes
the hand the now mark — it points at an appointment's band while the appointment is
running, stopping short of the ring so the markers keep their lane to
themselves. The date and the countdown sit in a disc at the centre, drawn
over the hands so they stay readable at every minute.

An idle face is the timeline, the clock and the date: nothing appears unless it has
something to say.

Pebble has no on-watch access to a phone's calendar, so the entries are pushed over
Bluetooth by a native **Android** companion. This repo houses all three sides.

## Repository layout

| Path | What it is |
| --- | --- |
| `watchface-digital/` | Pebble watchapp, the left-edge timeline (C, Pebble SDK). See `watchface-digital/CLAUDE.md`. |
| `watchface-analog/` | Pebble watchapp, the analog dial (C, Pebble SDK). See `watchface-analog/CLAUDE.md`. |
| `pipe/` | Android companion (Kotlin, calendar reader). See `pipe/CLAUDE.md`. |
| `docs/` | Architecture and the watch↔phone protocol contract. |

## Target hardware

The 2026 devices only:

| Platform | Device | Display |
| --- | --- | --- |
| `flint` | Pebble 2 Duo | 144×168, **black and white** |
| `emery` | Pebble Time 2 | 200×228, colour |
| `gabbro` | Pebble Round 2 | 260×260, colour, round |

One of the three has a single ink, which is why the phone sends *meaning* and never
colour: the companion cannot know which watch is on the other end. Every distinction on
the face — running versus upcoming, overdue versus due, one entry versus several — is
carried by shape or depth first, with colour layered on top only where there is any.

## The integration surface

| | |
| --- | --- |
| App UUIDs | `f2fc68a6-9636-4694-929b-73c11c33f0e4` (digital), `bd9bd299-527e-4236-8206-27be3dc9781c` (analog) |
| Message keys | `Heartbeat` (`10000`), `CalEvents` (`10001`), `CalFlags` (`10002`), `NavManeuver` (`10003`), `NavDistance` (`10004`), `NavUnit` (`10005`), `PhoneBattery` (`10006`) |
| Direction | phone → watch, always |

Calendar entries arrive as a packed byte array: an id, an absolute start time, a
duration and two enum bytes each. **No titles, locations or attendees** — the watch
draws a position and a duration, and nothing else about an entry leaves the phone.

Two faces cannot share a UUID — the firmware keys installed apps by it — and the
protocol has no inbound channel, so the companion cannot tell which face is on screen.
It addresses both unconditionally; a message sent to a UUID nothing is running is NACKed
and dropped.

Read `docs/protocol.md` before changing any of it. Every side moves in the same commit.

## Quick start — watchfaces

Requires the [Pebble SDK](https://developer.repebble.com). Each face is a separate app
with its own UUID; **run every command from that face's own directory.**

```sh
cd watchface-analog                   # or watchface-digital
pebble build                          # all three target platforms
pebble install --emulator flint       # or emery, or gabbro
pebble install --phone <ip>           # install to a paired phone
```

To put something on the timeline without the companion, send the demo calendar — a
synthetic set of entries covering every marker case. The script reads the UUID out of
the `package.json` beside it, so it always targets the face it ships with:

```sh
watchface-analog/tools/send-demo-events.py --emulator flint
```

See each face's `CLAUDE.md` for the full command reference — emulator control,
screenshots, headless/VNC usage, and the gotchas specific to this environment.

## Quick start — Android companion

Gradle project `ProtoPipe`, namespace `link.dendritik.proto.pipe`.

```sh
cd pipe
./gradlew assembleDebug            # requires JDK 21 and the Android SDK
./gradlew installDebug
./gradlew test                     # the wire format and both UUIDs, on the bare JVM
```

On first launch, grant **calendar access** and **notifications**. The second is for
the companion's own ongoing notification: it runs as a foreground service, because
nothing else keeps the process alive long enough to watch a calendar.

## Documentation

- `watchface-digital/CLAUDE.md`, `watchface-analog/CLAUDE.md` — each face in detail
- `docs/architecture.md` — how the pieces fit and how data flows
- `docs/protocol.md` — the complete wire contract
- `CONTRIBUTING.md` — dev setup and conventions
- Pebble SDK reference — <https://developer.repebble.com>
