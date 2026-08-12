# Pebble watchface (`watchface/`)

This directory is the Pebble smartwatch component of the **proto** monorepo — a
watchface written in C using the Pebble SDK. The sibling `pipe/` directory holds the
Android companion. See the repo-root `README.md` and `docs/` for the system-level
picture and the watch↔phone protocol contract.

> **Run every `pebble` command from this directory** (`watchface/`), not the repo
> root. The SDK expects `package.json`, `wscript`, `src/` and `resources/` in the
> working directory.

## Supported platforms

The 2026 devices only:

| Platform | Device | Display | Notes |
| --- | --- | --- | --- |
| `flint` | Pebble 2 Duo | 144×168 rect | **Black and white.** 64 KB app RAM. |
| `emery` | Pebble Time 2 | 200×228 rect | Colour. 64 KB app RAM. |
| `gabbro` | Pebble Round 2 | 260×260 **round** | Colour, touch. 128 KB app RAM. |

`aplite`, `basalt`, `chalk` and `diorite` were dropped in the redesign.

**Check every visual change on `flint` and on a colour platform.** Most of the design's
prominence cues exist specifically so the single-ink display still works; a change that
reads beautifully on `gabbro` can be invisible on `flint`.

## Commands

```bash
# Build for all three platforms
pebble build

# Build with a synthetic calendar seeded in — see "Testing the strip" below
PROTO_DEMO=1 pebble build

# Clean build artifacts (required after adding a message key)
pebble clean

# Install on a specific emulator
pebble install --emulator flint

# Screenshot the running emulator
pebble screenshot --no-open screenshot.png

# ...or, when that hangs (it does in this environment), go straight to the framebuffer
tools/grab.py flint --scale 4
```

Everything except the calendar can be driven by numeric key id:

```bash
# turn right in 250 m, fast heartbeat tier
pebble send-app-message --emulator flint --vnc --int 10003=3 10004=2500 10005=0 10000=30

# phone battery at 28% (the watch's own threshold is 30)
pebble send-app-message --emulator flint --vnc --int 10006=28 10000=900

# declare a 15 s cadence and then stay quiet: the companion-down alert appears in ~38 s.
# The only practical way to see that state.
pebble send-app-message --emulator flint --vnc --int 10000=15
```

If you need more information on the `pebble` command or a sub-command, append
`--help`.

> **After adding a message key**, run `pebble clean` before `pebble build`.
> `message_keys.auto.h` is not regenerated incrementally, so the new `MESSAGE_KEY_*`
> symbol comes back undeclared.

### Testing the strip

Two ways in, both seeding the same set: a running band across the pointer, two
overlapping bands that must flatten into one, two point entries too close to draw apart
that must merge, an overdue reminder above the pointer, a marker sitting on top of a
band, and one entry past the three-hour horizon that must not draw at all.

```bash
# Over the wire, against an ordinary build — preferred
tools/send-demo-events.py                    # the running emulator, or start one
tools/send-demo-events.py --emulator flint   # a specific device codename
tools/send-demo-events.py --clear            # flush with no records: an empty six hours
tools/send-demo-events.py --remove 1 5       # delta, no flush
tools/send-demo-events.py --dry-run          # print the blob and what it says
tools/send-demo-events.py --list             # which emulators are up, and how

# Compiled in
PROTO_DEMO=1 pebble build
```

Prefer the script: it exercises the decode path in `wire.c` instead of bypassing it,
it re-seeds without a reinstall, and no screenshot ends up taken from a build that
could not ship. It needs `--bytes`, which pebble-tool grew in 5.0.39; `PROTO_DEMO`
predates that and is what remains for older tooling.

**`--emulator` takes a device codename** and is optional: with nothing given the
script sends to whichever emulator is already up, starts `flint` if none is, and
refuses to guess when several are running. Starting one installs the watchface, because
a freshly booted emulator is showing the stock face and would drop the message. That
also makes **an emulator running the wrong app the first thing to suspect when a send
appears to do nothing** — the script retries once through an install for exactly that
case. It works out `--vnc` for itself; see the gotcha below for why you should let it.

