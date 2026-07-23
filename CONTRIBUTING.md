# Contributing

`proto` is a monorepo with two components that share one protocol. Work inside
the component you're changing; touch the protocol only when both sides move
together.

## Repository layout

```
proto/
  watchface/   Pebble watchapp (C, Pebble SDK)
  android/     Android companion (planned)
  docs/        Architecture + protocol contract
```

## Watchface (`watchface/`)

**Prerequisites:** the [Pebble SDK](https://developer.repebble.com) and its
`pebble` CLI.

Run all `pebble` commands from `watchface/`:

```sh
cd watchface
pebble build                      # build for every target platform
pebble install --emulator emery   # run in an emulator
pebble screenshot --scale 6       # capture the running emulator
```

In a headless environment (no window server), add `--vnc` to every command that
touches the emulator. See [watchface/CLAUDE.md](watchface/CLAUDE.md) for the
full command reference and emulator-button control.

**Verify UI changes with a screenshot.** After changing anything visual, build,
install, and screenshot the emulator before considering the change done.

## Android companion (`android/`)

Scaffolding is not committed yet. When it lands it will build with Gradle
(`./gradlew assembleDebug`) and use PebbleKit Android to send `UnreadCount` to
the watch. Add build/setup steps to `android/README.md` as the module grows.

## The protocol

The watch and phone share a single AppMessage key documented in
[docs/protocol.md](docs/protocol.md). Before changing the UUID, the message key
or its numeric id, the value type, or the AppMessage buffer sizes:

1. Update [docs/protocol.md](docs/protocol.md).
2. Change **both** `watchface/` and `android/` in the same commit/PR.
3. Keep the numeric key id in the Android code in sync with
   `watchface/build/appinfo.json`.

## Conventions

- **Match the surrounding code.** Follow the existing style, naming, and comment
  density in each component (`watchface/src/c/proto.c` is the reference for C).
- **Don't commit build output.** `watchface/build/`, `*.pbw`, and Android/Gradle
  artifacts are gitignored and regenerable — keep them out of commits.
- **Keep commits scoped to one component** where possible; protocol changes are
  the deliberate exception.
