# Pebble watchface (`watchface-digital/`)

This directory is one of the two Pebble watchfaces in the **proto** monorepo — the
left-edge timeline, written in C using the Pebble SDK. The sibling `watchface-analog/`
holds the other one, and `pipe/` holds the Android companion that feeds both. See the
repo-root `README.md` and `docs/` for the system-level picture and the watch↔phone
protocol contract.

The two faces are independent apps with their own UUIDs, built and installed separately.
`events.{c,h}`, `wire.{c,h}`, `wbatt.{c,h}`, `tools/grab.py` and
`tools/send-demo-events.py` exist in each **by copy**, identical except for that face's
visible-window constants — a fix to the decoder or the battery estimate belongs in both.
`tools/make-menu-icon.py` is not a copy; it draws this face's launcher icon.

> **Run every `pebble` command from this directory** (`watchface-digital/`), not the repo
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
pebble send-app-message --emulator flint --vnc --int 10006=28 10000=600

# declare a 15 s cadence and then stay quiet: the companion-down alert appears in ~45 s.
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
band, and three entries at and past the horizon.

**The set no longer shows the same face on all three platforms, and it cannot.** The
window is four hours on the rectangles and six on `gabbro`, so the last three entries
(ids 9-11) are the round display's horizon cases — a band clipping at the end of the arc,
a marker just inside the horizon and one past it — and are simply three more things off
the end of the other two. They are `watchface-analog/`'s three verbatim, that face having
run a six-hour window all along, which leaves the two by-copy scripts differing in prose
alone.

Neither way seeds a band clipped at the *top* of the window, which on `gabbro` is the arc
end at twelve o'clock and the one place `graphics_fill_radial` is handed exactly
`TRIG_MAX_ANGLE`. It cannot be added without wrecking the cases above it — anything
clipped at `u = 0` is by definition running now, so it merges with id 1's band. Check it
by hand when the arc changes: `--clear`, then one appointment starting 90 minutes ago and
running 150.

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
- **`pebble install` does not switch which watchface is on screen.** With both faces
  installed the emulator keeps showing whichever one it was already showing, so an
  install that succeeds can leave you screenshotting the *other* face — and if that copy
  is stale it can come up as "… is not responding". Killing the emulator does not help;
  its app list is persisted. `pebble kill && pebble wipe` clears it, and the next install
  is then the only face there and comes up on screen.
- `pebble emu-set-time` does not move an already-running watchface.
- `pebble send-app-message` requires **numeric** key ids, not names.
- `pebble emu-bt-connection --connected no` tends to wedge the control channel; do it
  last, in a throwaway emulator.

## Project structure

