# pipe — Android companion

The phone-side half of [**proto**](../README.md). Its job is to compute the
unread-message and missed-call counts on the phone and push them to the Pebble
watchface over Bluetooth.

> **Status: scaffolding.** This is an unmodified Android Studio project. It does
> not talk to the watch yet — there is no PebbleKit dependency, no notification
> listener, and no key constants. Both watch icons stay unlit until this module
> is implemented.

## Build

**Prerequisites:** JDK 21 and the Android SDK. `local.properties` points Gradle
at your SDK and is gitignored — Android Studio generates it on first open, or
create it with `sdk.dir=/path/to/Android/Sdk`.

```sh
./gradlew assembleDebug           # build the debug APK
./gradlew installDebug            # install to a connected device
./gradlew test                    # JVM unit tests
./gradlew connectedAndroidTest    # instrumented tests (device/emulator needed)
```

## Layout

| Path                          | What it is                                        |
| ----------------------------- | ------------------------------------------------- |
| `app/src/main/java/link/dendritik/proto/pipe/` | Application sources.            |
| `app/src/main/AndroidManifest.xml`             | Permissions and components.     |
| `gradle/libs.versions.toml`                    | Version catalog — all deps.     |
| `app/src/main/keepRules/`                      | R8 keep rules (merged by AGP).  |

Gradle project name `ProtoPipe`; application id and namespace
`link.dendritik.proto.pipe`; `minSdk` 24, `targetSdk`/`compileSdk` 36.

## What this module has to implement

The full contract is [docs/protocol.md](../docs/protocol.md); read it before
writing the sender. The essentials:

- **Target UUID** — `f2fc68a6-9636-4694-929b-73c11c33f0e4`.
- **Keys are addressed by numeric id from Android**, not by name: `UnreadCount`
  is `10000` and `MissedCount` is `10001`. Android never sees the string names,
  so these integers must match `watchface/build/appinfo.json`. Define them as
  named constants in one place.
- **Both are non-negative `int32`.** `0` leaves the matching watch icon unlit;
  any value `> 0` lights it.
- **Send on change only**, and keep messages small — the watch's AppMessage
  inbox is 64 bytes.

The counts themselves need a source — a `NotificationListenerService` is the
usual route, and it requires `BIND_NOTIFICATION_LISTENER_SERVICE` plus a
user-granted notification-access grant. Notification contents are sensitive:
derive counts from them and keep the text on the device.

## Conventions

See the repo-root [CONTRIBUTING.md](../CONTRIBUTING.md). The one that matters
most here: a change to the UUID, a key id, the value type, or the AppMessage
buffer sizes must land in `watchface/` and `pipe/` in the same commit.
