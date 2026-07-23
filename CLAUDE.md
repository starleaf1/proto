## Project Overview

**proto** is a monorepo for a Pebble watchface and its phone-side companion.
The watchface shows the time, date, and a set of status icons — one of which is
an unread-message envelope driven by the companion over Bluetooth.

## Repository Layout

```
proto/
  watchface/     Pebble watchapp (C, Pebble SDK) — see watchface/CLAUDE.md
  android/       Android companion app (planned) — sends UnreadCount to the watch
  docs/          Architecture and the watch↔phone protocol contract
  README.md      Start here
```

Each component owns its build tooling and its own `CLAUDE.md`. When working
inside a component, follow that component's `CLAUDE.md` — Claude Code loads the
nested file automatically.

- **Pebble watchface:** all `pebble` commands run from `watchface/`. See
  [watchface/CLAUDE.md](watchface/CLAUDE.md).
- **Android companion:** lives under `android/` (scaffolding may not exist yet).

## The one contract that ties the components together

The watch and the companion communicate through a single Pebble AppMessage key,
`UnreadCount` (int32), addressed to app UUID `f2fc68a6-9636-4694-929b-73c11c33f0e4`.
This is the entire integration surface. Before changing message keys, the UUID,
or AppMessage buffer sizes on either side, read [docs/protocol.md](docs/protocol.md)
— both components must move together.

## Documentation

- [README.md](README.md) — overview and quick start for each component
- [docs/architecture.md](docs/architecture.md) — how the pieces fit and data flows
- [docs/protocol.md](docs/protocol.md) — the AppMessage contract the companion implements
- [CONTRIBUTING.md](CONTRIBUTING.md) — dev setup and conventions
