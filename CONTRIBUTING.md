# Contributing

`proto` is a monorepo with two components that share one protocol. Work inside
the component you're changing; touch the protocol only when both sides move
together.

## Repository layout

```
proto/
  watchface/   Pebble watchapp (C, Pebble SDK)
  pipe/        Android companion (Kotlin, notification listener)
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
pebble screenshot --no-open s.png # capture the running emulator
```

In a headless environment (no window server), add `--vnc` to every command that
touches the emulator. See [watchface/CLAUDE.md](watchface/CLAUDE.md) for the
full command reference and emulator-button control.

**Verify UI changes with a screenshot.** After changing anything visual, build,
install, and screenshot the emulator before considering the change done.

## Android companion (`pipe/`)

**Prerequisites:** JDK 21 and the Android SDK. Run all Gradle commands from
`pipe/`:

```sh
cd pipe
./gradlew assembleDebug           # build the debug APK
./gradlew test                    # unit tests — the routing decision table
```

No JDK on your PATH? Android Studio ships one:
`JAVA_HOME=~/android-studio/jbr ./gradlew test`.

The routing logic is deliberately pure — `Classifier` and `ActiveSet` take plain
`NotificationFacts`, not framework objects, so the whole decision table is covered
by JVM unit tests with no device or Robolectric. **Add a test case there when you
touch routing**; a channel-id pattern is easy to get subtly wrong and impossible to
notice by hand. See [pipe/README.md](pipe/README.md).

## The protocol

The watch and phone share three AppMessage keys documented in
[docs/protocol.md](docs/protocol.md). Before changing the UUID, a message key
or its numeric id, the value type, or the AppMessage buffer sizes:

1. Update [docs/protocol.md](docs/protocol.md).
2. Change **both** `watchface/` and `pipe/` in the same commit/PR.
3. Keep the numeric key ids in `pipe/.../protocol/Protocol.kt` in sync with
   `watchface/build/appinfo.json`.
4. **Append** new keys to `messageKeys` in `watchface/package.json` — ids are
   positional from 10000, so inserting one shifts every id after it. Run
   `pebble clean` after adding a key, or the generated `MESSAGE_KEY_*` symbol
   comes back undeclared.

Keep policy on the phone and rendering on the watch: send a state enum, never a
colour or a blink frame. Three of the seven target platforms are black-and-white,
and only the watch knows which one it is.

## Conventions

- **Match the surrounding code.** Follow the existing style, naming, and comment
  density in each component (`watchface/src/c/proto.c` is the reference for C).
- **Don't commit build output.** `watchface/build/`, `*.pbw`, and Android/Gradle
  artifacts are gitignored and regenerable — keep them out of commits.
- **Keep commits scoped to one component** where possible; protocol changes are
  the deliberate exception.
