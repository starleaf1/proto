// PebbleKit JS companion.
//
// Not the data source. The watchface's three phone-driven values — an "unread
// message" envelope (`UnreadCount`), a missed-call count (`MissedCount`) and the
// phone-call state (`PhoneState`) — are supplied by the Android companion in
// `pipe/`, which reads the notification shade and talks to the watch over
// PebbleKit Android. There is no public way to read any of it from PebbleKit JS.
//
// This file stays a stub deliberately: it exists because the build wants a JS
// entry point, and if it sent anything it would race the companion for the same
// keys.
//
// For manual testing prefer the CLI, which addresses keys by numeric id
// (see ../../../docs/protocol.md):
//   pebble send-app-message --emulator basalt --vnc --int 10000=3 10002=2

Pebble.addEventListener('ready', function () {
  console.log('proto watchface companion ready');
});
