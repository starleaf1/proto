## Project Overview

**proto** is a monorepo for two Pebble watchfaces and the phone-side companion that
feeds them both. The calendar is pushed to the watch by an Android companion over
Bluetooth; the faces read the same hours ahead two different ways.

**`watchface-digital/`** reads the left edge of the display as a timeline running
downward past a "now" mark that never moves: four hours on the rectangles, one hour
behind and three ahead, the mark at the quarter mark. On `gabbro` the strip runs the
full height of the display, so the glass cuts off both ends and the ruler reads as longer
than what is shown (1.5 h behind and 3.5 h ahead on the track), and it sits left of
centre, only as far left as the clock needs to fit beside the mark. Appointments are bands spanning their duration, tasks and reminders are wedges
poking inward off the ruler, and the hour is numbered in a small lane just inside it. A
digital clock sits level with the now mark, and the date, a countdown, the next turn and
whatever is running out stack beneath it, left-aligned against the strip.

**`watchface-analog/`** is a circular analog clock whose rim is a six-hour timeline —
one hour behind, five ahead — laid on the dial at the clock angle of each entry's own
time, so a three o'clock meeting is at the 3. Six hours is half a turn of a twelve-hour
dial, which puts the ring at thirty degrees to the hour: the hour hand's own rate, so
the hand *is* the now mark and there is no second one. The date and the countdown sit
in a disc at the centre, drawn over the hands.

The two share a vocabulary and nothing else — not a build, not a header. An appointment
is a band, a task is a blunt wedge, a running thing is twice the depth of an upcoming
one, and red is spent on now.

## Repository Layout

```
proto/
  watchface-digital/  Pebble watchapp, the left-edge timeline (C, Pebble SDK)
  watchface-analog/   Pebble watchapp, the analog dial (C, Pebble SDK)
  pipe/               Android companion — reads the calendar, sends it to both
  docs/               Architecture and the watch↔phone protocol contract
  README.md           Start here
```

Each component owns its build tooling and its own `CLAUDE.md`. When working inside a
component, follow that component's `CLAUDE.md` — Claude Code loads the nested file
automatically.

- **Pebble watchfaces:** all `pebble` commands run from the face's own directory —
  never the repo root, and never the other face's. See
  [watchface-digital/CLAUDE.md](watchface-digital/CLAUDE.md) and
  [watchface-analog/CLAUDE.md](watchface-analog/CLAUDE.md).
- **Android companion:** lives under `pipe/`; all `gradlew` commands run from there.
  See [pipe/CLAUDE.md](pipe/CLAUDE.md).

`events.{c,h}`, `wire.{c,h}`, `wbatt.{c,h}`, `tools/grab.py` and
`tools/send-demo-events.py` exist in both faces **by copy, not by reference**. They
carry no rendering knowledge, and the copies are identical except for each face's own
visible-window constants. A fix to the decoder or the battery estimate belongs in both.
`tools/make-menu-icon.py` is the exception that is *not* a copy: it draws that face's
launcher icon and so is the one tools script that knows what its face looks like.

## Target platforms

The 2026 devices only: **`flint`** (Pebble 2 Duo, 144×168, black and white),
**`emery`** (Pebble Time 2, 200×228, colour) and **`gabbro`** (Pebble Round 2, 260×260,
colour, round).

That `flint` has a single ink is a design constraint, not a footnote. On both faces
every distinction is carried by shape, depth or position first — a running band is twice
the depth of an upcoming one, an overdue marker is simply *behind* now, and the analog
face's two hands differ in width rather than in colour — and colour is layered on top
only where there is any. **Verify visual changes
on `flint` as well as a colour platform**; something that reads well on `gabbro` can be
invisible on `flint`.

On `watchface-digital/` the now mark is the one place this goes the other way, and the
only element on that face whose *shape* differs by display: where there is colour it is
a red rule struck across the strip, and on `flint` it is a wedge beside it. A rule needs
a hue nothing else on the strip uses, and a black one would be an extra notch on a ruler
made of notches. `watchface-analog/` has no such element, because its now mark is the
hour hand and a hand is the same shape everywhere.

## The one contract that ties the components together

Seven Pebble AppMessage keys — `Heartbeat` (`10000`), `CalEvents` (`10001`), `CalFlags`
(`10002`), `NavManeuver` (`10003`), `NavDistance` (`10004`), `NavUnit` (`10005`) and
`PhoneBattery` (`10006`) — addressed to both app UUIDs,
`f2fc68a6-9636-4694-929b-73c11c33f0e4` (digital) and
`bd9bd299-527e-4236-8206-27be3dc9781c` (analog). This is the entire integration surface,
and it runs one way: phone → watch.

The firmware keys installed apps by UUID, so the two faces cannot share one, and the
protocol has no inbound channel — the companion cannot learn which face is on screen.
So it addresses both unconditionally: a message sent to a UUID nothing is running is
NACKed and dropped, which is cheaper than any way of choosing would be.

