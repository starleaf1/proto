# Watch ↔ phone protocol

This is the complete contract between the Pebble watchface (`watchface/`) and
the phone companion (`pipe/`, or the PebbleKit JS stub today). Both sides
must agree on every value here.

## Identity

| Property         | Value                                    | Source                          |
| ---------------- | ---------------------------------------- | ------------------------------- |
| App UUID         | `f2fc68a6-9636-4694-929b-73c11c33f0e4`   | `watchface/package.json`        |
| SDK version      | `3`                                      | `watchface/package.json`        |
| Transport        | Pebble AppMessage (Bluetooth)            | —                               |

The companion must address messages to this UUID. Change it in exactly one
place (`package.json`) and rebuild; the value is embedded into the app binary.

## Messages

Four keys, all sent **phone → watch**:

| Key           | Numeric ID | Type    | Range        | Meaning                                   |
| ------------- | ---------- | ------- | ------------ | ----------------------------------------- |
| `UnreadCount` | `10000`    | `int32` | `>= 0`       | Unread chat-message count. `0` = envelope faded; `> 0` = lit. |
| `MissedCount` | `10001`    | `int32` | `>= 0`       | Missed-call count. Informational once `PhoneState` is in use — see below. |
| `PhoneState`  | `10002`    | `int32` | `0`–`3`      | Phone-icon state. `0` idle, `1` call in progress, `2` ringing, `3` missed. Unknown values clamp to idle. |
| `Heartbeat`   | `10003`    | `int32` | `15`–`3600`  | Seconds until the companion next expects to check in. Sent with **every** message; see [Liveness](#liveness). Out-of-range values clamp. |

`PhoneState` is what colours the phone icon:

| Value | Name      | Colour platforms                | Black-and-white platforms   |
| ----- | --------- | ------------------------------- | --------------------------- |
| `0`   | `IDLE`    | Faded ghost                     | Faded ghost                 |
| `1`   | `ONGOING` | Solid green, steady             | Flashing at **2 Hz**        |
| `2`   | `RINGING` | Flashing green ↔ amber at 4 Hz | Flashing at **4 Hz**        |
| `3`   | `MISSED`  | Solid red                       | Solid black, steady         |

Colour platforms have a hue per state, so only ringing needs to move. The
black-and-white ones (aplite, diorite, flint) have no hue to spend, so **rate**
carries the state instead: static-and-faded is idle, static-and-solid is a missed
call, and the two live states are told apart by how fast they flash. This is the
clearest illustration of why the wire carries meaning rather than pixels — the same
`PhoneState` produces two entirely different rendering strategies.

When several states are true at once the phone resolves them before sending, in
this order: **`RINGING` > `ONGOING` > `MISSED` > `IDLE`**. A phone ringing right now
outranks a call in progress, which outranks one already missed.

The names are declared under `messageKeys` in `watchface/package.json`, in the
order listed there. With `enableMultiJS`, the Pebble build assigns them
sequential numeric ids starting at **10000** in that array order — `UnreadCount`
→ **10000**, `MissedCount` → **10001**, `PhoneState` → **10002**, `Heartbeat` →
**10003** (see `watchface/build/appinfo.json` → `messageKeys`, and
`build/src/message_keys.auto.c`). **Append** any future key to
the end of the array so existing ids don't shift. Both sides reference a key
differently:

- **Watch (C):** by symbol — `MESSAGE_KEY_UnreadCount`, `MESSAGE_KEY_MissedCount`,
  `MESSAGE_KEY_PhoneState`, `MESSAGE_KEY_Heartbeat`.
- **PebbleKit JS:** by name — `Pebble.sendAppMessage({ UnreadCount: n })`; the
  JS runtime resolves each name to its id automatically.
- **PebbleKit Android:** by **numeric id** — `10000` / `10001` / `10002` / `10003`. Android
  does not see the names, so the integers must match. They live in one place,
  [`protocol/Protocol.kt`](../pipe/app/src/main/java/link/dendritik/proto/pipe/protocol/Protocol.kt).
  If keys are ever renumbered (inserting a key out of order can shift ids), update
  the Android constants to match `appinfo.json`.

> Adding a key requires `pebble clean` before `pebble build`. The generated
> `message_keys.auto.h` is not regenerated on an incremental build, so the new
> `MESSAGE_KEY_*` symbol comes back undeclared.

## Who decides what

The split matters, because it is the reason no colour and no blink timing ever
crosses this wire:

- **The phone owns policy.** Which apps count, what a notification channel means,
  whether a call is ringing or in progress, and when something has been dismissed.
- **The watch owns rendering.** Which green, how a flash is timed, and what to do
  on a display that has no colour at all — three of the seven target platforms
  (aplite, diorite, flint) are black-and-white and substitute ink density for hue.

A phone-supplied colour would break this: the companion cannot know which watch
model it is talking to, so it cannot know whether colour is even available. It
sends *meaning*; the watch renders it.

**Flashing is watch-side.** The phone sends a state once, not a stream of blink
frames. The watch runs the flash on a local timer — 125 ms per phase for ringing
(4 Hz), 250 ms for a call in progress on black-and-white (2 Hz) — re-arms at the new
rate when the state changes mid-call, and stops on any static state, on link loss,
and while a modal covers the face. It gives up after 120 s so a companion that dies
mid-call cannot drain the battery; the icon stays lit, it just stops moving.

### `MissedCount` vs `PhoneState`

Both exist, and the watch resolves the overlap with a one-way latch:

- Once a companion has sent `PhoneState` **even once**, that key alone drives the
  phone icon and `MissedCount` becomes informational.
- A companion that never sends `PhoneState` keeps the original behaviour —
  `MissedCount > 0` lights the icon red. This is what keeps
  `pebble send-app-message --int 10001=1` working as a manual test.

The latch is per app run and is not persisted, so it re-arms whenever the
watchface relaunches.

## Liveness

Both status icons are phone-fed, so the watch draws them only while it is sure of
them. It hides the pair — **not faded, not drawn at all** — the moment it stops
being sure. Faded is unavailable as a signal here because faded already means
*idle*, and idle is a positive claim the watch can no longer make. The battery
gauge is unaffected: it is the one indicator the watch computes itself.

There are two independent ways to lose certainty, and the watch learns them
differently.

**Bluetooth loss — the watch detects it alone.** `connection_service_subscribe`
delivers it; no protocol involvement, and nothing for the companion to do. While the
link is down the companion should not send at all, heartbeats included: there is
nobody to hear them. `PebbleSender` stands down on `INTENT_PEBBLE_DISCONNECTED` and
resumes on `INTENT_PEBBLE_CONNECTED`.

**Companion death — only a heartbeat reveals it.** A companion that has crashed, been
force-stopped, or had its notification access revoked leaves the Bluetooth link
perfectly healthy. Silence from a dead companion is byte-for-byte identical to
silence from a companion with no news, so the companion must speak on a schedule
and the watch treats the absence of that as death.

- **Any inbound message is proof of life.** The explicit `Heartbeat` exists only so a
  companion with nothing to report can still speak. Ordinary traffic doubles as one.
- **The companion declares its own cadence.** `Heartbeat` carries *seconds until the
  next check-in*, not a ping token, so the two sides never have to agree a constant
  and the companion can change tier without a watchface update.
- **The watch allows 2.5 periods** before giving up. One missed beat is ordinary on a
  scheduler Android throttles; two in a row is a dead companion.
- **Before the first heartbeat, the watch assumes life.** A watchface relaunch raises
  no event the phone can see, so a watch that started blank would hide the row after
  every excursion into another app. It starts on the slow cadence's grace
  (`900 s × 2.5`), during which every count is zero anyway — visually identical to a
  healthy idle companion.
- **A verdict does not survive a Bluetooth gap.** On reconnect the watch clears
  "dead" and resets to the default grace, since the companion was never given a
  chance to check in while the link was down.

### Cadence

Two tiers, chosen by `PhoneState`, because staleness is not equally harmful in every
state and Android is not equally willing to wake the companion in every state.

| State | Period | Watch blanks after | Scheduler |
| ----- | ------ | ------------------ | --------- |
| `ONGOING` / `RINGING` | **30 s** | 75 s | `Handler.postDelayed` |
| everything else | **900 s** (15 min) | ~37 min | `AlarmManager.setAndAllowWhileIdle` |

A live call is the only state that lies loudly when it goes stale — a phantom ringing
handset — and it is also the only state where the device is certainly interactive, so
the fast tier is both the one that is needed and the one that can actually be
delivered.

Everything else, **including a lit envelope**, rides the slow tier, and this is the
part that is easy to get wrong. The tempting rule is "beat faster whenever an icon is
lit", but a lit envelope on a phone dozing in a pocket is exactly the case that breaks
it: in Doze the system throttles `setAndAllowWhileIdle` to roughly one alarm per 9–15
minutes per app, so a nominally faster cadence is simply not delivered, and the watch
would blank a perfectly correct envelope every night. A stale unread count is a far
smaller lie than a row that flickers.

Note what this does *not* affect: **notification latency**. Real changes are pushed
from `onNotificationPosted`, a system callback into a bound
`NotificationListenerService` that Doze does not defer, and they reach the watch in
`DEBOUNCE_MS`. The heartbeat is only the "nothing has changed and I am still here"
signal. Raising its rate would not make a single notification arrive sooner.

15 min is also the practical floor for any companion without a foreground service:
`setExactAndAllowWhileIdle` needs `SCHEDULE_EXACT_ALARM`, which Android 14 no longer
grants by default and which Play reserves for genuine alarm-clock apps, and
`WorkManager`'s periodic minimum is 15 min regardless. Going faster across the board
means a foreground service and its permanent notification.

## Watch-side behaviour (already implemented)

In [`watchface/src/c/proto.c`](../watchface/src/c/proto.c):

```c
static void inbox_received(DictionaryIterator *iter, void *ctx) {
  bool dirty = false;
  Tuple *u = dict_find(iter, MESSAGE_KEY_UnreadCount);
  if (u) { s_unread = u->value->int32; dirty = true; }   // drives the envelope
  Tuple *m = dict_find(iter, MESSAGE_KEY_MissedCount);
  if (m) { s_missed = m->value->int32; dirty = true; }   // a count; see PhoneState
  Tuple *p = dict_find(iter, MESSAGE_KEY_PhoneState);
  if (p) {                                 // authoritative once ever sent
    int32_t v = p->value->int32;
    s_phone = (v >= PHONE_IDLE && v <= PHONE_MISSED) ? (int)v : PHONE_IDLE;   // clamp
    s_phone_seen = true;
    dirty = true;
  } else if (m && !s_phone_seen) {         // pre-PhoneState companion
    s_phone = s_missed > 0 ? PHONE_MISSED : PHONE_IDLE;
  }
  Tuple *h = dict_find(iter, MESSAGE_KEY_Heartbeat);
  if (h) s_hb_grace_ms = hb_grace_from(h->value->int32);   // companion declares its cadence
  if (!s_companion) { s_companion = true; dirty = true; }  // arriving at all is the proof
  hb_sync();                               // re-arm the watchdog
  if (dirty) {
    flash_sync();                          // one decision point for the timer
    layer_mark_dirty(s_root_layer);
  }
}
// ...
app_message_register_inbox_received(inbox_received);
app_message_open(64, 64);               // inbox / outbox buffers, in bytes
```

Notes for the sender:

- **Inbox buffer is 64 bytes, and all four keys fit in 45 of them.** A Pebble
  dictionary costs `1 + (n * 7) + payload` bytes — a 1-byte header, then 7 bytes of
  tuple header (4-byte key, 1-byte type, 2-byte length) per entry. Four `int32`
  keys are `1 + 4*7 + 4*4 = 45` bytes, leaving 19 bytes of headroom — room for
  exactly **one** more `int32` key (56 bytes); a sixth would need
  `app_message_open` raised. It does **not** need raising today.
- An oversized message is not truncated — it fails to transmit entirely. Since
  `APP_MESSAGE_INBOX_SIZE_MINIMUM` is 124, the 64-byte request always succeeds.
- The watch only **reads** these keys; it never replies with app data. The
  outbox exists only for AppMessage ACK/NACK bookkeeping.
- Unknown keys are ignored — forward-compatible, but the watch acts only on
  keys it knows.

## Sending the value

### PebbleKit JS (current stub)

```js
// watchface/src/pkjs/index.js
Pebble.sendAppMessage(
  { UnreadCount: 3, MissedCount: 1 },
  function ack()  { console.log('delivered'); },
  function nack(e){ console.log('failed', e); }
);
```

### PebbleKit Android (the companion)

Implemented in
[`pebble/PebbleSender.kt`](../pipe/app/src/main/java/link/dendritik/proto/pipe/pebble/PebbleSender.kt);
the constants live in
[`protocol/Protocol.kt`](../pipe/app/src/main/java/link/dendritik/proto/pipe/protocol/Protocol.kt).

```kotlin
val APP_UUID: UUID = UUID.fromString("f2fc68a6-9636-4694-929b-73c11c33f0e4")
const val KEY_UNREAD_COUNT = 10000
const val KEY_MISSED_COUNT = 10001
const val KEY_PHONE_STATE  = 10002
const val KEY_HEARTBEAT    = 10003

val dict = PebbleDictionary().apply {
    addInt32(KEY_UNREAD_COUNT, unreadCount)
    addInt32(KEY_MISSED_COUNT, missedCount)
    addInt32(KEY_PHONE_STATE, phoneState)   // 0 idle, 1 ongoing, 2 ringing, 3 missed
    addInt32(KEY_HEARTBEAT, periodSeconds)  // 30 during a call, 900 otherwise
}
PebbleKit.sendDataToPebble(applicationContext, APP_UUID, dict)
```

> PebbleKit 4.0.1 is a 2016 artifact. Its
> `registerPebbleConnectedReceiver()` calls `registerReceiver` without an
> exported flag, which is a `SecurityException` at `targetSdk` 34+ — register that
> broadcast yourself with `Context.RECEIVER_EXPORTED`, as `PebbleSender.start()`
> does. `sendDataToPebble` and `isWatchConnected` are unaffected.

## Delivery semantics & conventions

- **Send on change, not on a timer** — with the heartbeat as the sole exception.
  Push a new value only when it actually changes; redundant sends waste the
  Bluetooth link and battery. When nothing has changed for a whole heartbeat period,
  re-send the current state anyway (see [Liveness](#liveness)); the watch hides its
  icons otherwise.
- **Put `Heartbeat` in every message.** Then ordinary traffic resets the watch's
  watchdog for free and the explicit heartbeat only fires during real silence. Send
  the period for the state you are sending, so leaving a call re-declares the slow
  cadence in the same message that ends it.
- **Say nothing while Bluetooth is down.** The watch already knows, and hides the
  row without being told. Stand down on `INTENT_PEBBLE_DISCONNECTED`.
- **Send absolute values**, not deltas. The watch replaces its stored value outright.
- **Clamp counts to `>= 0`** and `PhoneState` to `0`–`3`. The watch clamps
  out-of-range `PhoneState` to idle, and lights an icon on `count > 0` — so a
  negative count renders as faded, not lit.
- **Coalesce.** Dismissing a stack of notifications emits one removal callback per
  notification within milliseconds; debounce to the latest value rather than
  sending each one.
- **Re-send on reconnect.** The watch persists nothing — every value resets to `0`
  when the watchface launches or the watch reboots. Treat a
  `PEBBLE_CONNECTED` broadcast as "whatever I last sent is gone" and re-send even
  if nothing changed.
- **Send a terminal `PhoneState` when a call ends** (`0`, or `3` if it was missed).
  Nothing on the watch will correct a phantom ring left by a *live* companion: the
  120 s flash watchdog only stops the animation and leaves the icon lit, and the
  liveness watchdog does not fire while heartbeats keep arriving. It clears only if
  the companion goes silent too.
- **No retry contract is defined yet.** AppMessage may NACK when the watch is
  busy or disconnected; coalesce to the latest value and resend on the next
  opportunity rather than queueing every change.

## Changing the protocol

Any change to the UUID, the key name/id, the value type, or the buffer sizes is
a breaking change that must be made on **both** sides in the same change set.
Adding a new key is backward-compatible for the watch (unknown keys are ignored)
but still requires the watch to be updated before the key does anything.
