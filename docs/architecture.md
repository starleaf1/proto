# Architecture

`proto` is two programs that cooperate over Bluetooth:

```
┌──────────────────────────┐           AppMessage           ┌─────────────────────────┐
│  Phone companion         │  UnreadCount, MissedCount      │  Pebble watchface       │
│  (pipe/ — scaffolded;    │  (int32, phone → watch)        │  (watchface/)           │
│   pkjs stub today)       │  ──────────────────────────►   │  draws the envelope     │
│                          │         sent on change         │  and missed-call icons  │
└──────────────────────────┘                                └─────────────────────────┘
```

## Components

### `watchface/` — the Pebble watchapp

Single-window C app. A root layer's update proc paints everything each time the
watch state changes:

- **Time** — hours in Orbitron 54, rendered from a minute tick handler.
- **Date** — Rajdhani Light 22.
- **Status icons** — an **unread-message envelope** and a **missed-call
  handset**, drawn as a centered pair. The connection service gates the pair:
  while the phone link is down neither icon is drawn, since both counts are
  phone-fed and the watch has no current value for either.
- **Battery gauge** — from the battery state service, just above the numeral.
  The one indicator the watch computes itself, so the one that is always shown.

The envelope and the missed-call handset are the only elements the watch cannot
compute on its own. Pebble provides no on-watch API for the phone's notification
count or call history, so the watchface holds an `s_unread` counter and an
`s_missed` counter — each starts at `0` (icon unlit) and is updated only when an
AppMessage arrives carrying `UnreadCount` / `MissedCount`.

Entry point and lifecycle live in [`watchface/src/c/proto.c`](../watchface/src/c/proto.c);
the AppMessage inbox handler is `inbox_received()`.

### `pipe/` — the companion (scaffolded)

The companion's job is to compute the unread-message and missed-call counts on
the phone and push them to the watch as `UnreadCount` and `MissedCount`. Today
this role is filled by a PebbleKit JS stub
([`watchface/src/pkjs/index.js`](../watchface/src/pkjs/index.js)) that only logs
`ready` and never sends a value — so both icons stay unlit until the Android app
takes over.

`pipe/` is an Android Studio project (Kotlin, Jetpack Compose) that has been
scaffolded but not yet wired to the watch: it carries no PebbleKit dependency,
no notification listener, and no key constants. When implemented it will use
PebbleKit Android to open a channel to the watch UUID and send the counts
whenever they change.

## Data flow

1. The phone determines the current unread-message and missed-call counts
   (source TBD — notification listener, call-log observer, account API, etc.).
2. On any change, the companion sends `{ UnreadCount: <n>, MissedCount: <m> }`
   to the watch (either key may be sent on its own).
3. The watchface's `inbox_received()` stores whichever values arrived and marks
   the root layer dirty.
4. The next paint lights each icon when its count is `> 0`, leaves it unlit at
   `0` — and draws neither while the phone connection is down.

The exact identifiers, buffer sizes, and delivery semantics are specified in
[protocol.md](protocol.md). Because both sides share that one contract, a change
to the key, the UUID, or the message shape must land in `watchface/` and
`pipe/` together.

## Build & tooling boundaries

- `watchface/` builds with the Pebble SDK (`waf` via the `pebble` CLI). Output
  goes to `watchface/build/`, which is generated and gitignored.
- `pipe/` builds with Gradle (`./gradlew assembleDebug`, JDK 21). Its artifacts
  are gitignored both in-module and at the repo root.

Each component is self-contained; there is no shared build step. The only thing
they share is the protocol.