```
watchface-digital/
  src/c/           C sources — see the module table below
  src/pkjs/        PebbleKit JS entry point (a deliberate no-op stub)
  tools/           Dev scripts — the demo calendar sender, the icon generator
  resources/       Fonts, and the launcher icon — the only image on the face
  package.json     Pebble app manifest (UUID, platforms, message keys, resources)
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

**One image resource, and it is the launcher icon.** Every glyph the *face* draws —
turn arrows, the phone silhouette, the battery, the marker triangles — is drawn from
primitives or a normalised point table, so it stays crisp at three sizes and costs
nothing from the resource budget. The icon in the watchface selector cannot be: the
firmware draws that list, not the app, so the shape has to exist as a bitmap before the
app runs. `tools/make-menu-icon.py` draws it from the same primitives anyway and writes
`resources/images/`, so the source of truth is still code — see *The launcher icon*
below.

**Every font is a system font, and there are no font resources at all.** Four roles
(`FONT_NUM`, `FONT_DATE`, `FONT_SLOT`, `FONT_TICK`), selected per platform in `theme.h`
by `PBL_PLATFORM_*`, each a `FONT_KEY_*` into the firmware. The clock is the LECO bold
numerals; everything else is Gothic, bold for the rows and regular for the hour lane.
Pebble fonts are fixed-pixel resources either way, so one set scaled by the SDK was
never an option — what the system set adds is that the pixels were fitted by hand at
each size instead of rasterised from a TTF at build time, which is where the strokes
were being lost.

Three consequences worth knowing:

- **There is no `characterRegex` to maintain.** Gothic carries the full charset, so a
  new glyph in a drawn string just draws. The clock's font is digits-and-colon only,
  which is the one subset left and is not something you can widen — put a letter in
  `s_time_buf` and it renders as a fallback box.
- **Nothing is loaded into the app's heap.** `fonts_get_system_font` returns a handle
  into the firmware, so there is nothing to unload and nothing that can fail to load.
- **The size number is the pixel size**, and `graphics_text_layout_get_content_size`
  returns exactly that as the height. It was an em under the TTF resources and the
  rendered height was anybody's guess.

`FONT_TICK` is not a row — it is the strip's hour numbers, and the measured width of
`"00"` in it *is* the label lane, so changing that size moves the text column.

### The launcher icon

The watchface selector draws a 25x25 icon beside each face's name — **anything larger
is rejected by the SDK outright**, not scaled. `tools/make-menu-icon.py` draws it and
writes both files into `resources/images/`; edit the script, re-run it, and rebuild.
Never hand-edit the PNGs.

There are two files because the selector runs an icon in a different mode per platform
(the modes are listed under *Menu Icon in the Launcher* in the SDK's
[Images](https://developer.repebble.com/guides/app-resources/images.md) guide):

| File | Platforms | Mode |
| --- | --- | --- |
| `menu_icon_bw.png` | `flint` | Inversion. Opaque black on white, `memoryFormat` `1Bit`. |
| `menu_icon_color.png` | `emery`, `gabbro` | Non-inverting, transparent, colour. |

- **`flint` only gets inversion mode if the resource declares `memoryFormat: "1Bit"`,**
  and only for watchfaces — apps get it either way. It is worth having: the icon flips
  polarity with the row it lands on, so one black-on-white shape is legible both on the
  white rows and on the black highlighted one. Transparency in that file would be read
  as white, so there is none.
- **Only one of the two entries may carry `"menuIcon": true`.** Flag both and the build
  dies with `Exception: More than one resource is set to be your menuIcon!` even though
  the two target disjoint platforms. Both entries share the resource *name*
  (`MENU_ICON`), and the flag travels with the name, so it is declared once on the
  `flint` entry and the colour entry inherits it.
- **The colour icon sits on an opaque plate, and that is not a stray background.** It
  never inverts and the firmware never re-tints it, so it has to bring its own contrast:
  measured on `emery`, the selector paints the highlighted row `#AA0055`, against which
  the rail, the hour notches and the red now rule all but disappear. The plate is
  `COL_BG`, which makes the icon a thumbnail of the face rather than a tile with
  something on it, and it costs nothing on the unhighlighted rows because those are
  white already. On `gabbro` this is the whole ballgame: that launcher draws the icon
  **only** for the highlighted row, so an icon that failed on magenta would never be
  seen at all.
- **The icon says what the face says, in the face's own vocabulary:** the rail down the
  left with its hour notches, a running band, a task as a blunt wedge, and now a quarter
  of the way down — a red rule where there is colour and a wedge reaching out at the
  rail on `flint`, the same split the face itself makes. It stays a straight rail at the
  quarter mark even though `gabbro`'s face is now an arc at 330°: the colour icon is one
  resource shared with `emery`, 25x25 leaves no room to say "half a turn" anyway, and
  what the icon carries is the vocabulary, not the shape.

`flint` and `emery` share `GOTHIC_14` for the hour lane, and on `flint` that is free:
`zone` there is closed by the wedge and not by the labels, so any lane up to about
sixteen pixels wide costs the content column nothing. Gothic 14's `"00"` measures twelve
against the fourteen of the Rajdhani `TICK_14` it replaced, so the lane got *narrower* —
free on `flint`, two pixels back into the content column on `emery`. `gabbro` keeps a
size of its own, `GOTHIC_18`, for the same reason it always did.

