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

Three keys, all sent **phone → watch**:

| Key           | Numeric ID | Type    | Range        | Meaning                                   |
| ------------- | ---------- | ------- | ------------ | ----------------------------------------- |
| `UnreadCount` | `10000`    | `int32` | `>= 0`       | Unread chat-message count. `0` = envelope faded; `> 0` = lit. |
| `MissedCount` | `10001`    | `int32` | `>= 0`       | Missed-call count. Informational once `PhoneState` is in use — see below. |
| `PhoneState`  | `10002`    | `int32` | `0`–`3`      | Phone-icon state. `0` idle, `1` call in progress, `2` ringing, `3` missed. Unknown values clamp to idle. |

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
→ **10000**, `MissedCount` → **10001**, `PhoneState` → **10002** (see
`watchface/build/appinfo.json` → `messageKeys`, and
`build/src/message_keys.auto.c`). **Append** any future key to
the end of the array so existing ids don't shift. Both sides reference a key
differently:

- **Watch (C):** by symbol — `MESSAGE_KEY_UnreadCount`, `MESSAGE_KEY_MissedCount`,
  `MESSAGE_KEY_PhoneState`.
- **PebbleKit JS:** by name — `Pebble.sendAppMessage({ UnreadCount: n })`; the
  JS runtime resolves each name to its id automatically.
- **PebbleKit Android:** by **numeric id** — `10000` / `10001` / `10002`. Android
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

- **Inbox buffer is 64 bytes, and all three keys fit in 34 of them.** A Pebble
  dictionary costs `1 + (n * 7) + payload` bytes — a 1-byte header, then 7 bytes of
  tuple header (4-byte key, 1-byte type, 2-byte length) per entry. Three `int32`
  keys are `1 + 3*7 + 3*4 = 34` bytes, leaving 30 bytes of headroom, or two more
  `int32` keys. `app_message_open` does **not** need raising.
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

val dict = PebbleDictionary().apply {
    addInt32(KEY_UNREAD_COUNT, unreadCount)
    addInt32(KEY_MISSED_COUNT, missedCount)
    addInt32(KEY_PHONE_STATE, phoneState)   // 0 idle, 1 ongoing, 2 ringing, 3 missed
}
PebbleKit.sendDataToPebble(applicationContext, APP_UUID, dict)
```

> PebbleKit 4.0.1 is a 2016 artifact. Its
> `registerPebbleConnectedReceiver()` calls `registerReceiver` without an
> exported flag, which is a `SecurityException` at `targetSdk` 34+ — register that
> broadcast yourself with `Context.RECEIVER_EXPORTED`, as `PebbleSender.start()`
> does. `sendDataToPebble` and `isWatchConnected` are unaffected.

## Delivery semantics & conventions

- **Send on change, not on a timer.** Push a new value only when it actually
  changes; redundant sends waste the Bluetooth link and battery.
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
  The watch has no timeout that would clear a stale ringing state on its own,
  beyond the 120 s flash watchdog which stops the animation but leaves the icon lit.
- **No retry contract is defined yet.** AppMessage may NACK when the watch is
  busy or disconnected; coalesce to the latest value and resend on the next
  opportunity rather than queueing every change.

## Changing the protocol

Any change to the UUID, the key name/id, the value type, or the buffer sizes is
a breaking change that must be made on **both** sides in the same change set.
Adding a new key is backward-compatible for the watch (unknown keys are ignored)
but still requires the watch to be updated before the key does anything.
