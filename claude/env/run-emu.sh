#!/bin/sh
# Run the IIGS-Emu build on a test display.
#   run-emu.sh x11 [emulator args...]       # Xorg on :1
#   run-emu.sh wayland [emulator args...]   # headless sway on wayland-1
# Runs from /root/iigs-test so config.kegs and logs stay out of the repo.
set -eu

DIR=/root/iigs-env
BIN=/root/IIGS-Emu/build/iigs-emu
backend=${1:-x11}
[ $# -gt 0 ] && shift

export XDG_RUNTIME_DIR=/run/user/0
export SDL_AUDIODRIVER=dummy
case "$backend" in
    x11)     export SDL_VIDEODRIVER=x11 DISPLAY=:1 ;;
    wayland) export SDL_VIDEODRIVER=wayland WAYLAND_DISPLAY=wayland-1 ;;
    *) echo "usage: $0 x11|wayland [args...]" >&2; exit 2 ;;
esac

mkdir -p /root/iigs-test && cd /root/iigs-test
exec "$BIN" "$@"
