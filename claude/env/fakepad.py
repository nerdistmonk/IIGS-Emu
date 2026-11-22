#!/root/iigs-env/venv/bin/python
"""Virtual gamepads for testing via uinput.

Usage: fakepad.py xbox360|snes

  xbox360  "Microsoft X-Box 360 pad 0" (045e:028e), mapped by gamecontrollerdb.
  snes     "NEXT SNES Controller" (0810:e501), the cheap SNES USB pad. The D-pad
           is an ABS_X/ABS_Y axis pair (-1..1) and there are 10 generic joystick
           buttons. Matches the gamecontrollerdb entry (dpleft:-a0, dpright:+a0).

Commands are read line by line from FIFO:

    btn BTN_SOUTH 1        press (1) or release (0) a button
    abs ABS_X 32767        set an axis value
    quit                   destroy the device and exit

Prints "ready <devnode>" once the device exists.
"""
import glob
import os
import sys
import time

from evdev import AbsInfo, UInput, ecodes as e

FIFO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pad.fifo")
STICK = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)
TRIG = AbsInfo(value=0, min=0, max=255, fuzz=0, flat=0, resolution=0)
HAT = AbsInfo(value=0, min=-1, max=1, fuzz=0, flat=0, resolution=0)
DPAD = AbsInfo(value=0, min=-1, max=1, fuzz=0, flat=0, resolution=0)

XBOX_CAPS = {
    e.EV_KEY: [e.BTN_SOUTH, e.BTN_EAST, e.BTN_NORTH, e.BTN_WEST,
               e.BTN_TL, e.BTN_TR, e.BTN_SELECT, e.BTN_START,
               e.BTN_MODE, e.BTN_THUMBL, e.BTN_THUMBR],
    e.EV_ABS: [(e.ABS_X, STICK), (e.ABS_Y, STICK),
               (e.ABS_Z, TRIG), (e.ABS_RX, STICK), (e.ABS_RY, STICK),
               (e.ABS_RZ, TRIG), (e.ABS_HAT0X, HAT), (e.ABS_HAT0Y, HAT)],
}


SNES_CAPS = {
    e.EV_KEY: [e.BTN_TRIGGER, e.BTN_THUMB, e.BTN_THUMB2, e.BTN_TOP,
               e.BTN_TOP2, e.BTN_PINKIE, e.BTN_BASE, e.BTN_BASE2,
               e.BTN_BASE3, e.BTN_BASE4],
    e.EV_ABS: [(e.ABS_X, DPAD), (e.ABS_Y, DPAD)],
}

PROFILES = {
    "xbox360": dict(name="Microsoft X-Box 360 pad 0", vendor=0x045E,
                    product=0x028E, version=0x0114, caps=XBOX_CAPS),
    "snes": dict(name="NEXT SNES Controller", vendor=0x0810,
                 product=0xE501, version=0x0000, caps=SNES_CAPS),
}


def ensure_event_node(ui_name):
    """Create /dev/input/eventN if devtmpfs did not, since SDL scans there."""
    for sysdir in sorted(glob.glob("/sys/class/input/event*")):
        try:
            with open(os.path.join(sysdir, "device", "name")) as f:
                if f.read().strip() != ui_name:
                    continue
        except OSError:
            continue
        node = "/dev/input/" + os.path.basename(sysdir)
        with open(os.path.join(sysdir, "dev")) as f:
            major, minor = (int(x) for x in f.read().strip().split(":"))
        os.makedirs("/dev/input", exist_ok=True)
        if not os.path.exists(node):
            os.mknod(node, 0o660 | 0o020000, os.makedev(major, minor))
        return node
    return None


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in PROFILES:
        sys.exit("usage: fakepad.py " + "|".join(PROFILES))
    p = PROFILES[sys.argv[1]]
    ui = UInput(p["caps"], name=p["name"], vendor=p["vendor"],
                product=p["product"], version=p["version"], bustype=e.BUS_USB)
    time.sleep(0.5)
    node = ensure_event_node(p["name"])
    print("ready", node, flush=True)

    if not os.path.exists(FIFO):
        os.mkfifo(FIFO)
    while True:
        with open(FIFO) as f:
            for line in f:
                parts = line.split()
                if not parts:
                    continue
                if parts[0] == "quit":
                    ui.close()
                    return
                if parts[0] == "btn":
                    ui.write(e.EV_KEY, getattr(e, parts[1]), int(parts[2]))
                elif parts[0] == "abs":
                    ui.write(e.EV_ABS, getattr(e, parts[1]), int(parts[2]))
                ui.syn()


if __name__ == "__main__":
    sys.exit(main())