**The two are meant to show the same face** — extend `demo_seed()` in `src/c/proto.c`
and `DEMO` in the script together, and never ship a build with the flag set.

### Headless environments

Without a window server (headless Linux, Docker, CI) you must add `--vnc` to **all
commands that interact with the emulator** — installs, screenshots, button presses and
any `emu-*` command:

```bash
pebble install --emulator flint --vnc
pebble screenshot --vnc --no-open screenshot.png
pebble emu-button --emulator flint --vnc click select
```

`tools/send-demo-events.py` is the exception: it decides for itself, from the running
emulator's recorded mode or from `$DISPLAY`. Pass `--vnc` or `--no-vnc` only to override
it.

### Emulator gotchas in this environment

- **One emulator per platform at a time.** A wedged `qemu-pebble` keeps VNC display
  `:1` bound, and the next command fails with `Failed to find an available port`. Kill
  it before retrying — but bracket the pattern: `pkill -f 'qemu[-]pebble'`. Written
  plainly, `pkill -f qemu-pebble` matches the shell running it and kills itself (exit
  144) before reaching the rest of the command line.
- **A `--vnc` mismatch silently restarts the emulator you were looking at.** When the
  mode asked for differs from the mode a running emulator was started in, pebble-tool
  kills it and launches a replacement rather than failing, and a VNC launch also kills
  whatever else holds display `:1`. The replacement boots on the stock watchface, so the
  symptom is a send that reports success and changes nothing. `send-demo-events.py`
  reads the mode out of pebble-tool's own state file and matches it; prefer letting it,
  and use `--list` to see which mode each emulator is in.
- **An emulator can wedge while still looking alive.** Its `qemu` and `pypkjs` processes
  stay up and its port stays bound, but every command against it ends in
  `libpebble2.exceptions.TimeoutError`. Nothing recovers it — kill it and start again.
- **`pebble screenshot` hangs indefinitely in this environment**, on every platform and
  from a cold boot, with or without `--vnc`. `pebble install` over the same channel
  succeeds, and so does `send-app-message`, so it is the firmware's screenshot endpoint
  and not the connection. It is not caused by the app: a build of the previous commit
  fails identically. **Use `tools/grab.py` instead** — it asks QEMU's own monitor socket
  for `screendump`, which reads the framebuffer without involving the watch software at
  all, and it upscales for you. `pebble screenshot` also has no `--scale` flag in
  pebble-tool 5.0.39, which `grab.py` fixes on the way past.
- **A failed `pebble screenshot` appears to wedge the channel for AppMessages too.** A
  `send-demo-events.py` run after one still reports success and delivers nothing. If a
  seed does not show up, restart the emulator and send before screenshotting rather than
  after.
- **`pkill -f pypkjs` self-kills exactly like `pkill -f qemu-pebble` does** — bracket
  both: `pkill -f 'pypkj[s]'`.
- **Colour correction is on by default** — the screenshot is remapped through a
  display-emulation LUT, so `GColorChromeYellow` comes out peachy and `GColorYellow`
  near-white. Pass `--no-correction` when asserting on exact palette RGB.
- `pebble emu-set-time` does not move an already-running watchface.
- `pebble send-app-message` requires **numeric** key ids, not names.
- `pebble emu-bt-connection --connected no` tends to wedge the control channel; do it
  last, in a throwaway emulator.

## Project structure

```
watchface/
  src/c/           C sources — see the module table below
  src/pkjs/        PebbleKit JS entry point (a deliberate no-op stub)
  tools/           Dev scripts — the demo calendar sender
  resources/       Fonts (no images; every glyph is drawn in code)
  package.json     Pebble app manifest (UUID, platforms, message keys, fonts)
  wscript          waf build rules — also where PROTO_DEMO is wired in
  build/           Build output (generated; gitignored)
```

