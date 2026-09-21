// Nuron timeline pins.
//
// ## Why there is code in pkjs at all
//
// `src/pkjs/index.js` says, and has always said, that it stays a stub because
// anything it sent would race the Android companion in `pipe/` for the same
// AppMessage keys. That reasoning still holds and this does not break it: pins
// are not AppMessage. `Pebble.insertTimelinePin` hands a pin to the phone app,
// which writes it into the watch's timeline BlobDB. It touches no `messageKeys`,
// it cannot NACK a calendar sync, and the two channels cannot interfere.
//
// What it does need is a reason to be *here* rather than in an app of Nuron's
// own, and the reason is lifetime: pkjs runs only while its watchapp runs, and a
// watchface runs essentially all the time. A Nuron watchapp's JS would execute
// for the few seconds a month someone opened it, and the timeline would be a
// month stale. This is the only always-on JS on the phone.
//
// ## Local pins, and what that buys
//
// `insertTimelinePin` needs no timeline token, no API key and no appstore
// listing, and there is no 300-request budget and no 30-minute-to-3-hour
// delivery lag: the pin goes phone -> BlobDB -> watch over Bluetooth. It also
// cannot carry `createNotification`, `updateNotification` or custom `actions` —
// the phone ignores them for local pins. The first two being impossible is a
// gift: a re-insert can never buzz the wrist, so re-sending the whole set is
// free of side effects.
//
// **Inserting an id that already exists updates that pin rather than adding a
// second one.** That is the entire duplicate-pin defence, and it is why the
// server's ids are deterministic and why this sends the full desired set rather
// than a diff. pkjs state is not durable — the watchface is killed every time
// the user glances at another app — so a diff would be a diff against a
// phantom. Replaying converges from any state, including "no idea". It is the
// same argument `docs/protocol.md` makes for `CalFlags.FLUSH`.

var ENDPOINT = 'https://us-central1-quest-journal-e0d4f.cloudfunctions.net';

var POLL_MS = 15 * 60 * 1000;
var REQUEST_TIMEOUT_MS = 20000;

var TOKEN_KEY = 'nuron.deviceToken';

function log(msg) { console.log('[nuron-pins] ' + msg); }

function token() {
  try { return localStorage.getItem(TOKEN_KEY); } catch (e) { return null; }
}

function setToken(t) {
  try { if (t) { localStorage.setItem(TOKEN_KEY, t); } else { localStorage.removeItem(TOKEN_KEY); } }
  catch (e) { log('could not persist token: ' + e); }
}

function deviceTimezone() {
  // The single most valuable thing this file reports. A floating trigger stores
  // a wall clock, not an instant, and the server has no device zone to resolve
  // it in -- without this it must skip those triggers rather than guess, and the
  // quest classifier makes floating the default for anything recurring.
  try { return Intl.DateTimeFormat().resolvedOptions().timeZone || null; }
  catch (e) { return null; }
}

function request(method, path, body, onDone) {
  var xhr = new XMLHttpRequest();
  xhr.open(method, ENDPOINT + path, true);
  xhr.timeout = REQUEST_TIMEOUT_MS;
  xhr.setRequestHeader('Authorization', 'Bearer ' + token());
  if (body) { xhr.setRequestHeader('Content-Type', 'application/json'); }
  xhr.onload = function () {
    if (xhr.status === 401 || xhr.status === 403) {
      // The pairing is gone. Drop the token rather than retrying every fifteen
      // minutes for ever; the config page is the only way back and the user has
      // to go there anyway.
      log('token rejected (' + xhr.status + '), unpairing');
      setToken(null);
      onDone(null);
      return;
    }
    if (xhr.status < 200 || xhr.status >= 300) { onDone(null); return; }
    try { onDone(JSON.parse(xhr.responseText)); }
    catch (e) { log('bad response: ' + e); onDone(null); }
  };
  xhr.onerror = function () { onDone(null); };
  xhr.ontimeout = function () { onDone(null); };
  xhr.send(body ? JSON.stringify(body) : null);
}

