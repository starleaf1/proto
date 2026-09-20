# Android companion (`pipe/`)

The phone-side component of the **proto** monorepo. Kotlin + Jetpack Compose, namespace
`link.dendritik.proto.pipe`. The sibling `watchface-digital/` and `watchface-analog/`
directories hold the two Pebble watchfaces this companion feeds; it addresses both
unconditionally, because the protocol has no inbound channel and there is no way to
learn which face is on screen. See the repo-root `docs/protocol.md` for the wire
contract — it is the only thing the three components share.

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
├── PipeService.kt          host: foreground service — the fallback, the notification,
│                        and Android 15's six-hour cap on it
├── PipeHost.kt             chooseHost + the CompanionDeviceManager calls
├── BootReceiver.kt         re-arms whichever host this device uses
├── MainActivity.kt         permission grants, watch pairing, re-sync + diagnostics
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
- **The manual re-sync rides that same runtime registration, and is therefore
  undeliverable when nothing is running.** `PipeEngine.requestResync` broadcasts an action
  only ever registered at runtime, so a press with no engine alive reaches nothing. That
  is the point rather than the cost: a manifest receiver would let the button resurrect
  the process, and the first thing the fresh engine would do is flush a table it has just
  built and declare a cadence it has not been keeping. It is a broadcast at all because
  nothing outside a host holds an engine — both keep it in a private field and
  `PipeService` does not bind — and both hosts share the activity's process, so the
  registration is the whole of the plumbing.
- **The re-sync button is gated on the PebbleKit link, not on the association.** They are
  different facts: the association is what buys the notification-free host, and the link
  is what makes a send land. `syncCalendar` returns early with the link down, so a press
  gated the other way would be a silent no-op with a watch paired but out of range.
  `MainActivity.refresh` re-reads the link itself, via
  `PebbleSender.watchConnected(context)`, because the engine's `reconcile` is the only
  other writer of `PipeStatus.watchConnected` and on screen that can be a whole 600 s
  period stale — longer still once the tick has backed off.
- **A press can pull the tick alarm in but never pushes it out, and is not
  rate-limited.** The declared cadence is a promise about when the *next* scheduled
  message arrives; re-arming at the base period would slide that a full period, and with
  a three-period grace the button would be a way to bring on the alert it exists to
  clear. The one move it may make is the other direction: a press that succeeds out of a
  backed-off state resets `misses` and re-arms at the base, which is a shortening, and it
  is the recovery the backoff is waiting for. `noteSuccess` and `noteMiss` are the only
  things that write `misses`. As for rate-limiting: the reason to press it is that
  the watch is suspected of holding a stale table, which a button that sometimes declines
  cannot settle. The spinner debounces the UI for 450 ms, which is a different thing.
- **The spinner's 450 ms is a minimum, not a measurement.** The flush runs synchronously
  inside the broadcast's own dispatch on the main thread, so it cannot animate anything
  while it works and it is normally over within a frame of the press — a spinner tied
  strictly to it would be a flicker nobody could read. `SPINNER_MIN_MS` is the shortest
  press-to-feedback that reads as an action having been taken, and because the work
  finishes inside it, what follows the delay is the outcome rather than a guess at it.
  This is also why the result line is hidden while the spinner is up: until then it is
  either stale or saying what the spinner already says. If the flush ever moves off the
  main thread this becomes a floor rather than the whole duration, and nothing changes.
  The press is debounced inside `onClick`, not by `enabled`: a disabled M3 button drops
  its content to 38% alpha over a 12% container, and a spinner nobody can see is the
  thing the minimum exists to prevent.
- **A failed tick backs the next one off; a failed anything-else does not, and the
  asymmetry is the whole point.** 600, 1200, 2400, then a 3600 s cap, reset by the first
  send that lands from any path — including a delta off a calendar edit and the reconnect
  flush, because either one is proof the link works and neither is a tick. But only a
  *flush* may count a miss, because `syncCalendar` returns `false` for two unlike
  reasons: it could not send, or the delta came out empty. A flush always has something
  to say even with no records, so a `false` from one means the link; an empty delta is
  the ordinary case, since Android's calendar provider fires its observer on all manner
  of internal churn, and counting those would back the tick off to an hour on a link that
  never faltered. Nothing is queued and nothing is resent: the next tick
  rescans the whole window anyway, so what backs off is how hard we try, not a message we
  are holding. It only ever lengthens the delay, so it cannot breach the Doze budget the
  base period is chosen against, and against an unreachable watch it is six pointless
  wake-ups an hour turned into one. **The watch is still told the base period**, because
  a backed-off one could only be declared by a message that arrived and an arriving
  message is what ends the backoff — so while the retries fail the watch is meant to
  raise `NO LINK`, and its three-period grace is where it does.
