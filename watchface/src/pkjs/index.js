// PebbleKit JS companion.
//
// The watchface renders two phone-driven status icons: an "unread message"
// envelope (`UnreadCount`) and a "missed call" handset (`MissedCount`). Pebble
// exposes no on-watch API for either count, so this companion is the only place
// those values can come from. There is no public way to read them from
// PebbleKit JS either, so for now this is a stub: both icons stay unlit
// (count 0) until a real data source is wired up here.
//
// To light an icon, send an integer under its key, e.g.:
//   Pebble.sendAppMessage({ UnreadCount: 3, MissedCount: 1 });

Pebble.addEventListener('ready', function () {
  console.log('proto watchface companion ready');
});
