## Project Overview

**proto** is a monorepo for a Pebble watchface and its phone-side companion.
The watchface shows the time, date, and a set of status icons — one of which is
an unread-message envelope driven by the companion over Bluetooth.

## Repository Layout

```
proto/
  watchface/     Pebble watchapp (C, Pebble SDK) — see watchface/CLAUDE.md
  pipe/          Android companion app — sends UnreadCount/MissedCount to the watch
  docs/          Architecture and the watch↔phone protocol contract
  README.md      Start here
```

Each component owns its build tooling and its own `CLAUDE.md`. When working
inside a component, follow that component's `CLAUDE.md` — Claude Code loads the
nested file automatically.

- **Pebble watchface:** all `pebble` commands run from `watchface/`. See
  [watchface/CLAUDE.md](watchface/CLAUDE.md).
- **Android companion:** lives under `pipe/`; all `gradlew` commands run from
  there. It reads the notification shade and pushes the resulting state to the watch.

## The one contract that ties the components together

The watch and the companion communicate through four Pebble AppMessage keys —
`UnreadCount` (int32, id `10000`), `MissedCount` (int32, id `10001`),
`PhoneState` (int32, id `10002`) and `Heartbeat` (int32, id `10003`) — addressed to
app UUID `f2fc68a6-9636-4694-929b-73c11c33f0e4`. This is the entire integration
surface. The phone sends *meaning* (a state enum), never colour or blink frames; the
watch owns rendering, because three of the seven target platforms are black-and-white.
Before changing message keys, the UUID, or AppMessage buffer sizes on either side,
read [docs/protocol.md](docs/protocol.md) — both components must move together.

The watch hides both status icons unless it is sure of them, and there are two
independent ways to lose that certainty: Bluetooth drops (the watch detects this
itself) or the companion stops checking in (`Heartbeat`). Only the battery gauge
survives both, because it is the one thing the watch computes for itself.

## Documentation

- [README.md](README.md) — overview and quick start for each component
- [docs/architecture.md](docs/architecture.md) — how the pieces fit and data flows
- [docs/protocol.md](docs/protocol.md) — the AppMessage contract the companion implements
- [CONTRIBUTING.md](CONTRIBUTING.md) — dev setup and conventions
