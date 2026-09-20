# Watch ↔ phone protocol

The complete contract between the Pebble watchfaces (`watchface-digital/`,
`watchface-analog/`) and the Android companion (`pipe/`). Every side must agree on every
value here, and every side changes in the same commit.

## Identity

| Property | Value | Source |
| --- | --- | --- |
| App UUID, digital | `f2fc68a6-9636-4694-929b-73c11c33f0e4` | `watchface-digital/package.json` |
| App UUID, analog | `bd9bd299-527e-4236-8206-27be3dc9781c` | `watchface-analog/package.json` |
| SDK version | `3` | each face's `package.json` |
| Transport | Pebble AppMessage (Bluetooth) | — |
| Direction | phone → watch, always | — |

A face's UUID lives in exactly one place (its own `package.json`) and is embedded into
the app binary at build time. Change it there and rebuild.

**The companion addresses every message to every UUID.** The firmware keys installed
apps by UUID, so two faces cannot share one, and the protocol has no inbound channel —
there is nothing for the watch to answer with and `isWatchConnected()` reports the
*Pebble app's* link state, not which watchapp is in the foreground. So the companion
cannot know which face is on screen, and does not try: a message addressed to a UUID
nothing is running is NACKed by the firmware and dropped, which costs one round trip and
no state. `Protocol.APP_UUIDS` is the list, and `ProtocolTest` asserts it against both
`package.json`s — the mismatch is otherwise silent, a wrong UUID logging a clean success
on the phone and delivering nothing.

The watch only ever **reads**. It never replies with app data; its outbox exists only
for AppMessage's own ACK/NACK bookkeeping.

## Message keys

Seven keys. Ids are positional from `10000` in the order the names appear under
`messageKeys` in a face's `package.json`. **Both faces declare the same array in the
same order**, which is what lets one set of ids serve them both; `ProtocolTest` fails if
that drifts.

| Key | Id | Type | Meaning |
| --- | --- | --- | --- |
| `Heartbeat` | `10000` | `int32` | Seconds until the companion next expects to check in. Sent with **every** message. Range `15`–`3600`; out-of-range values clamp. |
| `CalEvents` | `10001` | `bytes` | Packed calendar records — see below. |
| `CalFlags` | `10002` | `int32` | Bit 0 `FLUSH`, bit 1 `MORE`. |
| `NavManeuver` | `10003` | `int32` | Turn shape, `0`–`10`. `0` clears navigation. Unknown values clamp to `0`. |
| `NavDistance` | `10004` | `int32` | Distance to the turn, in **tenths** of `NavUnit`. |
| `NavUnit` | `10005` | `int32` | `0` m, `1` km, `2` ft, `3` mi. |
| `PhoneBattery` | `10006` | `int32` | Charge percentage `0`–`100`, or `-1` for unknown. |

Three binding styles, one set of ids:

- **Watch (C):** by symbol — `MESSAGE_KEY_Heartbeat`, `MESSAGE_KEY_CalEvents`, …
- **PebbleKit Android:** by **numeric id**. Android never sees the names, so the
  integers must match `watchface-digital/build/appinfo.json`. They live in one place,
  `pipe/.../protocol/Protocol.kt`.
- **PebbleKit JS:** by name. The `src/pkjs/index.js` stub sends nothing — it exists
  only because the build requires a JS entry point, and anything it sent would race
  the Android companion for the same keys.

> Adding a key requires `pebble clean` before `pebble build`. The generated
> `message_keys.auto.h` is not regenerated on an incremental build, so the new
> `MESSAGE_KEY_*` symbol comes back undeclared.

**Append** new keys to the end of the array so existing ids do not shift.

## `CalEvents` blob

Little-endian throughout. A header, then N fixed-size records.

```
header   u8 version (= 1)   u8 count   u32 reserved (zero)
record   u32 id   i32 startEpochS   u16 durMin   u8 kind   u8 op
```

6 header bytes, 12 bytes per record.

