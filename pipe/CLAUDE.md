# Android companion (`pipe/`)

The phone-side half of the **proto** monorepo. Kotlin + Jetpack Compose, namespace
`link.dendritik.proto.pipe`. The sibling `watchface/` directory holds the Pebble app.
See the repo-root `docs/protocol.md` for the wire contract — it is the only thing the
two components share.

> **Run every `gradlew` command from this directory** (`pipe/`), not the repo root.

## Commands

```bash
./gradlew assembleDebug
./gradlew installDebug
./gradlew test                      # the wire format and the scan diff
./gradlew clean assembleDebug test  # before trusting a green build after a refactor
```

JDK 21 and the Android SDK are required. No JDK on your PATH? Android Studio ships one:
`JAVA_HOME=~/android-studio/jbr ./gradlew test`.

> **`./gradlew clean` after deleting a source file.** Kotlin's incremental compiler will
> report a green build from cached outputs, and can emit warnings citing paths from an
> entirely different checkout. A refactor is exactly the situation that triggers it.

## Structure

```
link.dendritik.proto.pipe
├── PipeEngine.kt           the work; host-agnostic
├── PipeCompanionService.kt host: system-bound while the watch is near (API 31+)
├── PipeService.kt          host: foreground service — the fallback, and the notification
├── PipeHost.kt             chooseHost + the CompanionDeviceManager calls
├── BootReceiver.kt         re-arms whichever host this device uses
├── MainActivity.kt         permission grants, watch pairing + diagnostics
├── PipeStatus.kt           observable, diagnostics only
├── calendar/
│   ├── CalendarSource.kt   ContentResolver over CalendarContract.Instances
│   ├── CalendarWatcher.kt  ContentObserver + ACTION_PROVIDER_CHANGED
│   └── EventFacts.kt       pure data — the framework boundary
├── battery/PhoneBattery.kt ACTION_BATTERY_CHANGED
├── pebble/
│   ├── EventBlob.kt        pure: the wire format + EventDiff
│   └── PebbleSender.kt     debounce, coalesce, chunk, heartbeat
└── protocol/Protocol.kt    key ids and the wire enums
```

## The architectural rule worth keeping

**Framework types stop at `CalendarSource` and `PhoneBattery`.** Everything below them
sees `EventFacts` and plain integers, which is why the entire wire format is covered by
JVM unit tests with no device and no Robolectric. Preserve that when adding a source: a
`NavFacts` equivalent should be pure data produced by a thin framework-facing adapter,
exactly as `EventFacts` is.

**Add a test when you touch packing or diffing.** A byte offset is easy to get subtly
wrong, impossible to notice by hand, and the symptom appears as markers in the wrong
place on a watch — the far end of a Bluetooth link, on a different machine, in C.
`EventBlobTest` asserts offsets rather than round-tripping, deliberately: a Kotlin
unpacker could share the same mistake as the packer and both would agree.

## Things that look like bugs and are not

- **The tick receiver is registered at runtime, not in the manifest.** A manifest
  receiver would let the system restart a dead process just to announce that it is alive
  — which is the one thing a liveness heartbeat must never be able to claim.
- **Two hosts, and `chooseHost` is the only thing that picks between them.** It is pure,
  and unit-tested, because the wrong answer is invisible: choose the companion host where
  the platform will not bind it and the app simply stops sending, with no notification
  left to show that it has. `MainActivity.maybeStart` is the only place that enforces
  mutual exclusion, and it does so by stopping the foreground service — which is the
  moment the notification disappears.
- **`PipeCompanionService` overrides the deprecated `String` callbacks, not the
  `AssociationInfo` ones.** The `String` form is the only one that exists on API 31–32,
  and on 33+ the platform forwards to it for any MAC-backed association, which a
  `BluetoothDeviceFilter` association always is. The newer overloads would mean naming a
  class absent from the two oldest releases the host supports, for nothing.
- **`CompanionHost`, not `Companion`.** The obvious name is shadowed by the implicit
  companion object of any class that has one.
- **Starting the engine before PebbleKit has a channel is correct.** Bluetooth presence
  usually beats the Pebble app's connection broadcast; `syncCalendar` no-ops while the
  watch is not connected, and the `INTENT_PEBBLE_CONNECTED` that follows drives the full
  flush. Do not add coordination for this.
- **`PipeStatus.watchPresent` is diagnostics that earn their keep.** The companion host's
  whole premise — that the platform binds us on presence — cannot be checked from a
  desktop. If that row reads `no` with the watch on the wrist, the binding is not working
  on that device.