**The clock used to be the constraint on the whole layout, and it is not any more.**
`"00:00"` in Orbitron measured 3.55 em, which put `flint` within a point of a ceiling of
content-column-width ÷ 3.55. LECO measures it at 2.8, so the same column holds a clock
four points larger: measured, `flint` has 22 px of column spare at `LECO_32_BOLD_NUMBERS`
and `emery` 49 at `LECO_38_BOLD_NUMBERS`. The height the five rows share is the binding
constraint now, and it binds on `flint`.

The ceiling has not gone away, it has just stopped being close. Over the column width
`graphics_draw_text` still does not complain — it wraps the minutes onto a second line
or replaces them with an ellipsis — so it is still worth checking after a size change.
What *has* gone is the trap that made checking it hard: Orbitron's digits were not
tabular, a `1` being about half the width of a `0`, so `14:21` fit a box that `20:08`
overflowed and the current time was never the honest test. LECO's are tabular. Every
time of day is now the same width as every other.

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
  `ARC_SPAN_DEG` in `geometry.c`. It is half a turn, twelve o'clock round to six, and the
  span is no longer a tuning knob — it is the reading. Half a turn over six hours is
  thirty degrees to the hour, an analog clock's own hour spacing, so the graduations land
  where a reader already expects hours and the rule falls on 330°. What moves is the
  scale, not the rule, which is the whole difference from `watchface-analog/`: that face
  runs the same rate and pins each entry to the clock angle of *its own* time.

  This entry used to read "90° is tuned, not arbitrary — a wider span pushes the arc's
  ends rightward into the content column at exactly the clock's height and costs a whole
  font size", and that was **correct**. The bill is paid, not dodged, and the two entries
  below are the invoice. What changed is what the span buys: a borrowed hour spacing was
  worth more than the chord it costs.
- **The clock lines up with the pointer's *body*, not with the point of the track it
  marks — except on `gabbro`, where it lines up with neither.** See `layout_compute`.
  Identical on a rectangle, where the ray is horizontal; on an arc the ray runs down and
  to the right, so the mark's body sits some way below the arc point it touches, and a
  clock levelled with the apex reads as floating above it.

  On `gabbro` the rule is now 30° back from twelve o'clock, near the top of the glass,
  and `fit_row` leaves a row up there about 60 px of chord against the ~100 `"00:00"`
  measures in `LECO_36`. No font size recovers it: level with the rule the box would have
  to start where the usable chord is thinner than the clock is wide. So the clock drops
  to the highest row that will hold it — about 24 px below the rule — and hangs off the
  mark rather than sitting beside it. The floor is **solved**, from `fit_row`'s own width
  rule read backwards through `isqrt32`, so it follows a font change by itself the way
  `watchface-analog/`'s disc radius does. It falls straight *down*, not along the ray,
  and the difference matters when reading the code: `fit_row` centres every round row on
  `center.x + zone/2` at all heights, so `y` is the only free variable there.
- **`gabbro` drops the hour label nearest now, and so does `flint`, for the same reason
  by a different route.** See `draw_hour_label`. The label lane is a circle of its own,
  and over half a turn it curls across the top of the glass and into the clock's row; the
  rows are drawn after the strip and knock out their own footprint, so what survives the
  meeting is a sliver of a digit, which reads as damage rather than as a number. Dropped,
  it loses the same nothing `flint`'s does — the clock beside the gap is showing that
  very hour.

  Tested against `num_box` and `warn_box` only, **never** all five rows: the lane passes
  within a pixel of `date_box`'s left edge around two hours ahead, so a blanket test
  starts eating mid-window labels the moment `label_w` measures a pixel wider than
  expected. And the `warn_box` half is gated on `slots_warn_active()`, because that row
  is conditional and a row that draws nothing knocks nothing out — testing it
  unconditionally would delete the last hour of the window for the first twenty minutes
  of every hour to protect it from something usually not there.
