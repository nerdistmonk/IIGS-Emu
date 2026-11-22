# claude/: handoff and test tooling

Working material for resuming IIGS-Emu work in a new session. Nothing here is compiled into the emulator. `CLAUDE.md` and `claude2.md` at the repo root are separate and unchanged.

- `handoff.md`: repo state, key code locations, verified results, open items, conventions. Read first.
- `env/`: test display scripts (Xorg `:1` with the dummy driver, headless sway on `wayland-1`), the emulator launcher, and uinput fake gamepads.
- `configs/boot-nodisk.cfg`: boots to the Applesoft `]` prompt with no disks.

## Restore the environment

The scripts use absolute paths: `/root/iigs-env`, `/root/iigs-test`, and `/root/IIGS-Emu/build/iigs-emu`.

0. Install packages. Ubuntu 24.04 has no SDL3 package, so CMake fetches SDL3 `release-3.4.10` from GitHub (needs network). `xserver-xorg-input-void` is not in 24.04:
   ```
   apt-get install -y cmake ninja-build git build-essential p7zip-full xdotool imagemagick xserver-xorg-core xserver-xorg-video-dummy xauth sway python3-venv libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxinerama-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev libegl-dev libgl-dev libgles-dev libpulse-dev libasound2-dev libpipewire-0.3-dev libsndio-dev libdbus-1-dev libudev-dev libibus-1.0-dev
   ```
   Uploaded ROMs, disk images, and reference docs are not in the repo. Extract them to `/root/iigs-assets/` from the session's upload folder: `tar -xjf Apple_IIGS_Dev.tar.bz2 -C /root/iigs-assets` and `7z x System6_and_Games.7z -o/root/iigs-assets/System6_and_Games`.
1. Copy the scripts and make them executable:
   ```
   mkdir -p /root/iigs-env /root/iigs-test/work
   cp /root/IIGS-Emu/claude/env/* /root/iigs-env/
   chmod +x /root/iigs-env/*.sh /root/iigs-env/fakepad.py
   ```
2. Create the venv used by `fakepad.py`:
   ```
   python3 -m venv /root/iigs-env/venv && /root/iigs-env/venv/bin/pip install evdev
   ```
3. Copy the config: `cp /root/IIGS-Emu/claude/configs/boot-nodisk.cfg /root/iigs-test/work/`
4. Provide the ROM yourself. The emulator searches for `ROM.03` by relative name in its working directory (`src/config.c:135`, `src/config.c:452`). Loading the config changes the working directory to `/root/iigs-test/work` (the log prints `CWD is now: ...`), so the link goes there, not in `/root/iigs-test`. The 256 KB "ROM 3 Tenspeed Late 1988 Early 1989 v25" image boots:
   ```
   ln -sfn /path/to/your/rom /root/iigs-test/work/ROM.03
   ```
5. Build (CMake home is `src/`):
   ```
   cmake -S src -B build -G Ninja && cmake --build build --target gsplus-sdl
   ```

## Run

Smoke test: after the steps above, the emulator log should show `Found ROM at path: ROM.03`. The screen should show "Check startup device!" with the `]` prompt.

```
/root/iigs-env/start-displays.sh                 # Xorg :1 and headless sway wayland-1
/root/iigs-env/run-emu.sh x11 -cfg /root/iigs-test/work/boot-nodisk.cfg
/root/iigs-env/stop-displays.sh
```

If Xorg does not start because a stale socket exists, remove `/tmp/.X11-unix/X1` and `/tmp/.X1-lock`, then rerun `start-displays.sh`.

Gamepads: `/root/iigs-env/fakepad.py xbox360|snes` creates a uinput pad and reads commands from `/root/iigs-env/pad.fifo`. It needs root.

## Tools used

Xorg with the dummy driver, `xdpyinfo`, `sway`, `xdotool`, ImageMagick `import` (screenshots), and Python 3 with `venv`.
