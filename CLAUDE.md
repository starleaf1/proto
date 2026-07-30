## Project Overview

**proto** is a monorepo for a Pebble watchface and its phone-side companion. The
watchface uses a twelve-hour dial as a six-hour timeline: appointments are arcs
spanning their duration at their real clock position, tasks and reminders are
triangles at the nearest notch. Two single-line slots inside the ring alert and count
down. The calendar is pushed to the watch by an Android companion over Bluetooth.

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
on the face is carried by shape or depth first — a running band fills the notch zone
while an upcoming one fills half of it, an overdue marker is solid where an upcoming one
is hollow — and colour is layered on top only where there is any. **Verify visual
changes on `flint` as well as a colour platform**; something that reads well on
`gabbro` can be invisible on `flint`.

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

## Two things that look like bugs and are not

- **The dial traces the rectangular perimeter on `flint` and `emery`,** so equal spans
  of time cover unequal arc lengths there. Accepted deliberately: the angle encodes the
  time and the angle is exact. What may never vary is how *thick* a marker looks —
  depths are pixel counts measured perpendicular to the boundary, never fractions of the
  distance to it. On a rectangle that means the distance travelled along the ray is
  divided by the cosine of its angle to the edge normal, so it stretches toward a
  corner; the radial extent varies precisely so that the visible thickness does not.
- **The watch keeps drawing calendar markers when the companion is unreachable.** An
  entry is timestamped and ages out on its own, unlike the notification counts this face
  used to carry, and the top slot's first priority is already saying the companion is
  gone. Uncertainty is stated, not silently omitted.

## Documentation

- [README.md](README.md) — overview and quick start for each component
- [docs/architecture.md](docs/architecture.md) — how the pieces fit and data flows
- [docs/protocol.md](docs/protocol.md) — the complete wire contract
- [CONTRIBUTING.md](CONTRIBUTING.md) — dev setup and conventions
