# Pebble watchface (`watchface/`)

This directory is the Pebble smartwatch component of the **proto** monorepo — a
watchface written in C using the Pebble SDK. The sibling `pipe/` directory
holds the Android companion app. See the repo-root `README.md` and `docs/` for the
system-level picture and the watch↔phone protocol contract.

> **Run every `pebble` command from this directory** (`watchface/`), not the
> repo root. The SDK expects `package.json`, `wscript`, `src/`, and `resources/`
> in the working directory.

## Supported Platforms

The app targets multiple Pebble watch models:
- aplite (Pebble classic)
- basalt (Pebble Time)
- chalk (Pebble Time Round)
- diorite (Pebble 2)
- emery (Pebble Time 2)
- flint (Pebble 2 Duo)
- gabbro (Pebble Round 2)

## Commands

```bash
# Build the app for all platforms
pebble build

# Clean build artifacts
pebble clean

# Install the app on specific emulator
pebble install --emulator basalt

# Screenshot the running emulator
pebble screenshot --no-open screenshot.png

# Drive the companion-fed icons by numeric key id (see ../docs/protocol.md)
pebble send-app-message --emulator basalt --vnc --int 10000=3 10002=2
```

If you need more information on the `pebble` command or a sub-command, append `--help`.

> **After adding a message key**, run `pebble clean` before `pebble build`.
> `message_keys.auto.h` is not regenerated incrementally, so the new
> `MESSAGE_KEY_*` symbol comes back undeclared.

### Headless Environments

If you're running in an environment without a window server (e.g., headless Linux, Docker, CI), you must add `--vnc` to **all commands that interact with the emulator**. This includes app installs, screenshots, button presses, and any `emu-*` commands:

```bash
pebble install --emulator basalt --vnc
pebble screenshot --vnc --no-open screenshot.png
pebble emu-button --emulator basalt --vnc click select
```

The `--vnc` flag enables a VNC-based display backend that doesn't require X11.

### Emulator gotchas in this environment

- **One emulator per platform at a time.** A wedged `qemu-pebble` keeps VNC display
  `:1` bound, and the next command fails with `Failed to find an available port`.
  `pkill -f qemu-pebble` before retrying.
- `pebble screenshot` has **no `--scale` flag** in pebble-tool 5.0.39.
- **Colour correction is on by default** — the screenshot is remapped through a
  display-emulation LUT, so `GColorYellow` comes out near-white. Pass
  `--no-correction` when asserting on exact palette RGB.
- `pebble emu-set-time` does not move an already-running watchface.
- `pebble send-app-message` requires **numeric** key ids, not names.
- `pebble emu-bt-connection --connected no` tends to wedge the control channel; do
  it last, in a throwaway emulator.

## Project Structure

```
watchface/
  src/c/           - C source files for the watchapp
  src/pkjs/        - PebbleKit JS files (phone-side stub)
  worker_src/c/    - Worker source files (optional, not present)
  resources/       - Images, fonts, and other resources
  package.json     - Pebble app manifest (UUID, platforms, message keys)
  wscript          - waf build rules
  build/           - Build output (generated; gitignored)
```

## Configuration

By default, this project is initialized as a watchface. To make it an app, replace "watchface": true with "watchface": false in package.json.

## Architecture

The application follows the standard Pebble app architecture:

1. **Main Entry Point**: `src/c/proto.c` - The `main()` function initializes the app and starts the event loop
2. **Window Management**: Single window app with a root layer whose update proc draws the time, date, and status icons
3. **Event Handling**: Tick, battery, connection, and app-focus service handlers; `UnreadCount`, `MissedCount` and `PhoneState` arrive via AppMessage inbox from the companion

## Phone ↔ watch contract

The watchface renders two companion-driven icons from three AppMessage keys:

| Key | Id | Drives |
| --- | -- | ------ |
| `UnreadCount` | `10000` | Envelope: `> 0` lit, `0` faded |
| `MissedCount` | `10001` | A count. Informational once `PhoneState` has been sent |
| `PhoneState`  | `10002` | Phone icon — see the table below |

The companion decides state; this watchface decides how each state *looks* on the
current display. **No colour or blink timing crosses the wire.**

| `PhoneState` | Colour (basalt, chalk, emery, gabbro) | B&W (aplite, diorite, flint) |
| ------------ | ------------------------------------- | ---------------------------- |
| `0` idle     | `GColorLightGray`, faded via `fade_icon` | faded, static             |
| `1` ongoing  | `GColorIslamicGreen`, **steady**      | flashing **2 Hz**            |
| `2` ringing  | flashing `GColorIslamicGreen` ↔ `GColorChromeYellow`, 4 Hz | flashing **4 Hz** |
| `3` missed   | `GColorRed`, steady                   | solid black, static          |