- **The clock is centred on its ink, not on its content box, and the correction is
  measured rather than derived.** See `layout_compute`. A digits-and-colon subset never
  descends below the baseline, so the box has more slack above the glyphs than below and
  centring the box leaves them low — two pixels on `flint`. The TTF's own `hhea` metrics
  predict the opposite sign, because what the SDK lays out to is the generated
  resource's metrics, not the source font's.
- **Coverage is one byte per minute, not per pixel or per degree.** See `s_cov` in
  `strip.c`. A minute is under a pixel of track on all three displays, so quantising to
  one is free. 241 bytes on the rectangles — less than the dial's 360 — and 361 on
  `gabbro`, whose window is six hours; the array is sized from `STRIP_SPAN_MIN`, as is
  `s_hours`, which a literal six overran by one the moment the window grew.
- **A band is filled per *run* of equal-weight minutes, not stroked per minute.** See
  `draw_bands` in `strip.c` and `fill_track_band` in `geometry.c`. The per-minute version
  was the ring's per-degree technique carried over: 241 thick lines spaced under a pixel
  apart, overlapping into something solid. It worked, and it paid for the solidity with
  the shape of the ends — a thick Pebble line is capped with a semicircle and there is no
  cap style to turn that off, so the one sample at each end of a run had no neighbour to
  cover its cap and every band came off the display with a pixel bitten out of all four
  corners. Filling the span instead ends it square, and a span is what `s_cov` was already
  holding.
- **The shallower band is filled first.** Where a running band abuts an upcoming one the
  two runs can land on the same pixel row, consecutive minutes being under a pixel apart,
  and the deeper has to win it — the same rule the `max()` in `build_coverage` applies a
  minute at a time.
- **The colour displays' "now" rule is filled by `fill_track_band` too.** See
  `strip_draw_now`. It is the same kind of object as a band — a span of the track, filled
  to a depth — and stroked it had the same rounded ends, which on the one element the
  whole face is measured against read as a lozenge laid over the strip rather than a rule
  struck through it. Filling it also gives back what the caps were spending: `rule_len` is
  the deepest thing the strip draws and `margin` is the gap between that and the hour
  numbers, and the inner cap was taking two of those pixels on `emery` and all three on
  `gabbro`. Its width is still the stroke width it was tuned to, now as `w/2` of track
  either side of now.
- **Everything the strip fills reaches one pixel outboard of the track**, and that pixel
  used to be an accident: it was the stroke's outward cap. It lines the band's outer edge up with an
  hour notch's own cap and pushes it hard against the screen edge, so `BAND_OUT_PX` in
  `geometry.c` now asks for it. **Measure a band off a screenshot before trusting a depth
  constant here**; `depth` is a distance from the track and the drawn thickness is that
  plus the overshoot plus the track's own column.
- **A marker's base is drawn a second time, as a band.** See `draw_track_wedge` in
  `geometry.c`: it fills the polygon and then re-fills its outermost two pixels with
  `fill_track_band()`, the bands' own call. The polygon's base lands *on* the track and a
  band's outer edge does not — it sits `BAND_OUT_PX` outboard of it — and on top of that
  a gpath fill and a rect fill disagree about the pixel at the same coordinate. Measured
  on `emery`, the base came out two pixels inboard of the band under it, which reads as
  the marker floating inside the appointment rather than standing on it. On `gabbro` the
  base is also a chord of the arc, so its middle falls short by the sagitta as well.
  Arithmetic cannot close the last of it — two rasterisers at one coordinate do not agree
  by being asked to — so the base borrows the call that draws the edge it has to match,
  and lands on the same pixels on both display shapes by construction. `watchface-analog/`
  caps its ring wedges the same way and for the same reason.