| Field | Notes |
| --- | --- |
| `id` | Stable key for one **instance**. A weekly meeting is one event row and many instances; keying on the event id alone would make each occurrence overwrite the last. Must be stable across scans *and* across process restarts, since it is the identity the watch removes entries by. `CalendarSource` mixes `(eventId, startMinute)` with FNV-1a rather than using `hashCode`, whose contract does not promise stability between runs. |
| `startEpochS` | **Absolute UTC seconds.** Pebble takes its clock from the phone, and absolute timestamps avoid a whole class of bug that "minutes from now" invites — a reference instant that has already moved by the time the watch decodes it. |
| `durMin` | Minutes. **`0` means a point in time**: a task or reminder, which the watch draws as a wedge rather than a band. Clamped to `0`–`65535`, never wrapped. |
| `kind` | `0` appointment, `1` task. Carried separately from `durMin` on purpose — see *Tasks* below. |
| `op` | `0` upsert, `1` remove. |

One blob rather than a message per entry: a first sync can be twenty entries, and
twenty round trips over a transport with no retry contract is twenty chances to be
dropped.

### Sync semantics

- **On connect**, and whenever the companion cannot know what the watch holds, it
  sends a **flush**: `CalFlags` has `FLUSH` set on the first message, and the watch
  drops its whole table before applying the records. This is the only path that
  corrects a watchface's own restored table — the watch keeps its entries across a
  relaunch, but it cannot learn what changed while it was not running.
- **Otherwise** the companion sends only the difference — upserts for entries that
  are new or changed, removes for entries that have left the scan — with
  `CalFlags = 0`.
- `MORE` is set on every message of a multi-message sync except the last.
- An interrupted sync needs no recovery: the next `FLUSH` restarts it.
- A flush carrying **zero** records is how the companion says the next six hours are
  clear. It must still be sent; sending nothing would leave the watch showing what it
  had.
- Companion invariant: never interleave a delta into an in-flight flush.

**Removal is one op regardless of cause.** An entry that was deleted, an appointment
that was cancelled and a task that was completed all leave the scan the same way — by
no longer appearing in it — and the watch renders no reason, so the wire carries none.

### Tasks

Android exposes no standard tasks provider, so the companion derives `kind` from the
duration: a zero-length instance is a task or reminder. Two consequences worth
knowing:

- `CalendarContract` gives `STATUS_CANCELED`, which covers "appointment cancelled",
  but has no notion of completion. **A task ticked off is not observable.** Its
  marker ages out via the watch's linger instead of disappearing at once.
- `kind` is on the wire independently of `durMin` precisely so a local provider that
  *does* expose completion (OpenTasks / Tasks.org, `org.dmfs.tasks`) can be added
  later as a companion-only change — no protocol change, no watchface change.

Google Tasks cannot fill this gap: its on-device data is not reachable by third-party
apps, and its REST API documents `due` as *"Only date information is recorded; the
time portion of the timestamp is discarded"*, so every Google task arrives as a bare
date — exactly the whole-day case the watch is specified to ignore.

## Navigation

The phone sends the maneuver's **shape**, not an arrow, and a number plus a unit, not
a formatted string. The watch owns both, because it is the only side that knows
whether the display it is drawing on has colour at all.

| `NavManeuver` | |
| --- | --- |
| `0` | none — clears the slot |
| `1` | straight |
| `2` / `3` | left / right |
| `4` / `5` | slight left / slight right |
| `6` / `7` | sharp left / sharp right |
| `8` | u-turn |
| `9` | roundabout |
| `10` | arrive |

`NavDistance` is **tenths** of `NavUnit`. A watch face has room for about five
characters, and `0.3 MI` needs the fraction while `250 M` does not; sending tenths
lets the watch decide which to render. It shows the tenth below ten units and drops it
above.

