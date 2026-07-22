// PebbleKit JS companion.
//
// The watchface renders an "unread message" envelope icon that is driven by the
// `UnreadCount` AppMessage key. Pebble exposes no on-watch notification-count
// API, so this companion is the only place that value can come from. There is
// no public way to read the phone's unread count from PebbleKit JS either, so
// for now this is a stub: the envelope stays unlit (count 0) until a real data
// source is wired up here.
//
// To light the envelope, send an integer under MESSAGE_KEY_UnreadCount, e.g.:
//   var keys = require('message_keys');
//   Pebble.sendAppMessage({ UnreadCount: 3 });

Pebble.addEventListener('ready', function () {
  console.log('proto watchface companion ready');
});
