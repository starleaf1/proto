# proto

A Pebble watchface and its phone-side companion, developed together in one
repository.

The watchface shows the time, date, battery, Bluetooth-connection state, an
**unread-message envelope**, and a **missed-call** indicator. Pebble exposes no
on-watch API for a phone's notification count or call history, so those numbers
are pushed to the watch by a companion app over Bluetooth. The companion is
moving from a PebbleKit JS stub to a native **Android** app — this repo now
houses both sides.

## Repository layout

| Path          | What it is                                                                 |
| ------------- | -------------------------------------------------------------------------- |
| `watchface/`  | The Pebble watchapp — C, built with the Pebble SDK.                        |
| `pipe/`       | The Android companion app (scaffolded; will send `UnreadCount` and `MissedCount`). |
| `docs/`       | [Architecture](docs/architecture.md) and the [protocol contract](docs/protocol.md). |
| `CONTRIBUTING.md` | Dev environment setup and conventions.                                 |

## How the two sides talk

Everything the watch and phone exchange rides on two Pebble AppMessage keys:

| Field        | Value                                    |
| ------------ | ---------------------------------------- |
| App UUID     | `f2fc68a6-9636-4694-929b-73c11c33f0e4`   |
| Message keys | `UnreadCount`, `MissedCount` — non-negative `int32` |
| Direction    | phone → watch                            |
| Behaviour    | `0` leaves the matching icon unlit; `> 0` lights it |

That's the whole integration surface. Full details — buffer sizes, delivery
semantics, and the PebbleKit Android call to send the value — are in
[docs/protocol.md](docs/protocol.md).

## Quick start

### Watchface (Pebble)

Requires the [Pebble SDK](https://developer.repebble.com). Run all commands from
`watchface/`:

```sh
cd watchface
pebble build                          # build for all target platforms
pebble install --emulator emery       # run in the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

See [watchface/CLAUDE.md](watchface/CLAUDE.md) for the full command reference
(emulator control, screenshots, headless/VNC usage).

### Android companion

The module lives under `pipe/` (Gradle project `ProtoPipe`, namespace
`link.dendritik.proto.pipe`):

```sh
cd pipe
./gradlew assembleDebug            # requires JDK 21 and the Android SDK
```

It is currently Android Studio scaffolding — it does not talk to the watch yet.
The work ahead is a notification listener to source the two counts, plus
[PebbleKit Android](https://developer.repebble.com/guides/communication/using-pebblekit-android/)
to send `UnreadCount` and `MissedCount`. See [pipe/README.md](pipe/README.md).

## Documentation

- [docs/architecture.md](docs/architecture.md) — components and data flow
- [docs/protocol.md](docs/protocol.md) — the AppMessage contract in full
- [CONTRIBUTING.md](CONTRIBUTING.md) — how to set up and contribute
- Pebble SDK reference — <https://developer.repebble.com>