- **`gabbro`'s arc angles are trig angles, not whole degrees.** See `arc_angle` in
  `geometry.c`. One degree of that arc is 2.2 px, so a band's square end and the notch it
  has to line up with cannot each round to a degree of their own. `track_at` and
  `fill_track_band` both go through the one function.
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
- **"Thicker" is a ratio to its neighbour, not a divisor of its own.** See `draw_notches`
  in `strip.c`. The two weights were `radius/24` and `radius/90`, which is to say 3/3/5
  against 1/1/1 — a minute notch was a single pixel on all three displays, so on `gabbro`
  it was a hairline on a 260 px screen and the pitch between the numbered notches stopped
  registering at all. The minute notch is a fortieth now (1/1/3), and the hour notch is
  floored at twice it, which lifts `gabbro`'s to 7 and leaves the other two where they
  were. Both floors matter: with the minute notch at 3 and the hour notch still 5, every
  graduation on `gabbro` looked the same and only the numbers said which were hours.
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
- **A point marker is a blunt wedge, not a triangle.** See `draw_track_wedge` in
  `geometry.c`. Depth is about three times the half-base on every display, so a triangle
  spends its inner third under two pixels tall — thinner than the minute notches the shape
  exists to stand out from — and the part that vanished was the apex, which is the end
  carrying the time. Measured on `emery`: a blob with a whisker. Stopping the taper at a
  `tip_half` costs no depth, so the ladder in `geometry.h` and everything `zone` is
  measured from stay where they were.
- **A merged marker is wider at the base as well as deeper and blunter.** Depth alone was
  a 25% difference — 12 px against 15 on `flint` — which is not a difference a reader can
  see without the single case beside it to compare against. Blunting alone was worse: a
  wedge with the taper taken out is a rectangle, and the group came off `flint` as a
  horizontal bar that stopped reading as a marker at all. Widening the base restores the
  taper at the larger size, so a group looks like more of the same thing rather than like
  a different thing.
- **The notches sit at a different depth in the stack on `flint`.** See `strip.c`. On the
  colour platforms they are drawn last — ink over the bands and the markers both, so the
  ruler the timeline is read off stays unbroken, and black over cerulean or amber costs
  nothing to see. `flint` draws the band and the notches in the same ink, so there the
  notches invert under a running band and stay *below* the markers; a cut through a solid
  marker would split the one shape that says "overdue".
- **An inverted notch is two segments, and the split is where the band stops.** See
  `notch_inverts` and `band_depth` in `strip.c`. Inverting the *whole* notch is what this
  did, and it deleted the ruler: a notch is longer than a running band is deep — two
  pixels longer on `flint` — so those two were being painted in the background colour on
  top of the background. Under a running appointment the strip came off the display as a
  black bar with white slots in it and no graduations anywhere, and a white slot where a
  notch cut looked exactly like the white gap where one band run ends and the next begins.
  The cut belongs only where there is band under it; past that the notch is ink like every
  other notch.
- **"Now" is a different *shape* on flint than on the colour displays,** and it is the
  only element on the face that is. See `strip_draw_now`. Where there is colour it is a
  red rule struck across the strip — over the bands, the notches and the markers alike,
  as deep as the strip's own elements go and no deeper — and a line *through* the ruler
  says "the ruler is here" without having to be learned. That only works in a hue nothing
  else on the strip uses, so on `flint` it would be a black line across a ruler made of
  black lines, i.e. a thicker notch, which is already what an hour notch is. `flint`
  therefore keeps the wedge.
- **flint's wedge sits inside the notch zone, not in it, and points the other way.** Its
  apex stops `POINTER_TIP_GAP` short of the zone's inner edge and the wedge reaches inward
  from there, so the strip stays the bands' and markers' alone. Markers reach *in* from
  the track; the wedge reaches *out* at it, which is what keeps the two from being read as
  one another. The colour displays do not need this distinction because a rule is not a
  wedge in the first place.
- **The hour numbers sit at a different depth on `flint` than on the colour displays.**
  See `label_x` in `geometry.c`. On colour they go past the rule and the markers with it,
  where nothing can ever be over or under them. On `flint` they go *between* the ruler and
  the wedge — three pixels off the notch ends, so they read as graduations of the ruler
  rather than as a column beside the text — and that is free, because the wedge was
  already claiming that area: `zone` on `flint` is what it was before the face had any
  numbers on it at all, and so is the clock's size.
