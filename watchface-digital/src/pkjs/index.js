// PebbleKit JS companion.
//
// Not the data source. Everything the watchface shows from the phone — calendar
// entries, the phone's battery, the next turn — is supplied by the Android
// companion in `pipe/`, which reads the calendar and talks to the watch over
// PebbleKit Android. PebbleKit JS has no public way to read any of it.
//
// This file sends no AppMessage, deliberately: anything it put on those keys
// would race the companion. What it does do is timeline pins, which are a
// different channel entirely -- see the `nuronPins` note below.
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

// Nuron timeline pins. Not AppMessage, and therefore not a race with the Android
// companion: `insertTimelinePin` writes into the watch's timeline BlobDB and
// touches no `messageKeys` at all. It lives in a watchface rather than in an app
// of Nuron's own because pkjs runs only while its watchapp runs, and a watchface
// is the only thing on the phone that runs all the time. See
// ../common/nuron-pins.js for the full argument.
var nuronPins = require('../common/nuron-pins');

Pebble.addEventListener('ready', function () {
  console.log('proto watchface companion ready');
  nuronPins.start();
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(nuronPins.configUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  nuronPins.onConfigClosed(e && e.response);
});