- **There is one periodic alarm, not two, and the heartbeat does not own it.** The tick
  re-scans the window and only sends a bare heartbeat when that scan had nothing to say,
  because every message already carries `Heartbeat` and any arrival is proof of life. A
  separate beat alarm at the same period is not redundancy but harm: Doze throttles
  `setAndAllowWhileIdle` per *app*, so the two would queue behind one budget and make
  each other late.
- **`PebbleSender.beat()` suppresses itself for 60 s after any send, and 60 s is not an
  arbitrary number.** It has to be far below the period. The tick cadence is fixed, so a
  skipped beat moves the next message a whole period out; suppressing for the full 900 s
  could leave 1800 s plus a Doze-slipped tick between messages, which crosses the watch's
  2.5-period grace and raises the alert the suppression was meant to avoid.
- **`ContextCompat.registerReceiver` everywhere, never PebbleKit's own helper.**
  PebbleKit 4.0.1 is a 2016 artifact; its `registerPebbleConnectedReceiver()` calls
  `registerReceiver` without an exported flag, which is a hard `SecurityException` at
  targetSdk 34+.
- **A watch reconnect triggers a full flush, not a diff.** The watchface persists
  nothing, so after a relaunch it holds no events, and a diff against what we *think* it
  has would be a diff against a phantom. `sentEvents` is cleared on disconnect for the
  same reason.
- **`CalendarSource` re-checks the permission before every query**, not just at startup.
  The grant can be revoked while the service is running, and the `SecurityException` is
  caught as well — the check and the query are not atomic.
- **The instance id is a hand-rolled FNV mix, not `hashCode()`.** It is the identity the
  watch removes entries by, so it must be stable across process restarts, which
  `hashCode`'s contract does not promise. It hashes the start time in *minutes* so that
  sub-minute jitter in a provider's reported start cannot re-key an entry.
- **`STATUS IS NULL OR STATUS != STATUS_CANCELED`**, not a bare inequality. `STATUS` is
  nullable and `NULL != 2` is `NULL` in SQL — which is not true — so the bare form
  silently drops every entry whose status is unset, which is most of them.
- **Only whole-percent battery changes are reported.** `ACTION_BATTERY_CHANGED` is sticky
  and fires on every temperature and voltage wobble.

## Not yet implemented: navigation

The protocol keys, the watch's turn-arrow glyphs, the top slot's priority resolution and
the nav-slot expiry timer all exist and work — `pebble send-app-message` can drive them
end to end (see `docs/protocol.md`). Nothing on this side populates them.

The gap is deliberate. Google Maps exposes no public turn-by-turn API, so the only
no-root source is its ongoing navigation notification, read through a
`NotificationListenerService`. The maneuver has to come from the notification's
small-icon resource id — a lookup table that must be **calibrated against a real device**
and re-checked whenever Maps updates — with an English-only keyword fallback parsed from
`EXTRA_TITLE`. None of that can be built or verified from a desktop, so the watch half
was finished first.

When adding it:

- A new `nav/` package with a `NavFacts` pure-data boundary, matching `EventFacts`.
- A `NotificationListenerService`, its manifest entry with
  `BIND_NOTIFICATION_LISTENER_SERVICE`, and the notification-access grant flow in
  `MainActivity` (a special access, not a runtime permission).
- `PipeEngine` forwards the facts; `PebbleSender.submitNav` already exists.
- Note that active navigation is what earns the fast 30 s heartbeat tier — currently
  nothing does, so the companion always runs on the 900 s tier.

## Permissions

| Permission | Why |
| --- | --- |
| `READ_CALENDAR` | Runtime. The only data source. |
| `REQUEST_COMPANION_RUN_IN_BACKGROUND` | Normal. The preferred host. |
| `REQUEST_OBSERVE_COMPANION_DEVICE_PRESENCE` | Normal. Lets us ask to be bound on presence. |
| `REQUEST_COMPANION_START_FOREGROUND_SERVICES_FROM_BACKGROUND` | Normal. Escape hatch to the fallback host. |
| `POST_NOTIFICATIONS` | Runtime (API 33+). Only requested when the *fallback* host will be used — the companion host posts nothing. |
| `FOREGROUND_SERVICE` + `_DATA_SYNC` | The fallback host. |
| `RECEIVE_BOOT_COMPLETED` | Nothing rebinds a foreground service after a reboot, and a presence-observation request does not reliably survive one either. |

No Bluetooth permission, deliberately: the association dialog scans on the system's
behalf. Add one only if a real device proves it necessary.

The `<queries>` block is not optional: on Android 11+ PebbleKit cannot see the Pebble
app — neither its broadcasts nor the content provider behind `isWatchConnected()` —
without it.
