## Project Overview

**proto** is a monorepo for a Pebble watchface and its phone-side companion. The
watchface reads the left edge of the display as a four-hour timeline running downward —
one hour behind, three ahead, and a "now" mark at the quarter mark that never moves.
Appointments are bands spanning their duration, tasks and reminders are wedges poking
inward off the ruler, and the hour is numbered in a small lane just inside it. A digital
clock sits level with the now mark, and the date, a countdown, the next turn and whatever
is running out stack beneath it, left-aligned against the strip. The calendar is pushed
to the watch by an Android companion over Bluetooth.

## Repository Layout

```
proto/
  watchface/     Pebble watchapp (C, Pebble SDK) — see watchface/CLAUDE.md
  pipe/          Android companion — reads the calendar, sends it to the watch
  docs/          Architecture and the watch↔phone protocol contract
  README.md      Start here
```

Each component owns its build tooling and its own `CLAUDE.md`. When working inside a
component, follow that component's `CLAUDE.md` — Claude Code loads the nested file
automatically.

- **Pebble watchface:** all `pebble` commands run from `watchface/`. See
  [watchface/CLAUDE.md](watchface/CLAUDE.md).
- **Android companion:** lives under `pipe/`; all `gradlew` commands run from there.
  See [pipe/CLAUDE.md](pipe/CLAUDE.md).

## Target platforms

The 2026 devices only: **`flint`** (Pebble 2 Duo, 144×168, black and white),
**`emery`** (Pebble Time 2, 200×228, colour) and **`gabbro`** (Pebble Round 2, 260×260,
colour, round).

That `flint` has a single ink is a design constraint, not a footnote. Every distinction
on the face is carried by shape, depth or position first — a running band fills the notch
zone while an upcoming one fills half of it, and an overdue marker is simply *above* the
now mark — and colour is layered on top only where there is any. **Verify visual changes
on `flint` as well as a colour platform**; something that reads well on `gabbro` can be
invisible on `flint`.

The now mark is the one place this goes the other way, and the only element on the face
whose *shape* differs by display: where there is colour it is a red rule struck across
the strip, and on `flint` it is a wedge beside it. A rule needs a hue nothing else on the
strip uses, and a black one would be an extra notch on a ruler made of notches.

## The one contract that ties the components together

Seven Pebble AppMessage keys — `Heartbeat` (`10000`), `CalEvents` (`10001`), `CalFlags`
(`10002`), `NavManeuver` (`10003`), `NavDistance` (`10004`), `NavUnit` (`10005`) and
`PhoneBattery` (`10006`) — addressed to app UUID
`f2fc68a6-9636-4694-929b-73c11c33f0e4`. This is the entire integration surface, and it
runs one way: phone → watch.

Calendar entries travel as a packed byte array — an id, an absolute UTC start, a
duration and two enum bytes each. **No titles, locations or attendees ever cross the
wire.** The phone sends *meaning*; the watch owns rendering, because three displays of
two different shapes and two different colour depths cannot share a pixel decision made
on the phone.

Before changing message keys, the UUID, the blob layout, or AppMessage buffer sizes,
read [docs/protocol.md](docs/protocol.md) — both components must move together, in one
commit.

## Three things that look like bugs and are not

- **The strip is straight on `flint` and `emery` and curved on `gabbro`,** following the
  left arc there rather than a chord inset from it. One renderer covers both: every point
  on the track carries a position *and* a ray angle, which is a constant 270° on a
  rectangle and sweeps on the arc. What may never vary is how *thick* a marker looks, and
  here it cannot — depths are pixel counts perpendicular to the boundary, and both shapes
  are square to their own boundary, so no correction is needed. The dial this replaced
  did need one, and its absence is the shape paying for itself rather than a regression.
- **Nothing animates, yet the strip scrolls.** There is one `MINUTE_UNIT` tick and no
  animation anywhere. Every position on the track is a function of `t - now`, so
  recomputing the face once a minute slides the whole ruler — hour numbers and all — past
  a now mark that is pinned to a quarter of the way down.
- **The watch keeps drawing calendar markers when the companion is unreachable.** An
  entry is timestamped and ages out on its own, unlike the notification counts this face
  used to carry, and the bottom row is already saying the companion is gone. Uncertainty
  is stated, not silently omitted.

## Documentation

- [README.md](README.md) — overview and quick start for each component
- [docs/architecture.md](docs/architecture.md) — how the pieces fit and data flows
- [docs/protocol.md](docs/protocol.md) — the complete wire contract
- [CONTRIBUTING.md](CONTRIBUTING.md) — dev setup and conventions
