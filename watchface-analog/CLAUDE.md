# Pebble watchface (`watchface-analog/`)

This directory is one of the two Pebble watchfaces in the **proto** monorepo — a
circular analog face written in C using the Pebble SDK. The sibling
`watchface-digital/` holds the other one, and `pipe/` holds the Android companion
that feeds both. See the repo-root `README.md` and `docs/` for the system-level
picture and the watch<->phone protocol contract.

> **Run every `pebble` command from this directory** (`watchface-analog/`), not the
> repo root. The SDK expects `package.json`, `wscript`, `src/` and `resources/` in
> the working directory.

The two faces are independent apps with their own UUIDs, built and installed
separately. They share the protocol and nothing else — not a build, not a resource,
not a header. What they *do* share is the vocabulary: an appointment is a band, a
task is a blunt wedge, a running thing is twice the depth of an upcoming one, and
red means now. See [../docs/architecture.md](../docs/architecture.md).

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

# Build with a synthetic calendar seeded in - see "Testing the ring" below
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

### Testing the ring

Two ways in, both seeding the same set: a running appointment with the hour hand
inside it, two overlapping bands that must flatten into one, two point entries too
close to draw apart that must merge, an overdue reminder behind the hour hand, a
marker sitting on top of a band, an appointment running past the five-hour horizon
that must clip at the end cap, one entry just inside that horizon, and one past it
that must not draw at all.

```bash
# Over the wire, against an ordinary build - preferred
tools/send-demo-events.py                    # the running emulator, or start one
tools/send-demo-events.py --emulator flint   # a specific device codename
tools/send-demo-events.py --clear            # flush with no records: an empty six hours
tools/send-demo-events.py --remove 1 5       # delta, no flush
tools/send-demo-events.py --dry-run          # print the blob and what it says
tools/send-demo-events.py --list             # which emulators are up, and how

# Compiled in
PROTO_DEMO=1 pebble build
```

The script exercises the decode path in `wire.c`; `PROTO_DEMO` bypasses it. **The two
are meant to show the same face** - extend `demo_seed()` in `src/c/proto.c` and `DEMO`
in the script together, and never ship a build with the flag set.

`--emulator` is optional: with no target the script sends to whichever emulator is
already up, starts `flint` if none is, and refuses to guess when several are running.
It retries once through an install, because a rebooted emulator comes back on the
stock watchface and a message addressed to a UUID nothing is running just NACKs — an
emulator running the wrong app is the first thing to suspect when a send appears to do
nothing. **With two faces installed that is now the ordinary mistake**: check which one
is on screen before blaming the wire.

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
watchface-analog/
  package.json          App metadata, UUID, message keys, the launcher icon
  wscript               Pebble SDK build script (waf)
  src/c/
    proto.c             Lifecycle, service handlers, paint order, the demo seed
    dial.{c,h}          The ring, the markers, the ticks and the hands
    geometry.{c,h}      The radial ladder, the angle mapping, the disc's rows
    theme.h             The palette and the two font choices
    events.{c,h}        The event table, the visible window, the countdown pick
    slots.{c,h}         The countdown, the maneuver and the warning
    wire.{c,h}          The AppMessage inbox and the two watchdogs
    wbatt.{c,h}         The watch's own hours-remaining estimate
  src/pkjs/index.js     PebbleKit JS stub (deliberately does nothing)
  resources/images/     The launcher icon, the only image on the face
  tools/                grab.py, send-demo-events.py, make-menu-icon.py