- **Two different resolutions for one overlap, on `flint`, and the difference is
  z-order.** The numbers share their area with the markers and the wedge both. A marker
  the label *covers*: it knocks out its own footprint and draws over the top, exactly as
  the text rows do, because a marker drawn on top cut a background slot through the digit
  with its halo and the number came out unreadable while the marker lost nothing worth
  having — and a marker's meaning is in its position, not its middle. The wedge covers the
  *label*, being drawn last of everything so that nothing can hide it, and a plate cannot
  win against something drawn later — so the label gets out of the way instead. The pad on
  that plate is 2 px, not 0: cut to the box exactly, a marker's apex stopped one pixel
  short of the first digit and the two read as one shape.
- **The number nearest now is missing about two thirds of every hour on `flint`,** and
  that is the wedge's suppression rule. It is the one label on the strip whose value is
  also stated somewhere else: the clock is pinned to that wedge and is showing that very
  hour, a few pixels to the right of the gap. Dropping any other label would lose
  information; dropping this one loses a repetition.
- **An hour label is placed by the same `step_in` the markers use, and branches on
  nothing.** `label_x` is a depth along the ray, so the lane is vertical on a rectangle
  and curves with the arc on `gabbro` for free. Its vertical correction is `ts.h / 6`, not
  the clock's `ts.h / 20`: Gothic keeps more of its box above the caps than LECO does,
  and at this size that difference is the whole correction. Measured off a flint
  screenshot — in a 14 px `GOTHIC_14` box the digits ink rows 5 to 13, so all five pixels
  of slack are above them and half of that is the lift. At this size integer division is
  a design decision: the eighth the Rajdhani resource wanted truncates to one here and
  left the number a pixel below the notch it names.
- **A halo is a grown filled shape, never a wide stroked outline.** See `draw_tri` in
  `geometry.c`. A stroked path miters its corners, and the miter at a sharp vertex runs
  *far* past the vertex — enough to clear the pointer's tip gap and punch a
  background-coloured slot through an appointment band. Growing the vertices away from
  the centroid instead bounds the halo at any angle.
- **The now mark is drawn last, after the text.** See `proto.c`. The clock is pinned to
  it, so the two are adjacent by construction and the clock's background knockout was
  erasing the one element every marker is measured against.
- **The now mark is not the accent colour.** It shared `GColorVividCerulean` with the
  band and vanished whenever now fell inside a running appointment — which is most of the
  time it matters. It went to ink next, and to red on the colour displays when it became a
  rule, because a rule lies over cerulean, black and amber at once and red is the only
  entry in `theme.h` that is none of them. Red is therefore spent twice now — here and on
  the companion-down alert — where it used to be reserved for the alert alone.
- **Both bands use one hue.** `GColorCeleste` was too pale to see on white, and a second
  tint is redundant when depth and notch-inversion already say it.
- **The palette is split by role, and the split is a measured contrast ratio.** See
  `theme.h`. Against `COL_BG`, `GColorChromeYellow` is 1.9:1 and `GColorVividCerulean` is
  2.6:1 — which a ten-pixel band of solid fill survives and two glyphs of `"28%"` do not.
  An ink that draws **text** needs 4.5:1; an ink that draws a large solid **shape** can
  live near 2.5:1. So the clock went to `GColorCobaltBlue` (5.0:1) and the warnings row
  to `GColorWindsorTan` (4.1:1), while the bands and the point markers keep the brighter
  tints. The rejection of `GColorGreen`/`GColorYellow` at 1.4:1 and 1.1:1 was measured and
  right; it just stopped one step short of the pair it kept.
- **The clock is no longer the band's colour, and that is the same fix.** `COL_ACCENT`
  and `COL_BAND` were one macro apart and one hue identical, so the element the face
  exists to show shared an ink with the strip's decoration and the eye grouped them.
- **Text rows knock out their own footprint** before drawing. Cheap (the background is
  already that colour) and it is what stops a marker spiking through the countdown.