The watch expires the nav row on its own after **2 minutes** without an update,
independently of the liveness watchdog below. A turn instruction is the one thing here
that lies loudly when it goes stale — a phantom "right in 250 m" is worse than no
instruction — and the general watchdog is far too slow to catch it.

> Navigation is **stage 2**. The wire, the watch's rendering and the expiry timer are
> all in place and drivable from the command line; the Google Maps notification parser
> that would populate it is not written yet. See `pipe/CLAUDE.md`.

## Who decides what

**The phone owns policy.** Which calendars count, what a whole-day entry is, whether
an entry is an appointment or a reminder, when something has been removed, what the
phone's charge is, and which turn is next.

**The watch owns rendering, and every threshold that is really a rendering decision.**
Which blue, how deep a band sits in the notch zone, what a turn arrow looks like at
twenty pixels, and when a battery counts as low. One of the three target platforms
(`flint`) has a single ink; the companion cannot know which watch it is talking to, so
it cannot know whether colour is even available. It sends *meaning*.

That is why `PhoneBattery` is a percentage and not a warning: a percentage is a fact,
and the watch applies the ≤ 30% threshold itself.

## Liveness

The watch's bottom row has one job above all others: say so when it cannot vouch for
anything phone-fed. Two independent failures, learned two different ways.

- **Bluetooth loss — the watch detects it alone.** `connection_service_subscribe`
  delivers it; no protocol involvement. While the link is down the companion should
  say nothing at all, heartbeats included: there is nobody to hear them.
  `PebbleSender` stands down on `INTENT_PEBBLE_DISCONNECTED` and re-syncs on
  `INTENT_PEBBLE_CONNECTED`.
- **Companion death — only a heartbeat reveals it.** A companion that has crashed,
  been force-stopped or had its permissions revoked leaves Bluetooth perfectly
  healthy. Its silence is byte-for-byte identical to silence from a companion with no
  news.

Five rules:

- **Any inbound message is proof of life.** The explicit `Heartbeat` key exists only
  so a companion with nothing to report can still speak; ordinary traffic doubles as
  one.
- **The companion declares its own cadence.** `Heartbeat` carries *seconds until the
  next check-in*, not a ping token, so neither side hardcodes a constant and the
  companion can change tier without a watchface update.
- **The watch allows three periods** before raising the alert, which is the companion's
  own retry ladder read from the other end. The slow tier ticks at 600 s and backs a
  failed flush off to 1200 s before trying again, so a single miss puts the next real
  attempt 1800 s out — and alerting sooner than that would be alerting while the
  companion is still visibly working the problem. Three periods is exactly where "it
  missed one and the retry missed too" begins, and that is a dead companion rather than a
  throttled one. It was 2.5 periods against a 900 s tier that had no retry to account
  for; note the absolute grace got *shorter* in the change, 1800 s against 2250.
- **Before the first heartbeat the watch assumes life.** A watchface relaunch raises
  no event the phone can see, so starting with the alert up would flash
  "companion down" after every excursion into another app. It starts on the slow
  tier's grace (600 s × 3).
- **A verdict does not survive a Bluetooth gap.** On reconnect the watch clears
  "dead" and resets to the default grace — the companion was never given a chance to
  check in, and a 30 s navigation cadence must not be inherited into a reconnect where
  the companion is back on its slow tier.

### Cadence

| State | Period | Watch alerts after | Scheduler |
| --- | --- | --- | --- |
| Navigating | **30 s** | 90 s | `Handler.postDelayed` |
| Everything else | **600 s** (10 min) | 30 min | the companion's one `setAndAllowWhileIdle` tick, backing off to 3600 s while sends fail |

Navigation is the only state that earns the fast tier, and it is also the one state
where the device is definitely interactive, so a plain handler post fires on time and
costs nothing.

