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

# Build with a synthetic calendar seeded in — see "Testing the dial" below
PROTO_DEMO=1 pebble build

# Clean build artifacts (required after adding a message key)
pebble clean

# Install on a specific emulator
pebble install --emulator flint

# Screenshot the running emulator
pebble screenshot --no-open screenshot.png
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

### Testing the dial

Two ways in, both seeding the same set: a running band, two overlapping bands that must
flatten into one, clustered point entries that must merge, an overdue reminder, and a
marker sitting on top of a band.

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
- `pebble screenshot` has **no `--scale` flag** in pebble-tool 5.0.39. To inspect
  detail, upscale the PNG afterwards with nearest-neighbour.
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
| `geometry.{c,h}` | Dial trigonometry, the vertical layout, chord fitting. |
| `theme.h` | The whole palette and the three font choices. |
| `events.{c,h}` | The event table, the live window, linger rules, bottom-slot choice. |
| `dial.{c,h}` | Bands, notches, markers, the hour index. |
| `slots.{c,h}` | Both slots and every glyph. |
| `wire.{c,h}` | The AppMessage inbox and the two watchdogs. |
| `wbatt.{c,h}` | The watch's own hours-remaining estimate. |

There are **no image resources**. Every glyph — turn arrows, the phone silhouette, the
battery, the marker triangles — is drawn from primitives or a normalised point table,
so it stays crisp at three sizes and costs nothing from the resource budget.

Fonts are per-platform, three sizes each (`NUM_*`, `DATE_*`, `SLOT_*`), selected in
`theme.h` by `PBL_PLATFORM_*`. Pebble fonts are fixed-pixel resources, so one set
scaled by the SDK is not an option. The `characterRegex` subsets are aggressive —
**adding a glyph to any string means editing the regex** in `package.json`.

## Design decisions that look like bugs

Each of these was tried the other way first. The reasoning is in the source next to the
code; this is the index.

- **The dial traces the rectangular perimeter on `flint` and `emery`**, so it hugs the
  screen on every platform. See `geometry.c`. Equal spans of time therefore cover
  unequal arc lengths there — a corner is 1.5× further out than an edge midpoint — and
  that distortion is accepted deliberately, because the angle is what encodes the time
  and the angle is exact everywhere.
- **Marker depths are pixel counts perpendicular to the boundary, never fractions of the
  distance to it.** Scaling a depth by the radius would look like a uniform ring and be
  wrong. But a fixed count *along the ray* is wrong too on a rectangle, and that is the
  subtle one: the ray stops being square to the edge as it moves off an edge midpoint,
  so a constant radial depth only presents cos(θ) of itself across the edge and a band
  measured 1.8× thinner at a corner than at three o'clock. `depth_along_ray` in
  `geometry.c` divides by that cosine, which is why the ray distance grows toward a
  corner. **Every depth taken from the boundary goes through it** — bands, notches,
  markers, the hour index's tip clearance — because correcting one and not the others
  breaks their relationships: an uncorrected notch beside a corrected band leaves the
  band poking out past the notch inner-ends.
- **A band's asked-for depth and its drawn depth are two different numbers.** See
  `draw_bands` in `dial.c`. Bands are thick radial lines, and a thick line's caps run
  `stroke/2` past each endpoint — so asking for `tick_len` drew a band deeper than the
  notch zone it was supposed to fill, and deeper than the notches crossing it. The line
  is shortened by exactly that at the inner end. **Measure a band off a screenshot
  before trusting any depth constant here**; the arithmetic in the source is not what
  reaches the display.
- **Every stroke width goes through `stroke_px()`, and comes back odd.** See
  `geometry.h`. The SDK supports odd widths only — an even one is stored as asked but
  the drawing routines round it down, so a requested 4 reaches the screen as 3 and
  anything derived from the 4 is describing a line that was never drawn. Odd is also the
  only width that sits square on the pixel grid, since only an odd count puts the same
  number of pixels either side of the centre line; that is what keeps the four quarter
  notches — the only exactly vertical and horizontal lines on the face — centred on
  their own rays. Line *endpoints* are rounded to the nearest pixel for the same reason:
  `div_round()` in `geometry.c` replaces C's truncation, which biased every stepped
  point back toward where it started by up to a pixel.
- **An upcoming band is shallower, not lighter.** See `dial.c`. The first version
  hatched it — a 1px radial line every other degree, a textbook half-tone — which was
  completely wrong here, because the notches are *also* 1px radial lines and the band
  read as nothing but a patch of extra ticks. Depth is a different shape; density is not.
- **An upcoming point marker is hollow on `flint` and solid everywhere else.** See
  `dial.c`. On the colour platforms amber-against-orange already says upcoming against
  overdue, so both wedges are filled and fill is left to say "point in time, not a
  span". `flint` folds both to black, which leaves hollow-against-solid as the only
  urgency channel a marker has.
- **The notch ring sits at a different depth in the stack on `flint`.** See `dial.c`.
  On the colour platforms it is drawn last — ink over the bands and the markers both,
  so the grid the timeline is read off stays unbroken, and neither marker halos nor
  inverted notches are needed to separate amber from cerulean. `flint` draws the band
  and the notches in the same ink, so there the notches invert under a running band and
  stay *below* the markers; a cut through a solid marker would split the one shape that
  says "overdue", and the halo is what keeps a marker off a band it shares an ink with.
- **The hour index sits inside the ring, not in it.** See `dial.c`. Its tip stops 3px
  short of the notch zone's inner edge and the wedge reaches inward from there, so the
  ring stays the bands' and markers' alone. Long enough to cross whichever slot it is
  nearest at twelve and six o'clock — the halo is what keeps both legible.
- **A halo is a grown filled shape, never a wide stroked outline.** See `draw_tri` in
  `geometry.c`. A stroked path miters its corners, and the miter at a sharp vertex runs
  *far* past the vertex — at the hour index's 42° tip a 4px stroke overshoots by more
  than five pixels, which is enough to clear the tip's 3px gap and punch a
  background-coloured slot through an appointment band. Growing the vertices away from
  the centroid instead bounds the halo at any angle. Hollow shapes still need the
  outline form, and keep it.
- **The hour index is drawn last, after the text.** See `dial.c`. Both slots are
  centred, which puts them at twelve and six o'clock, and their background knockout was
  erasing the one element every marker is measured against.
- **The hour index is ink, not the accent colour.** It shared `GColorVividCerulean` with
  the band and vanished whenever the current hour fell inside a running appointment.
- **Both bands use one hue.** `GColorCeleste` was too pale to see on white, and a second
  tint is redundant when depth and notch-inversion already say it.
- **Text rows knock out their own footprint** before drawing. Cheap (the background is
  already that colour) and it is what stops a marker spiking through the countdown.
- **Point markers merge across neighbouring notches**, not just within one. A marker's
  base is wider than the 6° notch pitch on all three displays, so two entries six
  minutes apart — which straddle a notch boundary about half the time — would otherwise
  smear together.
- **The date is `%a %d`, not `%a %d %b`.** Inside the notch ring there is not room for
  the month at a readable size on any of the three platforms — `gabbro`'s chord at the
  date's height is the tightest, but even `flint`'s full width minus the ring is a few
  pixels short.

## Architecture notes

Single window, single layer, one update proc. Every repaint is
`layer_mark_dirty(s_root_layer)`.

**Nothing animates.** The only tick is `MINUTE_UNIT`, and it is also what advances both
countdowns and retires whatever has aged out, because every marker's position,
prominence and existence is a function of `now`. The previous design's flash subsystem
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
