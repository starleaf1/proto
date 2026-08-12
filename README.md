# proto

A Pebble watchface and its phone-side companion, developed together in one
repository.

The watchface reads the left edge of the display as a **four-hour timeline**, running
downward: one hour behind, three ahead, notched every fifteen minutes, with a pointer at
the quarter mark that never moves. The ruler slides past it as the clock advances.
Appointments are bands spanning their duration; tasks and reminders are wedges poking
inward off the ruler. Beside it, level with the pointer, a digital clock — then the date,
a countdown, the next turn, and whatever is running out, stacked downward. An idle face
is the strip, the clock, and the date: nothing appears unless it has something to say.

Pebble has no on-watch access to a phone's calendar, so the entries are pushed over
Bluetooth by a native **Android** companion. This repo houses both sides.

## Repository layout

| Path | What it is |
| --- | --- |
| `watchface/` | Pebble watchapp (C, Pebble SDK). See `watchface/CLAUDE.md`. |
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
| App UUID | `f2fc68a6-9636-4694-929b-73c11c33f0e4` |
| Message keys | `Heartbeat` (`10000`), `CalEvents` (`10001`), `CalFlags` (`10002`), `NavManeuver` (`10003`), `NavDistance` (`10004`), `NavUnit` (`10005`), `PhoneBattery` (`10006`) |
| Direction | phone → watch, always |

Calendar entries arrive as a packed byte array: an id, an absolute start time, a
duration and two enum bytes each. **No titles, locations or attendees** — the watch
draws a position and a duration, and nothing else about an entry leaves the phone.

Read `docs/protocol.md` before changing any of it. Both sides move in the same commit.

## Quick start — watchface

Requires the [Pebble SDK](https://developer.repebble.com). Run all commands from
`watchface/`.

```sh
cd watchface
pebble build                          # all three target platforms
pebble install --emulator flint       # or emery, or gabbro
pebble install --phone <ip>           # install to a paired phone
```

To put something on the strip without the companion, send the demo calendar — a
synthetic set of entries covering every marker case:

```sh
watchface/tools/send-demo-events.py --emulator flint
```

See `watchface/CLAUDE.md` for the full command reference — emulator control,
screenshots, headless/VNC usage, and the gotchas specific to this environment.

## Quick start — Android companion

Gradle project `ProtoPipe`, namespace `link.dendritik.proto.pipe`.

```sh
cd pipe
./gradlew assembleDebug            # requires JDK 21 and the Android SDK
./gradlew installDebug
./gradlew test                     # the wire format, on the bare JVM
```

On first launch, grant **calendar access** and **notifications**. The second is for
the companion's own ongoing notification: it runs as a foreground service, because
nothing else keeps the process alive long enough to watch a calendar.

## Documentation

- `docs/architecture.md` — how the pieces fit and how data flows
- `docs/protocol.md` — the complete wire contract
- `CONTRIBUTING.md` — dev setup and conventions
- Pebble SDK reference — <https://developer.repebble.com>