```

`events.{c,h}`, `wire.{c,h}`, `wbatt.{c,h}`, `tools/grab.py` and
`tools/send-demo-events.py` are shared with `watchface-digital/` by **copy, not by
reference** (`tools/make-menu-icon.py` is not — it draws this face's own icon). They
carry no rendering knowledge, so the copies stay identical except for the window
constants — this face's
`RING_BACK_S`/`RING_AHEAD_S` are 1 h and 5 h against the digital face's 1 h and 3 h.
A fix to the decoder or the battery estimate belongs in both.

**One image resource, and it is the launcher icon.** Every glyph the *face* draws is
drawn from primitives or a normalised point table, so it stays crisp at three sizes and
costs nothing from the resource budget. The icon in the watchface selector cannot be:
the firmware draws that list, not the app, so the shape has to exist as a bitmap before
the app runs. `tools/make-menu-icon.py` draws it from the same primitives anyway and
writes `resources/images/`, so the source of truth is still code - see *The launcher
icon* below.

**Two font roles, not the digital face's four, and both are system fonts.** There is no
clock numeral here - the hands are the clock - so the digital face's LECO ladder is not
needed, and with it the constraint that sizes every other row over there. The dial
carries ticks rather than numerals, so there is no hour-label lane either. What is left
is `FONT_DATE` for the date line and `FONT_SLOT` for everything else, both `GOTHIC_*_BOLD`
and both chosen per platform in `theme.h`.

There are **no font resources** and so no `characterRegex` to keep in step with the
drawn strings - that warning is gone from this face. Nothing is loaded into the app's
heap either: `fonts_get_system_font` hands back a pointer into the firmware.

This face felt the old TTFs harder than the digital one did. Every string here is
between 14 and 28 px, which is exactly the range in which the SDK's build-time
rasteriser has to round a condensed semibold stem to one pixel or two - and it rounded
down. There is no large text on this face to escape it, which is why the thin strokes
read as an analog problem.

Rajdhani being condensed *was* load-bearing, and what pays for losing it is that Gothic
is shorter as well as wider for the same reading: `"MON 22"` measures 50x18 in
`GOTHIC_18_BOLD` against 62x20 in the `DATE_20` it replaced, so the row is narrower
*and* shorter and `layout_compute()` solves a **smaller** disc, not a larger one.

**flint gives both rows the same size where the other two keep the date a rung above the
slot.** That display is the one where the disc's radius is dearest per row - it is solved
from these two rows, drawn over the hands, and the hour hand is 56 px long to begin with.
Measured: a 24 px date puts the disc at 37 and leaves 19 px of hour hand outside it,
where 18 px puts it at 34 and leaves 22.

### The launcher icon

The watchface selector draws a 25x25 icon beside each face's name - **anything larger
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
  and only for watchfaces - apps get it either way. It is worth having: the icon flips
  polarity with the row it lands on, so one black-on-white shape is legible both on the
  white rows and on the black highlighted one. Transparency in that file would be read
  as white, so there is none.
- **Only one of the two entries may carry `"menuIcon": true`.** Flag both and the build
  dies with `Exception: More than one resource is set to be your menuIcon!` even though
  the two target disjoint platforms. Both entries share the resource *name*
  (`MENU_ICON`), and the flag travels with the name, so it is declared once on the
  `flint` entry and the colour entry inherits it.
- **The colour icon sits on an opaque disc, and that is not a stray background.** It
  never inverts and the firmware never re-tints it, so it has to bring its own contrast:
  measured on `emery`, the selector paints the highlighted row `#AA0055`, against which
  the dial and both hands all but disappear. The disc is `COL_BG`, which makes the icon
  a thumbnail of the face rather than a tile with something on it - the plate being a
  disc and not a rounded rectangle is this face's own, because here the plate *is* the
  dial. It costs nothing on the unhighlighted rows, which are white already. On
  `gabbro` this is the whole ballgame: that launcher draws the icon **only** for the
  highlighted row, so an icon that failed on magenta would never be seen at all.
- **The icon carries one band on the rim and no rail under it.** On the face the rail
  spans half the ring and the band sits on it a quarter deeper, which is the grammar the
  whole ring is read by. At 25 px across, a 2 px rail under a 3 px band is one shape with
  it - a quarter of the ring's depth is not a difference this many pixels can hold - so
  the icon states the band alone and lets the rim be the scale. The band sits in the
  lower left because the hands are read first and must not be crowded, which is also why
  the pose is a quarter past twelve: two hands at a right angle read as hands, and the
  symmetric ten-past-ten V reads as a chevron at this size.