Calendar entries travel as a packed byte array — an id, an absolute UTC start, a
duration and two enum bytes each. **No titles, locations or attendees ever cross the
wire.** The phone sends *meaning*; the watch owns rendering, because three displays of
two different shapes and two different colour depths cannot share a pixel decision made
on the phone.

Before changing message keys, a UUID, the blob layout, or AppMessage buffer sizes, read
[docs/protocol.md](docs/protocol.md) — every component must move together, in one
commit. `ProtocolTest` in `pipe/` reads both `package.json`s and fails if the companion
stops addressing a face or the key order drifts.

## Things that look like bugs and are not

Shared by both faces:

- **Nothing animates, and the calendar moves anyway.** There is one `MINUTE_UNIT` tick
  and no animation in either face. Every position is a function of `t - now`, so
  recomputing once a minute is the whole of the motion — the digital face's ruler slides
  past a pinned now, and the analog face's window sweeps past markers that hold still.
  Same expression, read from opposite ends.
- **The watch keeps drawing calendar markers when the companion is unreachable, and
  across its own relaunch.** An entry is timestamped and ages out on its own, unlike the
  notification counts these faces used to carry, and the bottom row is already saying
  the companion is gone. Uncertainty is stated, not silently omitted — and an empty
  timeline is not a way of stating it, because it reads as a clear afternoon. The table
  goes to persistent storage at exit for that reason: the firmware kills a watchface
  every time the user glances at another app, and the companion's re-flush can be a
  ten minutes away.
- **The companion sends a full flush on every periodic tick, not a delta.** Switching
  watchface does not drop the Bluetooth link, so nothing signals it, and every delta
  sent while a face was off screen went to a UUID that NACKed it. The face restores its
  own table at launch, so what it comes up with is correct as of the last time it was
  running and no more; with two faces the swap is the ordinary thing to do, so the tick
  re-flushes to make it self-healing. It is the one place the companion speaks on a
  timer with nothing new to say. See [docs/protocol.md](docs/protocol.md).

`watchface-digital/` only:

- **The strip is straight on every display, and on `gabbro` it is off-centre by a
  solved amount.** It runs the full height of the display and the glass clips both ends,
  so the ruler reads as carrying on past the edge instead of stopping short of it with
  empty glass above and below. Hard against the left edge the glass would show little of
  it, and in the middle there is no room for the clock.
  `layout_compute` steps it left from the centre until the clock's row, level with the
  now mark, is wide enough; the first x that fits costs the strip least. Every point on
  the track still carries a position *and* a ray angle, a constant 270°, so depths are
  pixel counts perpendicular to the strip and need no correction. The dial this replaced
  did need one, and its absence is the shape paying for itself rather than a regression.

`watchface-analog/` only:

- **The markers do not move and there is no now mark.** Both fall out of the ring
  running at the hour hand's own rate. A marker sits at the clock angle of its own time
  and stays there; the hour hand points at the present position on the timeline by
  construction, so a separate mark would be a second one.
- **Half the ring is empty, and that is not "nothing scheduled".** The rail spans the
  visible window and stops, with a radial end cap at each end. Past the cap the ring is
  not free — it is not being shown.
- **With nothing in the window the rail does not draw either, and the face is a plain
  analog clock.** The rail is the scale the markers are read against; carrying no
  markers it would be asserting a clear six hours, and the ring looks the same when the
  companion has never spoken. The digital face's ruler stays, because it also carries
  the hour numbers and the now mark.
- **On `gabbro` a maneuver hides a low battery, and on the rectangles it does not.**
  The rectangles have a strip of screen under the dial that costs the clock nothing,
  and it holds two readings abreast. The round glass has no such strip, so the same
  pair are a row of the centre disc — the dearest place on that face to put anything,
  because the disc's radius is solved from its rows and drawn over the hands. One row
  instead of two is seven pixels of radius given back to the hour hand, and what it
  costs is that the four readings must queue: no link, then the maneuver, then the
  phone's battery, then the watch's. A turn instruction is the perishable one.
- **The hands break at the central disc and resume past it.** The disc is drawn over
  them, which inverts the digital face's rule that nothing may cover the now mark. It
  covers the half of each hand nearest the pivot, which carries no reading, and it is
  the only place on that face where text is safe from a hand at every minute of the day.

## Documentation

- [README.md](README.md) — overview and quick start for each component
- [watchface-digital/CLAUDE.md](watchface-digital/CLAUDE.md) — the left-edge timeline
- [watchface-analog/CLAUDE.md](watchface-analog/CLAUDE.md) — the analog dial
- [docs/architecture.md](docs/architecture.md) — how the pieces fit and data flows
- [docs/protocol.md](docs/protocol.md) — the complete wire contract
- [CONTRIBUTING.md](CONTRIBUTING.md) — dev setup and conventions
