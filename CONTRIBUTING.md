# Contributing

`proto` is a monorepo with two components that share one protocol. Work inside the
component you're changing; touch the protocol only when both sides move together.

## Repository layout

```
proto/
  watchface/   Pebble watchapp (C, Pebble SDK)
  pipe/        Android companion (Kotlin, calendar reader)
  docs/        Architecture + protocol contract
```

## Watchface (`watchface/`)

**Prerequisites:** the [Pebble SDK](https://developer.repebble.com) and its `pebble`
CLI. Run every command from `watchface/`.

```sh
pebble build                             # all three target platforms
PROTO_DEMO=1 pebble build                # with synthetic calendar entries
pebble install --emulator flint          # or emery, or gabbro
pebble screenshot --no-open s.png
```

In a headless environment (no window server), add `--vnc` to every command that
touches the emulator.

**Verify UI changes with a screenshot.** After changing anything visual, build,
install and screenshot the emulator before considering the change done — and do it on
`flint` as well as a colour platform. Most of the design's prominence cues exist
specifically so that the black-and-white platform still works, and a change that reads
beautifully on `gabbro` can be invisible on `flint`.

**Seed the calendar with `watchface/tools/send-demo-events.py`.** It packs the
`CalEvents` blob and sends it to a running watchface, covering every marker case — a
running band across the pointer, two overlapping bands that must flatten, two point
entries too close to draw apart, an overdue reminder above the pointer, a marker sitting
on top of a band, and one entry past the horizon that must not draw. `--clear` and
`--remove` drive the flush and delta paths.

`PROTO_DEMO=1 pebble build` compiles the same set in, for tooling older than
pebble-tool 5.0.39, which is where `send-app-message --bytes` arrived. Keep
`demo_seed()` in `src/c/proto.c` and `DEMO` in the script in step, and never ship a
build with the flag set.

Everything else is CLI-drivable too — see the snippets at the end of `docs/protocol.md`.

## Android companion (`pipe/`)

**Prerequisites:** JDK 21 and the Android SDK.

```sh
./gradlew assembleDebug
./gradlew test              # the wire format and the scan diff
./gradlew installDebug
```

No JDK on your PATH? Android Studio ships one:
`JAVA_HOME=~/android-studio/jbr ./gradlew test`.

**Run `clean` before trusting a green build.** Kotlin's incremental compiler will
happily report success from cached outputs after a source file has been deleted, which
is exactly the situation a refactor creates: `./gradlew clean assembleDebug test`.

**Keep the wire format pure and tested.** `EventBlob`, `EventDiff` and everything they
touch take plain `EventFacts`, not framework objects, so the whole format is covered by
JVM unit tests with no device and no Robolectric. Framework types stop at
`CalendarSource`. **Add a test case when you touch packing or diffing** — a byte
offset is easy to get subtly wrong and impossible to notice by hand, and the symptom
shows up as markers in the wrong place on a watch.

## Changing the protocol

1. Update `docs/protocol.md`.
2. Change **both** `watchface/` and `pipe/` in the same commit.
3. Keep the numeric key ids in `pipe/.../protocol/Protocol.kt` in sync with
   `watchface/build/appinfo.json`. Android addresses keys by integer and never sees
   the names.
4. **Append** new keys to `messageKeys` in `watchface/package.json` — ids are
   positional from 10000, so inserting one silently renumbers everything after it.
5. Run `pebble clean` after adding a key, or the new `MESSAGE_KEY_*` symbol comes back
   undeclared.
6. Check the buffer arithmetic in `docs/protocol.md` still holds. An oversized
   AppMessage is not truncated — it fails to transmit entirely.

## Conventions

- **Keep policy on the phone and rendering on the watch.** Send a state enum, a count
  or a timestamp; never a colour, a blink frame, or a formatted string. One of the
  three target platforms is black-and-white, and only the watch knows which one it is.
- **Prefer shape over colour for anything load-bearing.** Colour is a second channel
  on two of three platforms, so it may reinforce a distinction but never carry it
  alone.
- **Match the surrounding code.** Follow the existing style, naming and comment
  density in each component (`watchface/src/c/strip.c` is the reference for C).
- **Comment the decisions, not the code.** Several things in here look like bugs and
  are not — the pointer drawing last, an upcoming band being shallower rather than
  lighter, the strip curving on `gabbro` and running straight elsewhere. Each has a note saying what
  was tried and why it failed. Keep that up.
- **Don't commit build output.** `watchface/build/`, `*.pbw` and Android/Gradle
  artifacts are gitignored and regenerable.
- **Keep commits scoped to one component** where possible; protocol changes are the
  deliberate exception.