- **There is no central disc on the icon, though the face is built around one.** At this
  scale a disc wide enough to break the hands covers most of their length, and what is
  left of a 5 px hour hand under it is a stub. The pivot is a two-pixel dot instead: it
  is what says the two shapes radiate from a centre rather than meeting in a V.

## Design decisions that look like bugs

Each of these was tried the other way first.

- **The markers do not move.** On the digital face the ruler slides past a pinned now;
  here a marker sits at the clock angle of its own time and stays there, and it is the
  *window* that sweeps. Both are the same `f(t - now)` read from opposite ends. The
  payoff is that a three o'clock meeting is at the 3.
- **There is no "now" mark.** Six hours is half a turn of a twelve-hour dial, so the
  ring runs at thirty degrees to the hour — the hour hand's own rate — and the hand
  therefore points at the present position on the timeline by construction. Adding a
  mark would be adding a second one. This is also why this face has no element whose
  *shape* differs by platform: the digital face needed one only because its now mark
  had to be something other than a notch.
- **The ring is not graduated.** The twelve dial ticks are its scale as well as the
  clock's. Graduating it separately would put two scales in two concentric lanes on a
  display with one ink.
- **Half the ring is empty, and that is not "nothing scheduled".** The rail spans the
  window and stops, with a radial end cap at each end. Past the cap the ring is not
  free — it is not being shown.
- **An empty window takes the rail with it, and the face is then a plain clock.** When
  nothing in the table falls inside the six hours, the rail and its caps do not draw at
  all. The rail is the scale the markers are read against, and a scale with nothing on
  it states that the next six hours are clear — which is a claim the watch cannot make,
  because the ring is equally bare when the companion has never spoken and only the
  bottom row tells those two apart. Drawing it anyway makes an unanswered question look
  answered. Nothing else moves: `layout_compute()` never sees event state, so the disc,
  the tick lane and both hands sit exactly where they did. This is the one place the two
  faces part company on an empty table — the digital ruler is permanent, because it also
  carries the hour numbers and the now mark, and this one carries neither.
- **The hands break at the disc and resume past it.** The disc is drawn last of the
  dial, over both hands. It covers the half of each hand nearest the pivot, which
  carries no reading, and it is the only place on the face where text is safe from a
  hand at every minute of the day. This inverts the digital face's rule that nothing
  may cover the now mark — here the now mark is a hand, and the text outranks it.
- **The hour hand ends level with the ticks' outer end; the minute hand crosses the
  ring.** It reached the ring's mid-depth first, so that a running appointment had the
  hand's tip physically inside its band — a true reading that cost more than it was
  worth. A broad hand lying in the marker lane competes with the markers for the one
  lane they have, and on `flint`, where both are the same ink, the tip and the band it
  sat in read as one shape. It still points *at* the band, which is all the reading ever
  needed. Where it stops is the tick lane's own outer end — `tick_out`, not a clearance
  measured off the ring — because the ticks are the scale the hand is read against, and
  a tip sharing an edge with a graduation is what an hour hand does on any analog dial.
  It also leaves the whole marker clearance between the tip and the deepest a wedge can
  reach, where three pixels off `r_in` put the tip inside that clearance. Measured on
  `flint`: 56 px against the ticks' 56, where it was 60.
  Convention holds — the minute hand is still the longer — and the two are told apart by
  width, which is what `flint` has instead of hue.
- **The hands taper to three quarters, not to a half.** The disc hides everything inside
  its own radius, which on `flint` is half the hand, so a half-width tip had already
  narrowed the hour hand to about two pixels by the time it emerged from under the rim —
  spending the one distinction that display has on the part nobody can see.
