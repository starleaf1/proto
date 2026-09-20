#!/usr/bin/env python3
"""Screenshot a running emulator through QEMU's own monitor socket.

`pebble screenshot` goes through pypkjs and the firmware's screenshot endpoint,
and in some environments that channel times out forever while the emulator is
otherwise healthy — installs succeed, the display updates, only the screenshot
hangs. QEMU's monitor has a `screendump` command that reads the framebuffer
directly and does not involve the watch software at all, so it keeps working.

    tools/grab.py flint                  # -> /tmp/flint.png, upscaled 4x
    tools/grab.py gabbro --scale 2
    tools/grab.py flint --out /tmp/a.png

The port comes from pebble-tool's own state file, so this follows whichever
emulator that tool most recently started.
"""

import argparse
import json
import os
import socket
import sys
import tempfile
import time

from PIL import Image

STATE = os.path.join(tempfile.gettempdir(), "pb-emulator.json")


def monitor_port(platform):
    with open(STATE) as fh:
        state = json.load(fh)
    if platform not in state:
        sys.exit(f"no {platform} emulator recorded in {STATE}")
    # One SDK version per platform in practice; take the newest if not.
    ver = sorted(state[platform])[-1]
    qemu = state[platform][ver]["qemu"]
    if not pid_alive(qemu["pid"]):
        sys.exit(f"{platform}'s qemu (pid {qemu['pid']}) is not running")
    return qemu["monitor"]


def pid_alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def screendump(port, path):
    if os.path.exists(path):
        os.unlink(path)
    sock = socket.create_connection(("127.0.0.1", port), timeout=10)
    sock.settimeout(2)
    drain(sock)
    sock.sendall(f"screendump {path}\n".encode())
    # The monitor echoes the command back before acting on it, so waiting for
    # the file rather than for a reply is what actually tells us it is done.
    for _ in range(40):
        time.sleep(0.1)
        if os.path.exists(path) and os.path.getsize(path) > 0:
            break
    drain(sock)
    sock.close()
    if not os.path.exists(path):
        sys.exit("qemu wrote no framebuffer dump")


def drain(sock):
    try:
        while sock.recv(4096):
            pass
    except socket.timeout:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("platform", nargs="?", default="flint",
                    choices=["flint", "emery", "gabbro"])
    ap.add_argument("--out")
    ap.add_argument("--scale", type=int, default=4,
                    help="nearest-neighbour upscale; pebble-tool has no --scale "
                         "and these displays need one to inspect")
    args = ap.parse_args()

    out = args.out or os.path.join(tempfile.gettempdir(), f"{args.platform}.png")
    ppm = out + ".ppm"
    screendump(monitor_port(args.platform), ppm)

    img = Image.open(ppm).convert("RGB")
    if args.scale > 1:
        img = img.resize((img.width * args.scale, img.height * args.scale),
                         Image.NEAREST)
    img.save(out)
    os.unlink(ppm)
    print(f"{out}  {img.width}x{img.height}")


if __name__ == "__main__":
    main()