| Module | Owns |
| --- | --- |
| `proto.c` | Lifecycle, service handlers, paint order, the demo seed. |
| `geometry.{c,h}` | The track, the vertical layout, chord fitting. |
| `theme.h` | The whole palette and the three font choices. |
| `events.{c,h}` | The event table, the live window, linger rules, countdown choice. |
| `strip.{c,h}` | Bands, notches, markers, the pointer. |
| `slots.{c,h}` | The three conditional rows and every glyph. |
| `wire.{c,h}` | The AppMessage inbox and the two watchdogs. |
| `wbatt.{c,h}` | The watch's own hours-remaining estimate. |

There are **no image resources**. Every glyph — turn arrows, the phone silhouette, the
battery, the marker triangles — is drawn from primitives or a normalised point table,
so it stays crisp at three sizes and costs nothing from the resource budget.

Fonts are per-platform, three sizes each (`NUM_*`, `DATE_*`, `SLOT_*`), selected in
`theme.h` by `PBL_PLATFORM_*`. Pebble fonts are fixed-pixel resources, so one set
scaled by the SDK is not an option. The `characterRegex` subsets are aggressive —
**adding a glyph to any string means editing the regex** in `package.json`.

**The clock is the constraint on the whole layout, and it is a width constraint.**
`"00:00"` in Orbitron measures 3.55 em, so `NUM_*` may not exceed roughly the content
column's width divided by 3.55 — and the column is what is left after the strip claims
the left edge. Over that, `graphics_draw_text` does not complain: it wraps the minutes
onto a second line or replaces them with an ellipsis. Two traps when checking it:

- **Orbitron's digits are not tabular.** `1` is about half the width of `0`, so a clock
  reading `14:21` fits in a box that `20:08` overflows. Test the worst case, not the
  current time — temporarily `strcpy(s_time_buf, "00:00")` is the honest check.
- **The resource number is the em size, not the rendered width.** `NUM_28` puts about
  20px of ink height and 99px of `"00:00"` on the screen.

## Design decisions that look like bugs

Each of these was tried the other way first. The reasoning is in the source next to the
code; this is the index.

- **A left-edge strip is the old dial's nine-o'clock ray, and nothing about the helpers
  changed.** See `track_at` in `geometry.c`. At `a = TRIG_MAX_ANGLE * 3 / 4`, `step_in`
  moves `+x` — inward, toward the content column — and `step_side` moves `∓y` along the
  track, so bands, markers and the pointer are drawn by exactly the same primitives that
  drew them around a ring. One renderer covers a straight edge and `gabbro`'s arc.
- **There is no cosine correction any more, and that is not an oversight.** The dial
  needed `depth_along_ray` because a ray leaving a *rectangle* at an angle is not square
  to the edge it leaves through, so a fixed depth presented only cos(θ) of itself and a
  band measured 1.8× thinner at a corner. Both of the strip's shapes are square to their
  own boundary — a circle's ray is its normal, and so is a vertical edge's — so a depth
  in pixels is already perpendicular everywhere. Deleting it was the point of the shape,
  not a regression.
- **`gabbro`'s strip curves along the left arc; the other two are straight.** See
  `ARC_SPAN_DEG` in `geometry.c`. 90° is tuned, not arbitrary: the arc bulges to the
  bezel at nine o'clock and curls back in at both ends, so a wider span pushes those ends
  rightward into the content column at exactly the clock's height and costs a whole font
  size.
- **The clock lines up with the pointer's *body*, not with the point of the track it
  marks.** See `layout_compute`. Identical on a rectangle, where the ray is horizontal;
  on the arc the ray runs down and to the right, so the wedge sits about a dozen pixels
  below the arc point its apex touches, and a clock levelled with the apex reads as
  floating above it.
- **The clock is centred on its ink, not on its content box, and the correction is
  measured rather than derived.** See `layout_compute`. A digits-and-colon subset never
  descends below the baseline, so the box has more slack above the glyphs than below and
  centring the box leaves them low — two pixels on `flint`. The TTF's own `hhea` metrics
  predict the opposite sign, because what the SDK lays out to is the generated
  resource's metrics, not the source font's.
- **Coverage is one byte per minute, not per pixel or per degree.** See `s_cov` in
  `strip.c`. A minute is under a pixel of track on all three displays, so quantising to
  one is free, and 241 bytes is less than the dial's 360. Bands are then drawn one
  stroked line per covered minute, which is the same technique the ring used per degree —
  the samples are 0.7–0.9px apart under a 3px stroke, so they overlap into a solid band.