Everything else has to ride the slow tier, and its period is set by the platform rather
than by taste. Doze allows `setAndAllowWhileIdle` to fire **no more than once per nine
minutes, per app**; the power-management tables state the same limit as seven while-idle
alarms an hour. 600 s is six an hour — inside that budget with one alarm spare, and
inside the ten an hour the working-set standby bucket allows. A nominally faster cadence
would simply not be delivered, and an undelivered cadence is worse than a slow one: the
watch would alert every night on a promise the phone cannot keep.

Below Working set the buckets get tighter than this tick needs. Frequent allows two
alarms an hour and Rare one, against the six 600 s asks for. A companion held open by a
foreground service or bound on companion-device presence should sit in Active or Working
set, but that is a claim about a particular device and release rather than a guarantee —
`adb shell am get-standby-bucket link.dendritik.proto.pipe` is how to settle it, on the
same device that settles whether the companion binding holds at all.

Neither escape hatch is available. `setExactAndAllowWhileIdle` needs
`SCHEDULE_EXACT_ALARM`, which Android 14 no longer grants by default and which Play
reserves for actual alarm-clock apps; `WorkManager`'s periodic minimum is fifteen
minutes, which is slower than what this already has.

**A tick whose flush does not land backs the next one off** — 600, 1200, 2400, then a
3600 s cap — and the first send that gets through from any path resets it, including a
delta off a calendar edit and the flush on reconnect. Nothing is queued and nothing is
resent: the next tick rescans the whole window regardless, so what backs off is how hard
the companion tries, not a message it is holding on to. Backoff only ever lengthens the
delay, so it cannot breach the budget above, and against a watch that is not listening it
is the difference between six pointless wake-ups an hour and one. The cap is the largest
period the protocol lets `Heartbeat` carry, which is not a coincidence.

**The watch is told the base period throughout, never the backed-off one.** A backed-off
period could only be declared by a message that arrived, and a message arriving is what
ends the backoff — so the base is the only period the watch can coherently be given.
While the retries are failing the watch is *supposed* to notice, and three periods is
when it does.

**That throttle is per app, not per alarm, which is why the slow tier is not a
scheduler of its own.** The companion already needs one periodic wake-up to re-scan the
six-hour window, and a second alarm at the same period would not buy a second wake-up —
it would queue behind the first for one budget and make both late, spending the
three-period grace on self-inflicted contention. So there is exactly one alarm: it re-scans, sends the
delta if there is one, and speaks a bare heartbeat only if there was not. Since any
arrival is proof of life, a tick that sent a delta has already beaten.

A payload sent shortly *before* a tick also counts, so the beat is suppressed if
anything went out in the last 60 s. That guard is deliberately far shorter than the
period: the tick cadence is fixed, so a beat skipped at one tick moves the next message
a full period out, and suppressing for the whole 600 s could put 1200 s plus a
Doze-slipped tick between messages — two thirds of the grace spent to save one four-byte
message.

Note what this does **not** affect: latency. A real change is pushed the moment it
happens; the `ContentObserver` fires, the scan runs and the delta goes out
synchronously. (The 250 ms debounce in `PebbleSender` applies to the nav and battery
scalars, which arrive in bursts; calendar traffic does not go through it.)

### What survives an outage

The previous design hid its phone-fed icons whenever certainty was lost. This one does
not, because calendar entries are not counts:

- **Calendar markers and the countdown stay on screen.** An entry is timestamped and
  ages out on its own, so it does not go stale the way a count does — and the bottom row
  is already saying the companion is unreachable, so nothing is claiming to be
  complete.
- **Navigation is dropped** on either failure. A stale turn is the one genuinely
  dangerous lie.
- **The phone's battery keeps its last value**, but the companion-down alert outranks
  it, so it is not on screen anyway.
- **The watch's own battery is unaffected.** It is the one thing the watch computes
  itself.

## Buffers

The watch opens `app_message_open(512, 64)`.

A Pebble dictionary costs `1 + (n × 7) + payload` — a 1-byte header, then 7 bytes of
tuple header per entry. The largest message is a full `CalEvents` sync: 24 records is
`6 + 24 × 12 = 294` payload bytes, plus two `int32` keys, so `1 + 3×7 + 294 + 8 = 324`
bytes. 512 leaves comfortable headroom, and `app_message_inbox_size_maximum()` is far
above it.

