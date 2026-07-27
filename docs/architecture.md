# Architecture

`proto` is two programs that cooperate over Bluetooth:

```
┌──────────────────────────┐              AppMessage              ┌─────────────────────────┐
│  Phone companion         │  UnreadCount, MissedCount,           │  Pebble watchface       │
│  (pipe/ — Android;       │  PhoneState  (int32, phone → watch)  │  (watchface/)           │
│   reads notifications)   │  ────────────────────────────────►   │  draws the envelope     │
│                          │           sent on change             │  and phone icons        │
└──────────────────────────┘                                      └─────────────────────────┘
```

The dividing line: **the phone decides meaning, the watch decides pixels.** The
companion resolves which apps matter, what a notification channel means, and
whether a call is ringing or in progress. The watch turns that into a colour — or,
on the three black-and-white platforms, into ink density. No colour crosses the
wire, because the companion cannot know which watch model is on the other end.

## Components

### `watchface/` — the Pebble watchapp

Single-window C app. A root layer's update proc paints everything each time the
watch state changes:

- **Time** — hours in Orbitron 54, rendered from a minute tick handler.
- **Date** — Rajdhani Light 22.
- **Status icons** — an **unread-message envelope** and a **phone-call handset**,
  drawn as a centered pair. The connection service gates the pair: while the phone
  link is down neither icon is drawn, since both are phone-fed and the watch has no
  current value for either. The handset has four states: faded when idle, green
  during a call, flashing green/amber while ringing, red for a missed call. On the
  three black-and-white platforms there is no hue to spend, so **rate** carries the
  state instead — static-faded is idle, static-solid is a missed call, and the two
  live states are told apart by flashing at 2 Hz (in a call) versus 4 Hz (ringing).
  That flash is the only animation on the face. It runs on a local `app_timer` owned
  solely by `flash_sync()`, which picks the rate from the state, re-arms when the
  state changes mid-call, and stops on every static state, on link loss, while a
  modal covers the face, and after a 120 s watchdog.
- **Battery gauge** — from the battery state service, just above the numeral.
  The one indicator the watch computes itself, so the one that is always shown.

The envelope and the missed-call handset are the only elements the watch cannot
compute on its own. Pebble provides no on-watch API for the phone's notification
count or call history, so the watchface holds an `s_unread` counter and an
`s_missed` counter — each starts at `0` (icon unlit) and is updated only when an
AppMessage arrives carrying `UnreadCount` / `MissedCount`.

Entry point and lifecycle live in [`watchface/src/c/proto.c`](../watchface/src/c/proto.c);
the AppMessage inbox handler is `inbox_received()`.

### `pipe/` — the Android companion

Kotlin + Jetpack Compose, `namespace link.dendritik.proto.pipe`. It reads the
notification shade, decides what each notification means, and pushes the result to
the watch with PebbleKit Android.

The pipeline is four small pieces, arranged so that every decision is a pure
function and only the edges touch Android:

| Piece | File | Role |
| ----- | ---- | ---- |
| `ProtoNotificationListener` | `notify/ProtoNotificationListener.kt` | The `NotificationListenerService`. Flattens each `StatusBarNotification` into a `NotificationFacts`, and is the only class that touches framework types. |
| `Classifier` | `notify/Classifier.kt` | Pure. `NotificationFacts` + config → `Verdict`: ignore, chat, or a call in some state. |
| `ActiveSet` | `notify/ActiveSet.kt` | Holds the live verdict per notification key and folds them into one `IconState`. |
| `PebbleSender` | `pebble/PebbleSender.kt` | Debounces, drops no-op sends, and re-sends on watch reconnect. |

`PipeConfig` (`config/PipeConfig.kt`) holds the routing rules — currently
hard-coded defaults, shaped so a settings screen could persist them unchanged.
Each rule maps an app's packages plus a set of **notification-channel patterns** to
the envelope or the phone icon, with the notification's own `category` and a
per-app fallback behind that. Channel ids are the primary signal because that is
what distinguishes WhatsApp's *Calls* channel from its *Messages* channel.

The PebbleKit JS stub ([`watchface/src/pkjs/index.js`](../watchface/src/pkjs/index.js))
remains in place and still sends nothing; it is only the JS runtime the watchface
requires, not a data source.

## Data flow

1. A notification is posted, updated, removed, or re-ranked. The listener rebuilds
   the facts for it — or, on connect and on a ranking change, for the whole shade.
2. `Classifier` drops it if the app is unconfigured, if it is a group summary, or
   if the user silenced it. Two call states are exempt from the silence rule, since
   for them the icon reports a state rather than raising an alert: a call **in
   progress** (which every dialler posts on a deliberately silent channel) and a
   **missed** call. A silenced *ring* is still dropped.
3. `ActiveSet` folds every live verdict into `{ unreadCount, missedCount, phone }`.
   The phone state is the most urgent live call — **ringing > ongoing > missed >
   idle** — while `missedCount` still counts every missed call underneath it.
   Because the set is rebuilt from live notifications rather than counted
   incrementally, dismissing the last one returns the icon to faded on its own.
4. `PebbleSender` coalesces a burst into one message and sends it only if it
   differs from the last one delivered.
5. The watchface's `inbox_received()` stores whichever values arrived, calls
   `flash_sync()`, and marks the root layer dirty.
6. The next paint lights the envelope when `UnreadCount > 0` and colours the
   handset from `PhoneState` — and draws neither while the phone link is down.

Notification **content** never leaves the phone. Titles and text are read only to
spot answer/decline buttons on a call, and are never stored, logged, or transmitted;
the watch receives three integers.

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
