#!/bin/sh
# Start the IIGS-Emu test displays: Xorg (dummy driver) on :1 and headless
# sway on wayland-1. Safe to re-run; servers already running are left alone.
set -eu

DIR=/root/iigs-env
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

if [ ! -S /tmp/.X11-unix/X1 ]; then
    setsid Xorg :1 -config "$DIR/xorg-dummy.conf" -noreset -novtswitch \
        -nolisten tcp -logfile "$DIR/Xorg.1.log" \
        >"$DIR/Xorg.1.stdout" 2>&1 </dev/null &
    echo $! >"$DIR/Xorg.1.pid"
fi

if [ ! -S "$XDG_RUNTIME_DIR/wayland-1" ]; then
    env WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 \
        LIBSEAT_BACKEND=builtin \
        setsid sway -c "$DIR/sway-headless.conf" -d >"$DIR/sway.log" 2>&1 </dev/null &
    echo $! >"$DIR/sway.pid"
fi

i=0
until DISPLAY=:1 xdpyinfo >/dev/null 2>&1 && [ -S "$XDG_RUNTIME_DIR/wayland-1" ]; do
    i=$((i + 1))
    [ "$i" -gt 30 ] && { echo "displays did not come up; see $DIR/*.log" >&2; exit 1; }
    sleep 1
done
echo "X11:     DISPLAY=:1 (Xorg, 1920x1080)"
echo "Wayland: WAYLAND_DISPLAY=wayland-1 (headless sway, 1280x960)"
