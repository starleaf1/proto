# proto

A Pebble watchface and its phone-side companion, developed together in one
repository.

The watchface shows the time, date, battery, an **unread-message envelope**, and a
**phone-call** indicator that is faded when idle, green during a call, flashing
while ringing, and red for a missed call. Pebble exposes no on-watch API for a
phone's notifications or call state, so those are pushed to the watch by a
companion app over Bluetooth. The companion is a native **Android** app that reads
the notification shade — this repo houses both sides.

Because both icons are phone-fed, they are shown only while the companion link
is up; when it drops, the whole icon row is hidden and the battery gauge is the
only indicator left on screen.

## Repository layout

| Path          | What it is                                                                 |
| ------------- | -------------------------------------------------------------------------- |
| `watchface/`  | The Pebble watchapp — C, built with the Pebble SDK.                        |
| `pipe/`       | The Android companion — reads notifications, sends `UnreadCount`, `MissedCount`, `PhoneState`. |
| `docs/`       | [Architecture](docs/architecture.md) and the [protocol contract](docs/protocol.md). |
| `CONTRIBUTING.md` | Dev environment setup and conventions.                                 |

## How the two sides talk

Everything the watch and phone exchange rides on two Pebble AppMessage keys:

| Field        | Value                                    |
| ------------ | ---------------------------------------- |
| App UUID     | `f2fc68a6-9636-4694-929b-73c11c33f0e4`   |
| Message keys | `UnreadCount` (`10000`), `MissedCount` (`10001`), `PhoneState` (`10002`) — `int32` |
| Direction    | phone → watch                            |
| Behaviour    | `UnreadCount > 0` lights the envelope; `PhoneState` selects the phone icon's colour — `0` idle, `1` in a call, `2` ringing, `3` missed |

The phone sends *meaning*, never colour: three of the seven target platforms are
black-and-white, and only the watch knows which one it is. Flashing is likewise
watch-side — the phone says "ringing" once, and the watch runs the animation.

That's the whole integration surface. Full details — buffer arithmetic, delivery
semantics, and the PebbleKit Android call — are in
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
./gradlew installDebug             # install to a connected device
```

On first launch, grant it **notification access** from the app's home screen —
without that the shade is invisible to it and both icons stay faded. The app then
runs entirely in the background as a `NotificationListenerService`. See
[pipe/README.md](pipe/README.md) for the routing rules and how to change them.

## Documentation

- [docs/architecture.md](docs/architecture.md) — components and data flow
- [docs/protocol.md](docs/protocol.md) — the AppMessage contract in full
- [CONTRIBUTING.md](CONTRIBUTING.md) — how to set up and contribute
- Pebble SDK reference — <https://developer.repebble.com>