- **A text row reserves seven eighths of what it measures, and draws lifted.** A Pebble
  font box carries roughly a third of its height as ascent slack above the ink, which is
  invisible in one row and compounds into a disc a size larger than its contents once
  three stack. `ROW_H`/`ROW_LIFT` in `geometry.h` reclaim it; the lift is a quarter,
  measured against Gothic, where the Rajdhani resources it replaced wanted three
  sixteenths. A system Gothic sets its baseline on the box's last row and keeps *all*
  of the slack above the ink, so with nothing below to share the correction the lift is
  simply half the slack. `SIGN_RISE` is still three sixteenths and the two no longer
  agree, which they did only by coincidence before: they measure different slacks.
- **Both hands carry a background halo.** On flint a black hand over a black band is
  otherwise one shape. The halo is a grown filled polygon, never a wide stroked outline:
  a stroked path miters its corners, and the miter at a sharp vertex overshoots far
  enough to cut a background-coloured slot clean through a band.
- **Each hand is a filled polygon *and* a stroked line down its own spine.** The polygon
  alone does not survive being three pixels wide: `gpath_draw_filled` rasterises by
  scanline and a slanted sliver loses whole scanlines to it. Measured on flint, sweeping
  all sixty minute positions — the minute hand drew dotted between 102° and 150° and
  **did not draw at all** between 156° and 174°, sixteen minutes of every hour with no
  minute hand and the same sixteen again half a turn later. The defect is a property of
  the *slope*, not the direction, which is why it came in pairs 180° apart and why noon
  and six o'clock always looked right. Widening the hand is not the fix — at five pixels
  it still broke at 156°, and the two hands on flint are told apart by width alone.
  The line is a different rasteriser and is continuous at every angle; it is stroked at
  the *tip's* width so it can never be wider than the polygon and the taper survives,
  and it stops a tip-width short because a thick Pebble line is capped with a semicircle
  and there is no cap style to turn that off.
- **A band stops at the rail; a task crosses it.** Depth is the grammar — a running
  appointment fills the ring, an upcoming one its outer half — and the poke past the
  rail is what makes a point in time a different kind of thing from a span rather than
  a short one.
- **A wedge's base is drawn a second time, as an annular sector.** `ring_wedge()` fills
  the polygon and then re-fills its outermost two pixels with `fill_ring_band()` — the
  bands' own call, at the same radius. A polygon's base is a straight chord between two
  points *on* the circle, so only its corners reach the rim and the middle falls short
  by the sagitta; and `graphics_fill_radial` feathers its outer edge where `gpath`'s
  fill has no antialiasing to feather with. Neither is a whole pixel, and together they
  were enough to make the cone read as floating a pixel inside the band it sits on.
  Arithmetic cannot close it, the two edges coming off two different rasterisers, so
  the base borrows the edge it has to match — the same trick `draw_ring_arc()` uses to
  line the rail up with the inner edge of the bands sitting on it. `watchface-digital/`
  caps its track wedges the same way; there the offset is a whole two pixels, its bands
  reaching `BAND_OUT_PX` outboard of the track as well.
- **A merged marker is wider and blunter as well as deeper.** Depth alone is a quarter
  of a difference, which is not one a reader can see without the single case beside it;
  blunting alone takes the taper out, and a wedge with no taper is a rectangle. The
  clearance between the deepest a marker reaches and the tick lane is five percent of
  the radius rather than three purely so the deeper one has somewhere to be deeper into.
- **The coverage array is one byte per minute, not per degree.** A minute is 0.58 px on
  flint and 2.1 on gabbro, so a per-degree bucket would fold two minutes together and
  cost gabbro a pixel of placement. Six hours is 360 minutes over 180 degrees — the
  tempting one-to-one is the wrong way round.
- **Every angle on the ring is a base plus a span, never a fresh absolute.** The window
  is half a turn and rides with the hour hand, so it straddles twelve o'clock for six
  hours out of every twelve. Reducing both ends mod a turn independently puts the end
  behind the start, and `graphics_fill_radial` draws nothing at all when the start is
  the larger — a third of the day would render blank. `fill_ring_band()` normalises the
  start and carries the span, then splits at the boundary.
- **The ring sweeps the opposite way from the digital face's arc.** There the angle
  decreased as the strip went down, so a band started from its later end. Here time
  runs clockwise with increasing dial angle and the earlier end is the one to sweep
  from.