- **Point markers sit at their exact position and merge only when they would overlap.**
  See `build_points` in `strip.c`. The dial snapped every task to the nearest of 60
  notches, which quantised to twelve minutes; the strip has the resolution to place them
  properly, so the notches are a ruler rather than a bucket. Merging is decided by
  whether two bases would smear together, which needs the points sorted along the track —
  the event table has no order of its own, and this is the only place on the face that
  needs one.
- **The countdown's sign is the direction, and it is the only thing that carries it.**
  `+` while an appointment is running, counting since it began; `-` before anything
  starts, counting down to it. The reading used to be a progress bar under the digits,
  which drew only while something was running — so the countdown case was an *absence*,
  and an absence cannot be read. The sign states both cases on the line it belongs to,
  is the same shape on all three displays, and needs no colour, which is what `flint`
  has.
- **The sign is drawn at two heights, and the digits are a second draw because of it.**
  The `+` rides up onto the digits' cap line and the `-` drops onto their baseline
  (`SIGN_RISE`), so which way the count is running is legible from where the mark sits
  before its shape resolves — which is the order those two are read in at slot size.
  It costs nothing: three sixteenths of the text height is the gap the font leaves
  between a `+` and a cap, so neither position leaves the band the digits ink, the row
  keeps its height and `slot_knock_out()` still covers everything that draws. Re-measured
  when the rows became Gothic and it came out the same, which nothing else in this
  face's vertical arithmetic did — the hour label's correction went from an eighth to a
  sixth in the same change. The row
  is measured and placed as if the sign were always `+` — the wider of the two, and
  what `layout_compute()` budgets against — and the sign that draws goes left-aligned
  into that width, so nothing on the line moves at the minute the count turns over.
- **The countdown is a plain slot row now, and the seven pixels it gave back went to
  nav.** It used to reserve the bar's height whether or not the bar drew, because a row
  that changed height when the bar appeared would move the digits out from under the
  reader's eye. The clock is still pinned to the pointer and the warnings row to the
  bottom and the gaps above the date and the countdown were already getting the sizes
  they ask for — measured on `flint` and `emery`, `gap_n`/`gap_d` are unclamped both
  before and after — so the whole of the saving lands where this layout puts every
  slack: in the run between the countdown and the warnings, which nav centres itself in.
  The rest of what it bought is headroom: the clamp that drives those gaps down through
  zero into tucks has seven more pixels before it bites.
- **Every outlined glyph gets at least three pixels of stroke, and it used to get one.**
  See `glyph_stroke` in `slots.c`. A slot glyph is 15 px on `flint` and 23 on the other
  two, so the divisor of nine this was written with put `stroke_px()` on 1 for all
  three — a maneuver arrow whose shaft is a single pixel under a filled head reads as a
  blob with a whisker, and the phone silhouette reads as an empty box. Three, because
  `stroke_px()` yields only odd widths and there is nothing between a hairline and three.
- **The three-pixel floor applies from 14 px of glyph, and it used to be 16.** Gothic's
  box is two pixels shorter than the TTF it replaced at the same reading, which took
  `flint`'s slot glyph to 15 and dropped it back through the old floor to a hairline —
  the exact failure the floor exists to prevent, arriving by way of a font change rather
  than a divisor. Fourteen was measured, not assumed: at 15 px of glyph a 3 px stroke
  leaves the phone silhouette four pixels of interior and it still reads as an outline.
- **The battery and the calendar keep the hairline, and the other two do not.** Those two
  are mostly interior — a cell is eight pixels tall on `flint`, and it is the calendar's
  *open* lower half against its filled header that makes it a calendar — so a three-pixel
  stroke closes up the thing the shape is made of. The maneuvers and the phone are edges,
  and edges can carry it.
- **The maneuver head came down when the shaft went up.** `HEAD_LEN`/`HEAD_HALF` were
  tuned against a one-pixel shaft, and a head two thirds of the grid across swallowed a
  three-pixel one whole: the glyph came off `emery` as a solid lump with no direction in
  it. About three times the shaft's width is what reads as a head.
