# Watch ↔ phone protocol

This is the complete contract between the Pebble watchface (`watchface/`) and
the phone companion (`android/`, or the PebbleKit JS stub today). Both sides
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

Two keys, both sent **phone → watch**:

| Key           | Numeric ID | Type    | Range        | Meaning                                   |
| ------------- | ---------- | ------- | ------------ | ----------------------------------------- |
| `UnreadCount` | `10000`    | `int32` | `>= 0`       | Unread-message count. `0` = envelope unlit; `> 0` = lit. |
| `MissedCount` | `10001`    | `int32` | `>= 0`       | Missed-call count. `0` = handset unlit; `> 0` = lit. |

The names are declared under `messageKeys` in `watchface/package.json`, in the
order listed there. With `enableMultiJS`, the Pebble build assigns them
sequential numeric ids starting at **10000** in that array order — `UnreadCount`
→ **10000**, `MissedCount` → **10001** (see `watchface/build/appinfo.json` →
`messageKeys`, and `build/src/message_keys.auto.c`). **Append** any future key to
the end of the array so existing ids don't shift. Both sides reference a key
differently:

- **Watch (C):** by symbol — `MESSAGE_KEY_UnreadCount`, `MESSAGE_KEY_MissedCount`.
- **PebbleKit JS:** by name — `Pebble.sendAppMessage({ UnreadCount: n })`; the
  JS runtime resolves each name to its id automatically.
- **PebbleKit Android:** by **numeric id** — `10000` / `10001`. Android does not
  see the names, so the integers must match. If keys are ever renumbered
  (inserting a key out of order can shift ids), update the Android constants to
  match `appinfo.json`.

## Watch-side behaviour (already implemented)

In [`watchface/src/c/proto.c`](../watchface/src/c/proto.c):

```c
static void inbox_received(DictionaryIterator *iter, void *ctx) {
  bool dirty = false;
  Tuple *u = dict_find(iter, MESSAGE_KEY_UnreadCount);
  if (u) { s_unread = u->value->int32; dirty = true; }   // drives the envelope
  Tuple *m = dict_find(iter, MESSAGE_KEY_MissedCount);
  if (m) { s_missed = m->value->int32; dirty = true; }   // drives the missed-call icon
  if (dirty) layer_mark_dirty(s_root_layer);             // repaint
}
// ...
app_message_register_inbox_received(inbox_received);
app_message_open(64, 64);               // inbox / outbox buffers, in bytes
```

Notes for the sender:

- **Inbox buffer is 64 bytes.** Both `int32` keys fit comfortably in a single
  message; keep messages small. If the protocol grows further, raise the
  `app_message_open` sizes on the watch first.
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

### PebbleKit Android (planned companion)

```java
// UUID and key id come from this document — keep them in sync with appinfo.json.
final UUID APP_UUID = UUID.fromString("f2fc68a6-9636-4694-929b-73c11c33f0e4");
final int  KEY_UNREAD_COUNT = 10000;
final int  KEY_MISSED_COUNT = 10001;

PebbleDictionary data = new PebbleDictionary();
data.addInt32(KEY_UNREAD_COUNT, unreadCount);
data.addInt32(KEY_MISSED_COUNT, missedCount);
PebbleKit.sendDataToPebble(getApplicationContext(), APP_UUID, data);
```

## Delivery semantics & conventions

- **Send on change, not on a timer.** Push a new value only when the count
  actually changes; redundant sends waste the Bluetooth link and battery.
- **Send the current absolute count**, not a delta. The watch replaces its
  stored value outright.
- **Clamp to `>= 0`.** Negative values are undefined; the watch treats any
  non-zero value as "lit".
- **No retry contract is defined yet.** AppMessage may NACK when the watch is
  busy or disconnected; the companion should coalesce to the latest value and
  resend on the next ACK opportunity rather than queueing every change.

## Changing the protocol

Any change to the UUID, the key name/id, the value type, or the buffer sizes is
a breaking change that must be made on **both** sides in the same change set.
Adding a new key is backward-compatible for the watch (unknown keys are ignored)
but still requires the watch to be updated before the key does anything.