- **No cosine correction anywhere.** The dial's markers are square to a true circle on
  all three platforms, so a depth in pixels is already perpendicular to the boundary.
  The old dial that preceded the digital face *did* need one; that was a rectangle's
  angled ray, and there is no such thing here.
- **The disc's size is solved, not chosen.** Each row is a rectangle inscribed in the
  circle, so the binding corner is the far one, and the disc's radius is the largest of
  those corners' distances plus a pad. It comes out from the same four lines on all
  three, and it moves on its own when a row does — which is how dropping the countdown's
  progress bar shrank it, how the move to system fonts moved it again, and how folding
  `gabbro`'s two notification rows into one shrank it a third time. Measured:
  32/44/61 px on flint/emery/gabbro under the Rajdhani resources, 34/44/65 after the
  system fonts, **34/44/58 now**. Gothic is wider per row and shorter per row, and on
  two of the three displays those cancel; the third number is a row that stopped
  existing.
- **The countdown is `"%d:%02d"`, not `"%02d:%02d"`.** One digit of hours rather than two
  is what fits it into flint's disc, and nothing is lost: the far tier reaches five hours
  and a count-up cannot outrun a meeting. It is clamped at 9:59 so it can never grow.
- **The countdown's sign is the direction, and it is the only thing that carries it.**
  `+` while an appointment is running, `-` before anything starts. The reading used to be
  a progress bar under the digits, which drew only while something was running — so the
  countdown case was an *absence*, and an absence cannot be read. The sign states both
  cases on the line it belongs to and needs no colour, which is what `flint` has.
- **The sign is drawn at two heights, and the digits are a second draw because of it.**
  The `+` rides up onto the digits' cap line and the `-` drops onto their baseline
  (`SIGN_RISE`), so which way the count is running is legible from where the mark sits
  before its shape resolves — which is the order those two are read in at slot size.
  It costs nothing: three sixteenths of the text height is the gap the font leaves
  between a `+` and a cap, so neither position leaves the band the digits ink and no row
  of the disc moves. It is no longer the same fraction as `ROW_LIFT` — that was a
  coincidence of the old resources' metrics, not a shared derivation. The row is measured and
  placed as if the sign were always `+` — the wider of the two, and what the plate is
  budgeted against — and the sign that draws goes left-aligned into that width, so
  nothing moves at the minute the count turns over.
- **The countdown row gets half the glyph gap the other rows get — `COUNT_GAP_DIV`.**
  The sign is a glyph of width the row did not carry before, and width in a row of the
  disc costs the plate more radius than the bar it replaced cost in height; halving the
  gap pays that back. Half and not none, because at no gap a *task* countdown put the
  blunt wedge hard against the minus, and a solid triangle followed by a horizontal bar
  is a maneuver arrow — which is what the row below draws.
- **The notification glyphs get a three-pixel stroke from 14 px, where the floor used
  to be 16 and flint never reached it.** flint's band glyph was 13 px under the Rajdhani
  resources and is 14 under Gothic, so the one display that most needs the weight was
  the one not getting it. That was survivable beside a condensed semibold TTF rasterised
  light; beside a system Gothic Bold a hairline maneuver reads as a different kind of
  mark from the number it qualifies. See `glyph_stroke` in `slots.c`.
- **`slot_layout()` shrinks the glyph before it stacks the row.** The glyph is the
  qualifier and the number is the reading, so the glyph gives way first — and a row
  that keeps its shape keeps its alignment with the one opposite it. Stacking is the
  last resort, and in a row only twenty pixels tall it is unreadable, which is how the
  order was found.
