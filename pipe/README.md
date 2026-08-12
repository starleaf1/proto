# pipe — Android companion

The phone-side half of [**proto**](../README.md). It reads the calendar and the
phone's battery, and pushes them to the Pebble watchface over Bluetooth.

The watch has no calendar access of its own, so everything on its strip comes from
here. What crosses the wire is deliberately thin: per entry, an id, an absolute start
time, a duration and two enum bytes. **No titles, locations, attendees or
descriptions** — the watch draws a position and a duration, and nothing else about an
entry leaves the phone.

## Build

**Prerequisites:** JDK 21 and the Android SDK. `local.properties` points Gradle at your
SDK and is gitignored — Android Studio generates it on first open, or write it by hand.

```sh
./gradlew assembleDebug
./gradlew installDebug
./gradlew test
```

No JDK on your PATH? Android Studio ships one:
`JAVA_HOME=~/android-studio/jbr ./gradlew test`.

Run `./gradlew clean assembleDebug test` before trusting a green build after any
refactor — Kotlin's incremental compiler will report success from cached outputs after a
source file has been deleted.

## First run

From the app's home screen:

- **Calendar access** — the app's only data source. Without it there is nothing to send
  and the strip stays empty.
- **Pair watch** — on Android 12 and later. Recommended: it removes the ongoing
  notification. See below.
- **Notifications** — only asked for if you have not paired. It is for the companion's
  *own* ongoing notification, not for reading yours.

## Why it needs a host — and why pairing is the better one

Something has to keep the process alive. Watching the calendar for changes, watching the
battery, and the heartbeat the watch uses to decide whether to trust what is on screen
all have to outlive the activity.

The previous version of this app read the notification shade, and the system kept its
`NotificationListenerService` bound for its own reasons — that binding was, incidentally,
the whole process keeper. With the shade no longer read, there is no such host.

**Pairing gets one back.** Associating the watch through Android's own device-pairing
dialog asks the system to bind `PipeCompanionService` for as long as the watch is nearby,
which is exactly the arrangement that was lost — and it needs no notification, no
Bluetooth permission of ours, and no battery-optimisation exemption. When the watch is
out of range nothing runs, which is correct: the watch is the only consumer.

**Without pairing there is a fallback**, and it is the foreground service with the
permanent notification. That is what runs on Android 11 and earlier, or if you decline
the pairing dialog. Everything works identically; it just costs you a notification.

You can see which one is in use, and undo the pairing, from the app's status card.

## How it works

```
calendar changed ─┐
watch reconnected ─┼──► PipeEngine.reconcile ──► CalendarSource.query
15 minutes passed ─┘                                      │
                                                          ▼
                                            EventDiff ──► EventBlob ──► PebbleSender
```

Three change signals feed one action. The third matters as much as the others: nothing
"changes" when time passes, but the six-hour window has slid forward and the answer is
different.

| Piece | Role |
| --- | --- |
| `PipeEngine` | Owns everything with a lifecycle. Indifferent to which host holds it. |
| `PipeCompanionService` | Host, API 31+. Bound by the system while the paired watch is nearby. |
| `PipeService` | Host, fallback. Foreground service, and the ongoing notification. |
| `PipeHost` | `chooseHost`, plus the `CompanionDeviceManager` calls. |
| `CalendarSource` | Queries `CalendarContract.Instances` over `[now − 2 h, now + 6 h]`. |
| `CalendarWatcher` | `ContentObserver` for local edits, `ACTION_PROVIDER_CHANGED` for syncs landing from the server. |
| `PhoneBattery` | `ACTION_BATTERY_CHANGED`, filtered to whole-percent changes. |
| `EventBlob` / `EventDiff` | Pure. The wire format and the scan diff. |
| `PebbleSender` | Debounce, coalesce, dedup, chunk, heartbeat. |
| `BootReceiver` | Re-establishes the host after a reboot or an app update. |

Framework types stop at `CalendarSource`. Everything below it sees `EventFacts`, which
is what lets the entire wire format be unit-tested on the bare JVM with no device and no
Robolectric.

## What the calendar query does and does not include

- **Whole-day entries are excluded.** They have no position on a timeline, and no
  duration that would fit one.
- **Cancelled entries are excluded** (`STATUS_CANCELED`).
- **A zero-length entry is a task or reminder**, and the watch draws it as a triangle
  rather than a band. Duration is the only signal available — Android exposes no
  standard tasks provider.
- **A completed task is not detectable.** `CalendarContract` has no notion of
  completion, so a ticked-off reminder's marker ages out via the watch's linger
  instead of disappearing at once. See `docs/protocol.md` for why Google Tasks cannot
  fill this gap, and what a local tasks provider would.

## Not yet implemented

**Navigation.** The protocol, the watch's turn-arrow rendering and the nav-slot expiry
all exist and are drivable from the command line, but nothing here populates them yet.
Google Maps exposes no public turn-by-turn API; the only no-root source is its ongoing
notification, and the maneuver has to be read from a small-icon resource id that needs
calibrating against a real device. Until then the warnings row falls through to the battery
alerts. See `pipe/CLAUDE.md`.
