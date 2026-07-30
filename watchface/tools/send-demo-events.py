#!/usr/bin/env python3
"""Push the demo calendar to a running watchface, over the wire.

`PROTO_DEMO=1 pebble build` compiles the same entries in. This sends them instead,
which is better in two ways: the decode path in `wire.c` is exercised rather than
bypassed, and the dial can be changed without rebuilding and reinstalling. It also
means a shipping build is never the one used for screenshots.

pebble-tool grew `--bytes`/`--bytes-file` in 5.0.39; before that the packed blob
genuinely had no command-line path, which is why `demo_seed()` exists at all.

The set covers every marker case at once — a running band, two overlapping bands that
must flatten into one, clustered point entries that must merge, an overdue reminder,
and a marker sitting on top of a band. **Keep it in step with `demo_seed()` in
`src/c/proto.c`**; the two are meant to show the same face.

    tools/send-demo-events.py                     # flush, then the eight entries
    tools/send-demo-events.py --emulator gabbro
    tools/send-demo-events.py --clear             # flush carrying no records
    tools/send-demo-events.py --remove 5 6        # delta: two removes, no flush
    tools/send-demo-events.py --dry-run           # print the blob, send nothing

With no `--emulator` it talks to whichever emulator is already running, and starts one
if none is — installing the watchface as it goes, because a freshly booted emulator is
showing the stock face and would drop the message on the floor.

Offsets are resolved against *this machine's* clock at send time. If the emulator's
clock has been moved with `pebble emu-set-time`, the markers land wherever that
difference puts them.
"""

import argparse
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import time

WATCHFACE = pathlib.Path(__file__).resolve().parent.parent

# docs/protocol.md is the contract these mirror.
KEY_HEARTBEAT = 10000
KEY_CAL_EVENTS = 10001
KEY_CAL_FLAGS = 10002

CAL_VERSION = 1
CAL_FLUSH = 0x1
CAL_MORE = 0x2
OP_UPSERT = 0
OP_REMOVE = 1
EV_APPOINTMENT = 0
EV_TASK = 1

# 6 header bytes, 12 per record, little-endian throughout.
HEADER = "<BBI"
RECORD = "<IiHBB"

# What the companion caps a message at, and what the 512-byte inbox was sized for:
# 6 + 24 x 12 = 294 payload bytes. More than this splits, with MORE set on all but
# the last message.
MAX_RECORDS = 24

# Where pebble-tool records the emulators it has launched.
EMULATOR_STATE = pathlib.Path(tempfile.gettempdir()) / "pb-emulator.json"

# Started when nothing is running and no codename was given. flint's single ink is
# what hides a marker the colour platforms would forgive.
DEFAULT_PLATFORM = "flint"

# id, start offset in minutes from now, duration in minutes, kind, why it is here.
DEMO = [
    (1, -20, 90, EV_APPOINTMENT, "running: solid band + count-up"),
    (2, 150, 60, EV_APPOINTMENT, "overlaps 3 -- the two must flatten to one band"),
    (3, 180, 90, EV_APPOINTMENT, "overlaps 2"),
    (4, 40, 45, EV_APPOINTMENT, "inside 3 h, not 30 min"),
    (5, -40, 0, EV_TASK, "overdue: solid triangle"),
    (6, 300, 0, EV_TASK, "6 min from 7 -- same notch"),
    (7, 306, 0, EV_TASK, "-> one deeper marker"),
    (8, 200, 0, EV_TASK, "sits on top of a band"),
]


def app_uuid():
    """From package.json, so this keeps working if the UUID ever moves."""
    with open(WATCHFACE / "package.json") as fh:
        return json.load(fh)["pebble"]["uuid"]


def live_emulators():
    """Codename -> whether it is running under VNC, for emulators that are up.

    pebble-tool's state file outlives the processes it describes, so a recorded pid
    is only evidence; each one is checked before being believed.
    """
    try:
        with open(EMULATOR_STATE) as fh:
            state = json.load(fh)
    except (OSError, ValueError):
        return {}

    live = {}
    for platform, versions in state.items():
        for info in versions.values():
            qemu = info.get("qemu", {})
            if pid_alive(qemu.get("pid")):
                live[platform] = bool(qemu.get("vnc"))
    return live


