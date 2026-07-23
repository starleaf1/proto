# Architecture

`proto` is two programs that cooperate over Bluetooth:

```
┌──────────────────────────┐           AppMessage           ┌─────────────────────────┐
│  Phone companion         │  UnreadCount, MissedCount      │  Pebble watchface       │
│  (android/ — planned;    │  (int32, phone → watch)        │  (watchface/)           │
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
- **Status icons** — a Bluetooth-connection icon (from the connection service),
  an **unread-message envelope**, and a **missed-call handset**. A battery gauge
  (from the battery state service) sits just above the hour.

The envelope and the missed-call handset are the only elements the watch cannot
compute on its own. Pebble provides no on-watch API for the phone's notification
count or call history, so the watchface holds an `s_unread` counter and an
`s_missed` counter — each starts at `0` (icon unlit) and is updated only when an
AppMessage arrives carrying `UnreadCount` / `MissedCount`.

Entry point and lifecycle live in [`watchface/src/c/proto.c`](../watchface/src/c/proto.c);
the AppMessage inbox handler is `inbox_received()`.

### `android/` — the companion (planned)

The companion's job is to compute the unread-message and missed-call counts on
the phone and push them to the watch as `UnreadCount` and `MissedCount`. Today
this role is filled by a PebbleKit JS stub
([`watchface/src/pkjs/index.js`](../watchface/src/pkjs/index.js)) that only logs
`ready` and never sends a value — so both icons stay unlit until the Android app
takes over.

The Android app will use PebbleKit Android to open a channel to the watch UUID
and send the counts whenever they change.

## Data flow

1. The phone determines the current unread-message and missed-call counts
   (source TBD — notification listener, call-log observer, account API, etc.).
2. On any change, the companion sends `{ UnreadCount: <n>, MissedCount: <m> }`
   to the watch (either key may be sent on its own).
3. The watchface's `inbox_received()` stores whichever values arrived and marks
   the root layer dirty.
4. The next paint lights each icon when its count is `> 0`, leaves it unlit at `0`.

The exact identifiers, buffer sizes, and delivery semantics are specified in
[protocol.md](protocol.md). Because both sides share that one contract, a change
to the key, the UUID, or the message shape must land in `watchface/` and
`android/` together.

## Build & tooling boundaries

- `watchface/` builds with the Pebble SDK (`waf` via the `pebble` CLI). Output
  goes to `watchface/build/`, which is generated and gitignored.
- `android/` will build with Gradle. Its artifacts are gitignored at the repo
  root.

Each component is self-contained; there is no shared build step. The only thing
they share is the protocol.