function applyPins(data) {
  var inserted = [];
  var failed = [];
  var i;

  for (i = 0; i < data.pins.length; i++) {
    var pin = data.pins[i];
    try {
      Pebble.insertTimelinePin(pin);
      inserted.push(pin.id);
    } catch (e) {
      // Recorded, not retried. The next poll re-sends the whole set anyway, and
      // a failure here must be reported honestly: the ack below is what the
      // phone's alarm receiver consults before silencing its own notification,
      // so overstating coverage would turn a failed insert into a missed
      // reminder rather than a duplicated one.
      log('insert failed for ' + pin.id + ': ' + e);
      failed.push(pin.id);
    }
  }

  // Tombstones: ids the server has retired -- completed, excluded, rescheduled,
  // or simply aged past the horizon. Nothing else will ever mention them again,
  // so without this a pin inserted last week would sit on the timeline for ever.
  var tombstones = data.tombstones || [];
  for (i = 0; i < tombstones.length; i++) {
    try { Pebble.deleteTimelinePin(tombstones[i]); }
    catch (e) { /* already gone, or never arrived. Either way: nothing to do. */ }
  }

  log('inserted ' + inserted.length + ', removed ' + tombstones.length +
      (failed.length ? ', FAILED ' + failed.length : ''));

  request('POST', '/pebbleAck', {
    // Claim only what actually landed. A partial failure reports zero coverage
    // server-side, which hands every buzz back to the phone until the next
    // clean sync -- the safe direction.
    coveredUntilMs: data.horizonEndMs,
    inserted: inserted,
    failed: failed
  }, function () {});
}

function sync() {
  if (!token()) { return; }
  request('GET', '/pebblePins', null, function (data) {
    if (!data || !data.pins) { return; }
    if (data.timezoneKnown === false) {
      // Not an error, but it IS why the timeline may be short: the server had no
      // zone to resolve floating quests in, so it skipped them rather than
      // guessing an hour. Re-pairing reports it.
      log('server has no timezone for this user; floating quests were skipped');
    }
    applyPins(data);
  });
}

function pair(code, onDone) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', ENDPOINT + '/pebblePair', true);
  xhr.timeout = REQUEST_TIMEOUT_MS;
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.onload = function () {
    var ok = false;
    try {
      var body = JSON.parse(xhr.responseText);
      if (xhr.status === 200 && body.token) { setToken(body.token); ok = true; }
    } catch (e) { /* fall through to ok = false */ }
    onDone(ok);
    if (ok) { sync(); }
  };
  xhr.onerror = function () { onDone(false); };
  xhr.ontimeout = function () { onDone(false); };
  xhr.send(JSON.stringify({
    code: code,
    timezone: deviceTimezone(),
    // An identifier, never a credential: any watchapp the user installs can read
    // it, so the server uses it only to recognise a second face belonging to an
    // already-paired user. The device token is the credential.
    accountToken: Pebble.getAccountToken(),
    platform: (Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo().platform) || null
  }));
}

module.exports = {
  /** Call from the watchface's `ready` handler. */
  start: function () {
    sync();
    setInterval(sync, POLL_MS);
  },

  /** Configuration page URL, for `showConfiguration`. */
  configUrl: function () {
    return 'https://quest-journal-e0d4f.web.app/pebble/pair' +
      '?account=' + encodeURIComponent(Pebble.getAccountToken()) +
      '&paired=' + (token() ? '1' : '0') +
      '&return=' + encodeURIComponent('pebblejs://close');
  },

  /** `webviewclosed` handler. The page returns a six-digit pairing code. */
  onConfigClosed: function (response) {
    if (!response) { return; }
    var decoded;
    try { decoded = JSON.parse(decodeURIComponent(response)); }
    catch (e) { log('unreadable config response'); return; }

    if (decoded.unpair) { setToken(null); log('unpaired'); return; }
    if (!decoded.code) { return; }

    pair(String(decoded.code), function (ok) {
      log(ok ? 'paired' : 'pairing failed');
    });
  }
};