- **A band's asked-for depth and its drawn depth are two different numbers.** See
  `draw_bands` in `strip.c`. A thick line's caps run `stroke/2` past each endpoint, so
  asking for a depth drew a band that much deeper than the notch zone it was supposed to
  fill. The outward overshoot is wanted — it pushes the band hard against the screen
  edge — so only the inner end is shortened. **Measure a band off a screenshot before
  trusting any depth constant here**; the arithmetic in the source is not what reaches
  the display.
- **Every stroke width goes through `stroke_px()`, and comes back odd.** See
  `geometry.h`. The SDK supports odd widths only — an even one is stored as asked but
  the drawing routines round it down, so a requested 4 reaches the screen as 3 and
  anything derived from the 4 is describing a line that was never drawn. Odd is also the
  only width that sits square on the pixel grid, since only an odd count puts the same
  number of pixels either side of the centre line. Line *endpoints* are rounded to the
  nearest pixel for the same reason: `div_round()` in `geometry.c` replaces C's
  truncation, which biased every stepped point back toward where it started by up to a
  pixel.
- **Hour notches are thicker, not longer.** Length is already spoken for: it is what
  separates a notch from a band, which fills part of the same depth.
- **Notches sit on the wall clock's quarter hours, not at multiples of fifteen minutes
  from the top of the window.** That is what makes the strip scroll — every notch's
  position slides by the same fraction of a pitch each minute, and the stationary pointer
  is what that motion is read against.
- **An upcoming band is shallower, not lighter.** See `strip.c`. The first version
  hatched it — a thin line every other step, a textbook half-tone — which was completely
  wrong here, because the notches are *also* thin lines and the band read as nothing but
  a patch of extra ticks. Depth is a different shape; density is not.
- **Point markers are solid on every platform, including `flint`.** See `draw_markers` in
  `strip.c`. The dial drew `flint`'s upcoming marker hollow because folding every colour
  to black left hollow-against-solid as the only channel that could say "overdue". A
  linear track says it by position — overdue is *above* the pointer, always — which a
  twelve-hour ring could never do, every point on it being both past and future.
  Dropping it also fixed a real defect: measured on `flint`, a hollow wedge at this size
  is a 1px outline under a 3px background halo, and where it crossed a band it striped
  the two into noise.
- **The notches sit at a different depth in the stack on `flint`.** See `strip.c`. On the
  colour platforms they are drawn last — ink over the bands and the markers both, so the
  ruler the timeline is read off stays unbroken, and black over cerulean or amber costs
  nothing to see. `flint` draws the band and the notches in the same ink, so there the
  notches invert under a running band and stay *below* the markers; a cut through a solid
  marker would split the one shape that says "overdue".
- **The pointer sits inside the notch zone, not in it, and points the other way.** See
  `strip_draw_now`. Its apex stops `POINTER_TIP_GAP` short of the zone's inner edge and
  the wedge reaches inward from there, so the strip stays the bands' and markers' alone.
  Markers reach *in* from the track; the pointer reaches *out* at it, which is what keeps
  the two from being read as one another.
- **A halo is a grown filled shape, never a wide stroked outline.** See `draw_tri` in
  `geometry.c`. A stroked path miters its corners, and the miter at a sharp vertex runs
  *far* past the vertex — enough to clear the pointer's tip gap and punch a
  background-coloured slot through an appointment band. Growing the vertices away from
  the centroid instead bounds the halo at any angle.
- **The pointer is drawn last, after the text.** See `proto.c`. The clock is pinned to
  it, so the two are adjacent by construction and the clock's background knockout was
  erasing the one element every marker is measured against.
- **The pointer is ink, not the accent colour.** It shared `GColorVividCerulean` with the
  band and vanished whenever now fell inside a running appointment — which is most of the
  time it matters.
- **Both bands use one hue.** `GColorCeleste` was too pale to see on white, and a second
  tint is redundant when depth and notch-inversion already say it.