- **The notification pair is side by side on the rectangles and a single slot on
  gabbro.** Abreast they need 157 px of chord there and the disc offers 142, and a
  circle sells height near its centre far more cheaply than width — so the obvious fix
  was two rows, and it was the wrong one. A row of the disc is the dearest place on
  this face to put anything: it is solved into the radius, the radius is drawn over the
  hands, and the second row cost seven pixels of it. What the rectangles have that the
  round display does not is a strip of screen below the dial that costs the clock
  nothing, and that is what pays for two readings there.

  So on `gabbro` the notification area is one row holding one reading, chosen by strict
  priority: the companion being gone, then the maneuver, then the phone's battery, then
  the watch's. The plate is 58 px where two rows made it 65, and the difference is hour
  hand. The rectangles are unchanged and still show a turn and a low battery at once.
- **On that one slot a maneuver outranks a low battery, and on the rectangles it does
  not have to.** A turn instruction is perishable — it is wrong within the minute if it
  is not acted on — where a battery reading is true all day and will still be there
  after the junction. The suppression is one line at the top of `slots_draw_warn()`,
  gated on `band_inset`, and it is only ever reached on the round display. The tier
  above it needs no line at all: both roads into the companion-down state null the
  maneuver upstream, so nav cannot be active while the alert draws.
- **The companion-down alert says `NO LINK` on the rectangles and shows only its glyph
  on `gabbro`.** It is the one reading in the band with no number under it, so the
  slashed phone is the only glyph on this face asked to carry a whole state on its own
  — the other two qualify a figure — and a reader who has not learnt it has nothing to
  learn it from. Where the band is a strip of screen there is room to spell it out.
  Where it is a row of the disc there is not: `layout_compute()` sizes the disc from the
  widest string each row can hold, so a seven-character label would grow the plate over
  the dial permanently in order to state something that is true for minutes at a time —
  and would hand straight back the radius that merging the two rows into one just
  bought. The merged row is budgeted against `"000 KM"`, which is the widest reading it
  can hold that is not this one.
- **That label is wider than warn's own share of the band, and borrows nav's.** Free
  rather than lucky. Both roads into the state drop the maneuver — the watchdog expiring
  in `hb_expired()` and the link going down in `wire_set_connected()`, each for its own
  reason — so `wire_nav_active()` is false for as long as the alert is drawing, and the
  half of the band to its left is empty by construction. The pair stays right-aligned,
  so it sits where the lone glyph sat and grows leftward into that room rather than
  shifting into it: nothing on the band moves when the words appear.
- **The date row does not centre itself when there is no countdown.** The countdown's
  row is reserved whether it draws or not. A disc that resized when an appointment
  appeared would move every row in it at the one moment the reader is watching one.

## Architecture notes

Single window, single layer, one update proc. Every repaint is
`layer_mark_dirty(s_root_layer)`.

**Nothing animates.** The only tick is `MINUTE_UNIT`: the minute hand steps six
degrees, the hour hand and the whole window half a degree, and recomputing the face is
what moves them. The same tick advances the countdown and retires whatever has aged
out, because every marker's position, prominence and existence is a function of `now`.

**No second hand, deliberately.** It would be the first thing in either watchface to
need a faster tick, and with it the flash subsystem and `app_focus_service` subscription
the redesign deleted.

**Two persistent-storage keys, and the event table is one of them.** A watchface is not
a resident program — the firmware kills it whenever the user opens anything else and
starts it again when they come back — so key 1 holds `wbatt`'s drain anchor, which could
never accumulate a sample history in RAM, and key 2 holds the calendar table, which used
to come back empty. The companion re-flushes on its periodic tick, but that tick is the
slow tier's 600 s, so a glance at a notification cost up to ten minutes of an empty ring.
An empty ring does not read as "waiting", it reads as "nothing scheduled". `events_save()`
runs in `deinit()` and `events_load()` in `init()`; entries carry absolute starts, so
`events_gc()` drops whatever expired while the app was gone on the first paint back.

**Two `AppTimer`s**, both in `wire.c`: the companion liveness watchdog and the nav-slot
expiry. Both follow the same single-owner pattern — one `*_sync()` function decides
whether the timer should be running and nothing else touches the handle — and both traps
apply: `app_timer_cancel` invalidates the handle, and an elapsed timer must never be
cancelled, which is why each callback nulls the handle as its first statement.

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
