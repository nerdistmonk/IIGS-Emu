#!/bin/sh
# Stop the IIGS-Emu test displays started by start-displays.sh.
DIR=/root/iigs-env
for name in sway Xorg.1; do
    pidfile="$DIR/$name.pid"
    if [ -f "$pidfile" ]; then
        pid=$(cat "$pidfile")
        kill "$pid" 2>/dev/null && echo "stopped $name (pid $pid)"
        rm -f "$pidfile"
    fi
done