- **Text rows knock out their own footprint** before drawing. Cheap (the background is
  already that colour) and it is what stops a marker spiking through the countdown.
- **Point markers sit at their exact position and merge only when they would overlap.**
  See `build_points` in `strip.c`. The dial snapped every task to the nearest of 60
  notches, which quantised to twelve minutes; the strip has the resolution to place them
  properly, so the notches are a ruler rather than a bucket. Merging is decided by
  whether two bases would smear together, which needs the points sorted along the track —
  the event table has no order of its own, and this is the only place on the face that
  needs one.
- **The countdown reserves the progress bar's height whether or not it draws it.** The
  bar only appears while an appointment is running, and a row that changed height when it
  appeared would move the digits out from under the reader's eye at the one moment they
  are watching them.
- **Counting down draws no bar at all.** A bar is a thing that fills up, so its presence
  already says the number is climbing; a second empty bar under a countdown would invite
  reading it as a countdown bar running the other way. Its absence is the distinction.
- **The nav row reserves no height, and that is what makes five rows fit.** Everything
  above it is flowed down from the pointer and the warnings row is pinned to the bottom,
  so nav lives in the slack between them and nothing moves whether it draws or not.
- **The warnings row is at the bottom, below nav.** The dial gave the companion-down
  alert the *first* slot on the face, on the grounds that it invalidates everything
  phone-fed. It is last now because that was asked for; red still marks it, so it is
  demoted in position rather than in salience.
- **The date is `%a %d`, not `%a %d %b`.** There is not room for the month at a readable
  size beside the strip on any of the three platforms — `gabbro`'s chord at the date's
  height is the tightest.

## Architecture notes

Single window, single layer, one update proc. Every repaint is
`layer_mark_dirty(s_root_layer)`.

**Nothing animates, and the strip "scrolls" anyway.** The only tick is `MINUTE_UNIT`.
Scrolling is not an animation but a consequence of the mapping: every position on the
track is `f(t - now)`, so recomputing it once a minute slides the whole ruler past a
pointer that is pinned to a quarter of the way down. The same tick advances the countdown
and retires whatever has aged out, because every marker's position, prominence and
existence is a function of `now`. The previous design's flash subsystem
and its `app_focus_service` subscription are both gone — there is nothing sub-minute
left to pause under a modal.

**Two `AppTimer`s remain**, both in `wire.c`: the companion liveness watchdog and the
nav-slot expiry. Both follow the same single-owner pattern — one `*_sync()` function
decides whether the timer should be running and nothing else touches the handle — and
both traps still apply: `app_timer_cancel` invalidates the handle, and an elapsed timer
must never be cancelled, which is why each callback nulls the handle as its first
statement.

## SDK documentation

The full Pebble SDK documentation is at <https://developer.repebble.com>.

An index of every page is at <https://developer.repebble.com/llms.txt>. Every page also
has a Markdown version: append `.md` to any documentation URL to fetch plain Markdown
instead of HTML (e.g.
`https://developer.repebble.com/guides/events-and-services/buttons.md`). Prefer the
`.md` form when reading docs.

Key sections: App Resources (images, fonts, vector graphics, 256-resource limit); User
Interfaces (layers, round vs rectangular displays); Events & Services; Communication
(AppMessage, PebbleKit); Graphics & Animations; Debugging; Best Practices
(multi-platform support, battery conservation).

## Emulator button control

```bash
pebble emu-button click select                 # press then release
pebble emu-button click back --duration 2000   # long press
pebble emu-button click down --repeat 5        # repeat
```

Buttons: `back`, `up`, `select`, `down`. Actions: `click`, `push`, `release`. Note that
a **watchface** receives no button input — the system owns all four — so these are only
useful for navigating away from it.

## AI interaction guidelines

- When given an image of a watchface to replicate, describe the target in precise
  detail first. Note every visual element present, as well as size, alignment, font
  weight, spacing and location.
- After a visual change, screenshot it, upscale it, and look at it. Then ask whether
  anything collides, anything is invisible on `flint`, and anything reads as a different
  element than intended. Several of the entries under *Design decisions* above were
  found exactly that way and would not have been found by reading the code.
