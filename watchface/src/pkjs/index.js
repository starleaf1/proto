// PebbleKit JS companion.
//
// Not the data source. Everything the watchface shows from the phone — calendar
// entries, the phone's battery, the next turn — is supplied by the Android
// companion in `pipe/`, which reads the calendar and talks to the watch over
// PebbleKit Android. PebbleKit JS has no public way to read any of it.
//
// This file stays a stub deliberately: it exists because the build wants a JS
// entry point, and if it sent anything it would race the companion for the same
// keys.
//
// For manual testing prefer the CLI, which addresses keys by numeric id
// (see ../../../docs/protocol.md):
//   pebble send-app-message --emulator flint --vnc --int 10003=3 10004=2500 10005=0
//
// The one thing the CLI cannot do is send the packed `CalEvents` byte array —
// `send-app-message` takes integers only. Two ways round that:
//   * `PROTO_DEMO=1 pebble build` seeds the event table in C, for screenshotting
//     the dial. It bypasses the decoder in `wire.c`.
//   * To exercise the decoder itself, temporarily send a hand-built blob from
//     here; `Pebble.sendAppMessage` accepts a JS array as a byte array. Revert
//     afterwards.

Pebble.addEventListener('ready', function () {
  console.log('proto watchface companion ready');
});