The 24-record cap lives in `EventBlob.MAX_RECORDS` and is deliberately well under the
limit, because **an oversized AppMessage is not truncated — it fails to transmit
entirely.**

Unknown keys are ignored, so the watch is forward-compatible, but it acts only on keys
it knows.

## Delivery rules

- **Send on change, not on a timer** — with two exceptions, both riding the same
  periodic tick rather than a timer of their own.

  The first is the heartbeat, which has nowhere else to live.

  The second is that **the tick sends a full flush rather than a delta.** Switching
  watchface does not drop the Bluetooth link, so nothing signals it, and every delta
  sent while a face was not running went to a UUID that NACKed it. The face restores its
  own table on launch, so the swap is no longer a blank timeline — but that table is as
  of the last time that face was on screen, and nothing on the watch can reconcile it.
  With two faces installed, switching between them is the ordinary thing to do, so the
  periodic re-flush is what makes the swap self-healing — worst case one period of a
  stale marker rather than one period of an empty window. It costs `6 + 24 × 12 = 294`
  payload bytes against a bare heartbeat's four, once per period. The change-driven path
  still sends a delta; this exception is the tick's alone.
- **Put `Heartbeat` in every message**, which is what makes ordinary traffic a life sign
  and lets the tick stay silent when it has just sent a delta.
- **Say nothing while Bluetooth is down.**
- **Send absolute values**, never deltas, for everything except the calendar — and
  there, a diff is only ever sent against a table the companion knows it flushed.
- **Flush on reconnect.** Every scalar resets when the watchface launches or the watch
  reboots — nav, the phone battery, the declared cadence. The calendar table is the one
  thing that survives a relaunch, and it survives as the watch's *own* memory of the
  last thing it was told, which the companion has no way to read back; a diff against
  it would be a diff against a phantom.
- **Clamp** before sending: percentages to `0`–`100`, distances to `>= 0`, durations
  to `0`–`65535`.
- **Coalesce the scalars.** A burst of nav or battery updates collapses to one send
  after 250 ms. Calendar edits are not debounced — each change signal re-scans — but a
  burst that nets out to no change sends nothing, because an empty diff is not
  transmitted.
- **Bail on the first failed chunk** of a multi-message sync, and do not record it as
  sent. The remaining chunks belong to a sync the watch will never see the start of;
  the next change or heartbeat redoes the whole thing.
- **Clear navigation explicitly** when a route ends (`NavManeuver = 0`). The watch's
  2-minute expiry is a backstop, not the contract.

## Versioning

Any change to a UUID, a key's name, id or type, the blob layout, or the buffer sizes
is a breaking change, and must be made on **both** sides in the same change set. There
is no version negotiation; the blob's `version` byte exists so the watch can ignore a
companion it does not understand rather than misread it.

## Driving it by hand

Every key is drivable from the command line. The `int32` ones go directly through
`pebble send-app-message`; the `CalEvents` blob is packed by
each face's own `tools/send-demo-events.py`, which is also the reference for the layout
above in a language you can read a hexdump in. The script reads the UUID out of the
`package.json` beside it, so it always targets the face it ships with.

```sh
# the demo calendar: a flush, then the records covering every marker case
watchface-analog/tools/send-demo-events.py --emulator flint   # or watchface-digital/

# turn right in 250 m, on the fast heartbeat tier
pebble send-app-message --emulator flint --vnc --int 10003=3 10004=2500 10005=0 10000=30

# phone battery at 28% (the watch's threshold is 30)
pebble send-app-message --emulator flint --vnc --int 10006=28 10000=600

# declare a 15 s cadence, then stay quiet: the companion-down alert appears in ~45 s
pebble send-app-message --emulator flint --vnc --int 10000=15
```