def pid_alive(pid):
    if pid is None:
        return False
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def resolve_target(requested, live):
    """Which emulator to talk to, and whether it still has to be started."""
    if requested:
        return requested, requested not in live
    if len(live) == 1:
        return next(iter(live)), False
    if len(live) > 1:
        sys.exit("Multiple emulators are running ({}); name one with --emulator."
                 .format(", ".join(sorted(live))))
    return DEFAULT_PLATFORM, True


def resolve_vnc(platform, live, requested):
    """Match a running emulator's display mode; pick one for a fresh launch.

    Returns (vnc, restarts). Getting this wrong is not a failed command: when the mode
    asked for differs from that of a running emulator, pebble-tool kills it and launches
    a replacement, which comes back on the stock watchface. So an unasked-for mode is
    never chosen for an emulator that is already up, and when the mode *is* asked for
    explicitly it is honoured -- with the reinstall that the restart then needs.
    """
    if platform not in live:
        if requested is not None:
            return requested, False
        # Headless is the only place VNC is required, and $DISPLAY is how to tell.
        return not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")), False

    running = live[platform]
    if requested is None or requested == running:
        return running, False
    print("note: {} is running {} VNC; pebble-tool will restart it into the other "
          "mode, so the watchface is reinstalled too"
          .format(platform, "under" if running else "without"), flush=True)
    return requested, True


def install_app(platform, vnc, why):
    """Install the watchface, starting the emulator if it is not up yet.

    `pebble install` starts the emulator itself, so there is no separate launch
    step -- and the install is the point: the message goes to a UUID, and nothing is
    listening on it until the app is both installed and on screen.
    """
    argv = ["pebble", "install", "--emulator", platform]
    if vnc:
        argv += ["--vnc"]
    # Flushed, because pebble's own output is not buffered and would otherwise
    # print before the line explaining why it is running.
    print(why, flush=True)
    result = subprocess.run(argv, cwd=WATCHFACE)
    if result.returncode != 0:
        sys.exit("pebble install failed (exit {})".format(result.returncode))
    time.sleep(1.5)     # let pkjs finish attaching before the first send


def pack(records):
    """records: [(id, start_epoch_s, dur_min, kind, op)] -> the CalEvents blob."""
    blob = struct.pack(HEADER, CAL_VERSION, len(records), 0)
    for rec in records:
        blob += struct.pack(RECORD, *rec)
    return blob


def demo_records(now):
    return [(eid, now + off * 60, dur, kind, OP_UPSERT)
            for eid, off, dur, kind, _ in DEMO]


def describe(rec):
    eid, start, dur, kind, op = rec
    if op == OP_REMOVE:
        return "  id {:<3} remove".format(eid)
    when = time.strftime("%H:%M", time.localtime(start))
    if dur:
        end = time.strftime("%H:%M", time.localtime(start + dur * 60))
        what = "appointment {}-{} ({} min)".format(when, end, dur)
    else:
        what = "{:<11} {}".format("task" if kind == EV_TASK else "appointment", when)
    return "  id {:<3} {}".format(eid, what)