- **Two hosts, and `chooseHost` is the only thing that picks between them.** It is pure,
  and unit-tested, because the wrong answer is invisible: choose the companion host where
  the platform will not bind it and the app simply stops sending, with no notification
  left to show that it has. `MainActivity.maybeStart` is the only place that enforces
  mutual exclusion, and it does so by stopping the foreground service — which is the
  moment the notification disappears.
- **Android 15 puts three separate restrictions on the `dataSync` host, and each is
  handled in exactly one place.** They are not variations of one thing, which is why the
  fix is not one line:
  1. A **six-hour budget per 24 hours**, after which the platform calls
     `Service.onTimeout` and throws `RemoteServiceException: "A foreground service of
     type dataSync did not stop within its timeout"` if the service does not stop within
     a few seconds. `PipeService.onTimeout` stops it, first statement.
  2. **`startForeground` then refuses**, with `ForegroundServiceStartNotAllowedException`
     — an `IllegalStateException` — saying "Time limit already exhausted". That is reached
     from `BootReceiver` and from a `START_STICKY` restart, neither of which is the user
     in the app, so `startInForeground()` returns a `Boolean` rather than letting it take
     `onCreate` down with it.
  3. **`BOOT_COMPLETED` may not launch a `dataSync` service at all** on these releases —
     refused outright, not throttled. `BootReceiver.startFallback` does not try.
     `MY_PACKAGE_REPLACED` is a different broadcast and is not covered, so an update
     still restores the host.
- **It is the two-argument `onTimeout` that matters, not the one-argument one.**
  `onTimeout(int)` is Android 14's and the platform calls it only for `shortService`;
  `onTimeout(int, int)` is Android 15's and is the one called for `dataSync`. Overriding
  the older signature would compile, read as careful, and never fire. Overriding a method
  that does not exist below `minSdk` is safe for the same reason
  `PipeCompanionService`'s `String` callbacks are — it is a method, not a named type.
- **Nothing restarts the host after a timeout, deliberately.** The budget is spent; a
  service that restarted itself would be refused outright or would burn the remainder in
  a loop. The documented reset is *the user bringing the app to the foreground*, and
  `MainActivity.onResume` already calls `maybeStart` on every resume — so the recovery
  path existed before the cap did and needed no new code. A reboot has the same answer.
- **The cap can only ever affect users who declined the pairing dialog, and the UI says
  so rather than just reporting it.** API 35 is well past `MIN_COMPANION_SDK`, so every
  device that can hit the six-hour limit could also have been on the companion host,
  which has no limit. That is why the durable sentence lives in the *pairing* card, where
  it is actionable and permanently true, while `PipeStatus.capped` only carries the event.
  Expect the event line to be nearly unreachable in practice: foregrounding the app is
  what resets the budget, so by the time anyone can read the screen the restart has
  usually already succeeded and cleared the flag.
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
  `setAndAllowWhileIdle` per *app* — no more than once per nine minutes, which the
  power-management tables also give as seven while-idle alarms an hour — so the two would
  queue behind one budget and make each other late. The 600 s base is six an hour, which
  leaves exactly one alarm of headroom and is the reason there is no second scheduler.
- **`PebbleSender.beat()` suppresses itself for 60 s after any send, and 60 s is not an
  arbitrary number.** It has to be far below the period. The tick cadence is fixed, so a
  skipped beat moves the next message a whole period out; suppressing for the full 600 s
  could leave 1200 s plus a Doze-slipped tick between messages, which spends two thirds
  of the watch's three-period grace to save one four-byte message.
- **`ContextCompat.registerReceiver` everywhere, never PebbleKit's own helper.**
  PebbleKit 4.0.1 is a 2016 artifact; its `registerPebbleConnectedReceiver()` calls
  `registerReceiver` without an exported flag, which is a hard `SecurityException` at
  targetSdk 34+.
- **A watch reconnect triggers a full flush, not a diff.** The watchface restores its
  own event table when it relaunches, so after a gap it holds something — and nothing in
  the protocol lets us read back *what*, since it runs one way. A diff against what we
  *think* it has would be a diff against a phantom. `sentEvents` is cleared on
  disconnect for the same reason.
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

The protocol keys, the watch's turn-arrow glyphs, the warnings row's priority resolution and
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
  nothing does, so the companion always runs on the 600 s tier.

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