Two things here are deliberate and easy to "fix" wrongly:

- **`GColorGreen` and `GColorYellow` are not used.** On the white background they
  measure 1.4:1 and 1.1:1 contrast — a flash whose dim phase is invisible. The
  battery gauge can use them because it sits inside a black outline; this free-
  floating gpath cannot.
- **B&W uses rate, not density, to separate ongoing from ringing.** With only one
  ink colour there is no hue to spend, so the flash rate carries the meaning. An
  earlier attempt used a steady half-tone for ongoing, but then a single glance
  could not distinguish it from a dithered idle.

### Animation

The phone icon's flash is the app's only sub-minute repaint. Its `AppTimer` is owned
solely by `flash_sync()` — every handler that can change `s_phone`, `s_connected` or
`s_focused` calls it, and nothing else touches the handle. `flash_period()` is the
single place that decides whether and how fast to flash, so it is also what makes
ringing-then-answered re-arm at the new rate rather than keep the old one.

It stops on every static state, on link loss, under a modal, and after a 120 s
watchdog (which leaves the icon lit, just still). Reuse that single-owner pattern for
any future animation, and note two `AppTimer` traps: `app_timer_cancel` invalidates
the handle, and an elapsed timer must never be cancelled — which is why `flash_tick`
nulls the handle as its first statement.

## SDK Documentation

The full Pebble SDK documentation is available at https://developer.repebble.com.

An index of every page is at https://developer.repebble.com/llms.txt. Use it to discover what's available. Every page also has a Markdown version: append `.md` to any documentation URL to fetch plain Markdown instead of HTML (e.g. `https://developer.repebble.com/guides/events-and-services/buttons.md`). Prefer the `.md` form when reading docs.

Main Categories:
- Tutorials - Step-by-step learning (C watchface tutorial in 5 parts, advanced topics)
- Developer Guides - Comprehensive reference organized by topic

Key Sections:
- App Resources - Images, fonts, vector graphics, 256 resource limit
- User Interfaces - Layer hierarchy, TextLayer, MenuLayer, round vs rectangular displays
- Events & Services - Buttons, accelerometer, compass, health data, background workers
- Communication - Bluetooth AppMessage, PebbleKit JS/Android/iOS integration
- Graphics & Animations - Drawing APIs, property animations, vector graphics
- Debugging - App logs, GDB, common errors and solutions
- Best Practices - Multi-platform support, battery conservation, modular architecture
- Design & Interaction - Glance-first design, one-click actions, platform guidelines
- App Store Publishing - Submission requirements, assets, analytics

Key Entry Points:
- https://developer.repebble.com/tutorials/watchface-tutorial/part1 - C development start
- https://developer.repebble.com/guides/events-and-services/buttons - Button handling
- https://developer.repebble.com/guides/user-interfaces/layers - UI foundations

## Development Best Practices

- Whenever making changes, run `pebble screenshot --no-open <file>` and view the screenshot to make sure it's what the user requested. If not, make more changes until it does what it's supposed to.

## Emulator Button Control

Control emulator buttons programmatically with `pebble emu-button`:

```bash
# Click a button (press and release)
pebble emu-button click select

# Long press (e.g., 2 seconds to exit app)
pebble emu-button click back --duration 2000

# Repeat clicks (e.g., scroll down 5 times)
pebble emu-button click down --repeat 5

# Faster repeat interval
pebble emu-button click up --repeat 3 --interval 100
```

**Actions:**
- `click` - Press then release (use `--duration` for long press)
- `push` - Hold button down (use `release` to let go)
- `release` - Release all buttons

**Buttons:** `back`, `up`, `select`, `down`

**Best Practices:**
- Use `click` for normal navigation and selection
- Use `click --duration 2000` for long press (e.g., back button to exit)
- Use `--repeat` to scroll through menus instead of multiple commands
- After making UI changes, take a screenshot to verify the result

## AI Interaction Guidelines

- When given an image of a watchface to replicate, describe the target watchface in precise detail. Note every visual element present, as well as size, alignment, font weight, spacing, and location.

## AI Code Review Guidelines

- Once you think you've fulfilled the user's request, ask yourself if you see any issues with the current screenshot, and if there are any differences between the screenshot and the reference image or the user's description. If so, fix them.