def send(opts, records, flush):
    """One message per chunk. FLUSH rides the first, MORE every one but the last."""
    chunks = [records[i:i + MAX_RECORDS]
              for i in range(0, len(records), MAX_RECORDS)] or [[]]
    uuid = app_uuid()

    for i, chunk in enumerate(chunks):
        flags = 0
        if flush and i == 0:
            flags |= CAL_FLUSH
        if i < len(chunks) - 1:
            flags |= CAL_MORE

        blob = pack(chunk)
        argv = ["pebble", "send-app-message"]
        if opts.phone:
            argv += ["--phone", opts.phone]
        else:
            argv += ["--emulator", opts.emulator]
        if opts.vnc:
            argv += ["--vnc"]
        argv += ["--app-uuid", uuid,
                 # Heartbeat goes with every message: silence is what the watch reads
                 # as the companion being gone, and it starts counting from this.
                 "--int", "{}={}".format(KEY_CAL_FLAGS, flags),
                 "{}={}".format(KEY_HEARTBEAT, opts.heartbeat),
                 "--bytes", "{}={}".format(KEY_CAL_EVENTS, blob.hex())]

        print("message {}/{}: {} record(s), flags {:#04x}, {} bytes".format(
            i + 1, len(chunks), len(chunk), flags, len(blob)))
        for rec in chunk:
            print(describe(rec))
        if opts.dry_run:
            print("  blob {}".format(blob.hex()))
            continue

        if subprocess.run(argv, cwd=WATCHFACE).returncode != 0:
            # The commonest reason a send fails is that the watchface is installed
            # but not on screen -- a rebooted emulator comes back on the stock face,
            # and a message addressed to a UUID nothing is running just NACKs. One
            # install fixes that; anything else is a real failure.
            if opts.phone or i > 0:
                return 1
            install_app(opts.emulator, opts.vnc,
                        "send failed; installing the watchface and retrying once")
            if subprocess.run(argv, cwd=WATCHFACE).returncode != 0:
                return 1
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    target = ap.add_mutually_exclusive_group()
    target.add_argument("--emulator", metavar="CODENAME",
                        choices=["flint", "emery", "gabbro"],
                        help="device codename to send to: flint, emery or gabbro "
                             "(default: whichever emulator is running, else {} "
                             "started fresh)".format(DEFAULT_PLATFORM))
    target.add_argument("--phone", metavar="IP",
                        help="send to a phone running the developer connection")
    ap.add_argument("--vnc", action="store_true", default=None,
                    help="talk to the emulator over VNC (default: match the running "
                         "emulator, else decide from $DISPLAY)")
    ap.add_argument("--no-vnc", dest="vnc", action="store_false")
    ap.add_argument("--install", action="store_true",
                    help="reinstall the watchface first (implied when the emulator "
                         "has to be started)")
    ap.add_argument("--list", action="store_true",
                    help="list running emulators and exit")
    ap.add_argument("--clear", action="store_true",
                    help="flush carrying no records -- 'the next six hours are empty'")
    ap.add_argument("--remove", nargs="+", type=int, metavar="ID",
                    help="send removes for these ids as a delta, without flushing")
    ap.add_argument("--heartbeat", type=int, default=300, metavar="S",
                    help="seconds until the companion next checks in (15-3600)")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the blob and what it says, send nothing")
    opts = ap.parse_args()

    if not 15 <= opts.heartbeat <= 3600:
        ap.error("--heartbeat is clamped to 15-3600 by the watch")

    live = live_emulators()

    if opts.list:
        print("\n".join("{:<8} {}".format(p, "vnc" if live[p] else "local display")
                        for p in sorted(live)) or "No emulators running.")
        return 0

    # A phone is already running the app; only emulators need finding or starting.
    if not opts.phone:
        opts.emulator, must_start = resolve_target(opts.emulator, live)
        opts.vnc, restarts = resolve_vnc(opts.emulator, live, opts.vnc)
        if not opts.dry_run and (must_start or restarts or opts.install):
            install_app(opts.emulator, opts.vnc,
                        "no {} emulator running; starting one".format(opts.emulator)
                        if must_start else
                        "installing the watchface on {}".format(opts.emulator))

    now = int(time.time())
    if opts.clear:
        records, flush = [], True
    elif opts.remove:
        records = [(eid, now, 0, EV_TASK, OP_REMOVE) for eid in opts.remove]
        flush = False       # a delta: the watch keeps everything else it holds
    else:
        records, flush = demo_records(now), True

    return send(opts, records, flush)


if __name__ == "__main__":
    sys.exit(main())