- **The nav row reserves no height, and that is what makes five rows fit.** Everything
  above it is flowed down from the pointer and the warnings row is pinned to the bottom,
  so nav lives in the slack between them and nothing moves whether it draws or not.
- **The warnings row is at the bottom, below nav.** The dial gave the companion-down
  alert the *first* slot on the face, on the grounds that it invalidates everything
  phone-fed. It is last now because that was asked for; red still marks it, so it is
  demoted in position rather than in salience.
- **The lane gaps are `margin`, not a literal 3.** See `layout_compute` and the note
  above `Layout` in `geometry.h`. `LABEL_GAP` was 3 on a 144 px display and 3 on a 260 px
  one, and so was the clearance closing `zone` on the inboard side — proportionally
  tighter exactly where there was most room. Measured on `emery`, `"10"` and the `T` of
  `"THU 27"` came out two pixels apart and the hour numbers read as the text column's
  first glyph rather than as graduations of the ruler. `margin` is 3/4/5, which on `flint`
  is the same 3 the constant was tuned against, so that display does not move.
- **The date is `%a %d`, not `%a %d %b`.** There is not room for the month at a readable
  size beside the strip on any of the three platforms — `gabbro`'s chord at the date's
  height is the tightest.
- **The rows are left-aligned on the rectangles and centred on `gabbro`.** See `ROW_ALIGN`
  in `geometry.h`. It is not a shape that was given up on: a chord's left edge moves 66 px
  between the clock's height and the warnings row's, so one shared left edge there is
  either a staircase or, if a single x is forced on all five rows, narrow enough at the
  clock's height to clip `"00:00"`. What the circle does give free is a shared *centre* —
  `fit_row`'s left and right are symmetric about `center.x + zone/2` at every height — so
  centred rows already line up into the column that left-aligning is after.
- **Row spacing is a signed gap, and it starts positive.** See `gap_n`/`gap_d` in
  `layout_compute`. It used to be negative on purpose: the rows overlapped by a tenth of
  the row above, because a content box is taller than the ink in it and the gaps read
  wider than they measure. True, and overdone — the ink slack hides a few pixels, not a
  whole row's tenth. The clamps drive the gaps back down through zero into a tuck under
  pressure, so a font set that does not fit still fails as rows abutting rather than as
  text crossing text. The gaps may not grow into the nav row's height, which is a
  different thing from nav reserving space: nothing below the countdown moves whether nav
  draws or not.

## Architecture notes

Single window, single layer, one update proc. Every repaint is
`layer_mark_dirty(s_root_layer)`.

**Nothing animates, and the strip "scrolls" anyway.** The only tick is `MINUTE_UNIT`.
Scrolling is not an animation but a consequence of the mapping: every position on the
track is `f(t - now)`, so recomputing it once a minute slides the whole ruler past a
pointer that is pinned a quarter of the way down a straight strip, or 30° back from
twelve o'clock on `gabbro`'s arc. The same tick advances the countdown
and retires whatever has aged out, because every marker's position, prominence and
existence is a function of `now`. The previous design's flash subsystem
and its `app_focus_service` subscription are both gone — there is nothing sub-minute
left to pause under a modal.

**Two persistent-storage keys, and the event table is one of them.** A watchface is not
a resident program — the firmware kills it whenever the user opens anything else and
starts it again when they come back — so key 1 holds `wbatt`'s drain anchor, which could
never accumulate a sample history in RAM, and key 2 holds the calendar table, which used
to come back empty. The companion re-flushes on its periodic tick, but that tick is the
slow tier's 600 s, so a glance at a notification cost up to ten minutes of a bare ruler.
An empty strip does not read as "waiting", it reads as "nothing scheduled". `events_save()`
runs in `deinit()` and `events_load()` in `init()`; entries carry absolute starts, so
`events_gc()` drops whatever expired while the app was gone on the first paint back.

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
