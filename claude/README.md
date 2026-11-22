# claude/: handoff and test tooling

Working material for resuming IIGS-Emu work in a new session. Nothing here is compiled into the emulator. `CLAUDE.md` and `claude2.md` at the repo root are separate and unchanged.

- `handoff.md`: repo state, key code locations, verified results, open items, conventions. Read first.
- `env/`: test display scripts (Xorg `:1` with the dummy driver, headless sway on `wayland-1`), the emulator launcher, and uinput fake gamepads.
- `configs/boot-nodisk.cfg`: boots to the Applesoft `]` prompt with no disks.

## Restore the environment

The scripts use absolute paths: `/root/iigs-env`, `/root/iigs-test`, and `/root/IIGS-Emu/build/iigs-emu`.

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
4. Provide the ROM yourself. The emulator looks up `ROM.03` by relative name in its working directory (`src/config.c:452`), and `run-emu.sh` runs from `/root/iigs-test`:
   ```
   ln -s /path/to/your/rom /root/iigs-test/ROM.03
   ```
   ROMs, disk images, and firmware PDFs are not in the repo.
5. Build (CMake home is `src/`):
   ```
   cmake -S src -B build -G Ninja && cmake --build build --target gsplus-sdl
   ```

## Run

```
/root/iigs-env/start-displays.sh                 # Xorg :1 and headless sway wayland-1
/root/iigs-env/run-emu.sh x11 -cfg /root/iigs-test/work/boot-nodisk.cfg
/root/iigs-env/stop-displays.sh
```

If Xorg does not start because a stale socket exists, remove `/tmp/.X11-unix/X1` and `/tmp/.X1-lock`, then rerun `start-displays.sh`.

Gamepads: `/root/iigs-env/fakepad.py xbox360|snes` creates a uinput pad and reads commands from `/root/iigs-env/pad.fifo`. It needs root.

## Tools used

Xorg with the dummy driver, `xdpyinfo`, `sway`, `xdotool`, ImageMagick `import` (screenshots), and Python 3 with `venv`.
