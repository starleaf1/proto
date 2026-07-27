# pipe — Android companion

The phone-side half of [**proto**](../README.md). It reads the notification shade,
decides what each notification means, and pushes the result to the Pebble watchface
over Bluetooth as three integers.

## Build

**Prerequisites:** JDK 21 and the Android SDK. `local.properties` points Gradle
at your SDK and is gitignored — Android Studio generates it on first open, or
create it with `sdk.dir=/path/to/Android/Sdk`.

```sh
./gradlew assembleDebug           # build the debug APK
./gradlew installDebug            # install to a connected device
./gradlew test                    # JVM unit tests — the routing decision table
./gradlew connectedAndroidTest    # instrumented tests (device/emulator needed)
```

Android Studio's bundled JBR satisfies the JDK 21 toolchain if you have no other
JDK: `JAVA_HOME=~/android-studio/jbr ./gradlew test`.

## First run

Grant **notification access** from the app's home screen (or Settings →
Notifications → Device & app notifications). It is a special access grant, not a
runtime permission, so it cannot be requested from a dialog — the button opens the
system screen. Until it is granted the shade is invisible and both watch icons stay
faded.

The app has no other UI role: once granted, the system binds
`ProtoNotificationListener` and everything happens in the background. The home
screen is diagnostics — grant state, watch connection, and what is currently being
sent.

## How it works

Every routing decision is a pure function; only the edges touch Android.

| Path | Role |
| ---- | ---- |
| `notify/ProtoNotificationListener.kt` | The `NotificationListenerService`. Flattens each `StatusBarNotification` into `NotificationFacts` — the only class that touches framework types. |
| `notify/Classifier.kt` | Pure. Facts + config → `Verdict`: ignore, chat, or a call in some state. |
| `notify/ActiveSet.kt` | The live verdict per notification key, folded into one `IconState`. |
| `pebble/PebbleSender.kt` | Debounces bursts, skips no-op sends, re-sends on watch reconnect. |
| `config/PipeConfig.kt` | The routing rules and their defaults. |
| `protocol/Protocol.kt` | UUID and the three numeric key ids. The only place they appear. |

`NotificationFacts` is deliberately free of Android classes, which is what lets the
whole decision table be unit-tested on the JVM with no device and no Robolectric —
see `app/src/test/`.

**Dismissal works because state is derived, not counted.** `ActiveSet` holds the
live set and recomputes the snapshot; it never increments a counter. So when the
last chat notification is swiped away the set is empty, the snapshot is all-zero,
and the envelope returns to faded on its own. Counters would drift the first time
an app updated a notification we had already counted.

## Routing rules

In `PipeConfig.DEFAULT`. Each rule maps an app's packages to the envelope or the
phone icon, matched in this order:

1. **Notification channel id** against the rule's `chatChannels` / `callChannels`
   (case-insensitive substring, so `messages_2` matches `message`). This is the
   primary signal — it is what separates WhatsApp's *Calls* channel from its
   *Messages* channel. Calls are tested first so a broad chat pattern cannot
   swallow a call channel.
2. **The notification's own `category`** — `msg`, `email`, `call`, `missed_call`,
   or the presence of a `CallStyle` call type. This carries Gmail, whose channel
   ids are per-account and opaque.
3. **The rule's `fallback`**, which is deliberately `null` for Instagram, Messenger
   and email so that a *like* or a *follow* never lights the envelope.

| App | Envelope | Phone icon |
| --- | -------- | ---------- |
| Telegram | yes | yes |
| WhatsApp | yes | yes |
| SMS (default app, resolved at runtime) | yes | — |
| Instagram | DMs only | video calls |
| Facebook Messenger | messages only | calls |
| Email (Gmail, Outlook, Proton, K-9, Thunderbird, Yahoo) | yes | — |
| Phone (default dialler, resolved at runtime) | — | yes |

The default SMS app and dialler are per-device, so they are resolved at listener
startup via `Telephony.Sms.getDefaultSmsPackage` and
`TelecomManager.defaultDialerPackage`, and unioned with a static fallback list.

### Call state

`Classifier.callState` picks ringing / ongoing / missed from the most certain
signal available, in order: an explicit `missed_call` category or a `missed`
channel; then `CallStyle`'s own call type; then the shape of the notification (a
full-screen intent, or both an answer *and* a decline action, means it is ringing
right now); then channel-name hints; then whether the system still considers it
ongoing. Answer/decline label matching is a last resort because labels are
localised.

When more than one call notification is live, `ActiveSet` sends the most urgent:
**ringing > ongoing > missed > idle**, from `PhoneState.precedence`. `missedCount`
still counts every missed call even while a newer ringing or ongoing call outranks
it for the icon's appearance.

### Silent notifications

Silenced notifications are dropped — channel importance below
`IMPORTANCE_DEFAULT`, falling back to `priority` on API 24–25 where channels do not
exist.

**Two call states are exempt**, listed in `PipeConfig.silentExemptCallStates`,
because for these the phone icon reports a *state* rather than raising an alert:

- **`ONGOING`** — every dialler posts its in-call notification on a deliberately
  silent low-importance channel, so applying the rule literally would make "green
  while on a call" unreachable.
- **`MISSED`** — a call you missed is worth showing even from a silenced channel;
  that is what a missed-call indicator is for.

`RINGING` is deliberately *not* exempt: a ring the user chose to silence is an alert
they asked not to receive. Chats are never exempt. Set
`silentExemptCallStates = emptySet()` for the strictly literal behaviour.

## Privacy

Notification content never leaves the device. Titles and action labels are read
only to detect an incoming call's answer/decline buttons, and are never stored,
logged, or transmitted. What reaches the watch is three integers: two counts and a
state enum.

## Conventions

See the repo-root [CONTRIBUTING.md](../CONTRIBUTING.md). The one that matters most
here: a change to the UUID, a key id, the value type, or the AppMessage buffer
sizes must land in `watchface/` and `pipe/` in the same commit. The full contract is
[docs/protocol.md](../docs/protocol.md).

**PebbleKit is a 2016 artifact** (`com.getpebble:pebblekit:4.0.1`, Maven Central,
no transitive dependencies). Its `registerPebbleConnectedReceiver()` calls
`registerReceiver` without an exported flag, which throws at `targetSdk` 34+ — that
broadcast is registered manually in `PebbleSender.start()`. `sendDataToPebble` and
`isWatchConnected` are fine as-is.
