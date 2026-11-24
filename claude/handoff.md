# IIGS-Emu handoff

Read this first in a new session. It covers repo state, setup and run, test gotchas, verified results, open items, and working conventions. Architecture and build are in `claude/CLAUDE.md`.

## Project
- IIgs-Emu: Apple IIgs emulator forked from KEGS/GSplus, SDL3 build.
- Repo: `/root/IIGS-Emu`, remote `origin` = `nerdistmonk/IIGS-Emu`, branch `main`.
- Run `git log --oneline -10` for the current state.
- Build and configure: see `claude/CLAUDE.md`.

## Key code facts
- `src/adb.c`
  - `g_a2_key_to_ascii[]`. Table bytes: `[1]` is the unshifted column, `[2]` shifted, `[3]` ctrl.
  - `hard_key` is set from the unshifted column `[1] & 0xef00`. It queues a char to `$C000` only when set.
  - `adb_key_is_nonchar_extended()` forces `hard_key = 0` for F-keys and nav keys in `adb_key_event()`. `adb_panel_key_event()` also calls it.
  - `special = (ascii >> 8) & 0xff` is ORed into `$C025` on press and recomputed on release.
  - `adb_key_event()` ← `adb_virtual_key_update()` ← `adb_maybe_virtual_key_update()`. Raw ADB register `adb_kbd_reg0_data()` is written only when autopoll is off (the two calls in `adb_maybe_virtual_key_update()`).
  - Keystroke paste: `adb_paste_add_buf()`, `adb_paste_update_state()`. Paced at `PASTE_FRAMES_PER_KEY` = 4, so 15 keys/s. See the Input section of `claude/CLAUDE.md`.
- `$C025` (KEYMODREG) bits, per the IIgs firmware reference: 0x80 Open Apple, 0x40 Solid Apple, 0x20 modifier latch updated, 0x10 keypad key active, 0x08 repeat, 0x04 caps, 0x02 control, 0x01 shift.
- `$C061` (Open Apple) reads `adb_is_cmd_key_down()` (`src/adb.c`, called from `io_read()` in `src/moremem.c`). Any 0x80 in the table holds Open Apple, which is why F-keys were changed.
- `src/input_map.c`
  - Default map `g_default_map`. `key:PAUSE = adb:0x71` (F15) is a default binding. Copy and paste are `ctrl+shift+key:C` and `ctrl+shift+key:V`.
  - Remap catalog `g_remap_items[]` entries "Copy text screen" and "Paste clipboard" (Mouse and Actions). `imap_remap_bind()`.
  - Actions enum `enum imap_action` in `src/input_map.h`; name table `g_action_names` in `src/input_map.c`. Default-unbound actions: fullscreen, scanlines, crt, status, debug_go, warp_pointer. Bound: mouse_grab (Ctrl+F2 and middle-click), config_panel (Ctrl+F1), reset (Ctrl+F12), zoom_in (Ctrl+1), zoom_out (Ctrl+2), screenshot (Ctrl+F5), speed (Ctrl+F3), fast_disk (Ctrl+F4), copy (Ctrl+Shift+C), paste (Ctrl+Shift+V).
- `g_vbl_count` (defined in `src/sim65816.c`, incremented in `run_16ms()`) advances on wall-clock time, not per guest cycle. Why paste is paced: see the Input section of `claude/CLAUDE.md`.

## Setup and run

The scripts use absolute paths: `/root/iigs-env`, `/root/iigs-test`, and `/root/IIGS-Emu/build/iigs-emu`.

1. Install packages. Ubuntu 24.04 has no SDL3 package, so CMake fetches SDL3 `release-3.4.10` from GitHub (needs network). `xserver-xorg-input-void` is not in 24.04:
   ```
   apt-get install -y cmake ninja-build git build-essential p7zip-full xdotool xclip imagemagick xserver-xorg-core xserver-xorg-video-dummy xauth x11-utils sway python3-venv 64tass libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxinerama-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev libegl-dev libgl-dev libgles-dev libpulse-dev libasound2-dev libpipewire-0.3-dev libsndio-dev libdbus-1-dev libudev-dev libibus-1.0-dev
   ```
2. Extract the uploaded assets. ROMs, disk images, and reference docs are not in the repo. Extract them from the session's upload folder: `tar -xjf Apple_IIGS_Dev.tar.bz2 -C /root/iigs-assets` and `7z x System6_and_Games.7z -o/root/iigs-assets/System6_and_Games`.
3. Copy the scripts, make them executable, and create the venv that `fakepad.py` needs:
   ```
   mkdir -p /root/iigs-env /root/iigs-test/work
   cp /root/IIGS-Emu/claude/env/* /root/iigs-env/
   chmod +x /root/iigs-env/*.sh /root/iigs-env/fakepad.py
   python3 -m venv /root/iigs-env/venv && /root/iigs-env/venv/bin/pip install evdev
   cp /root/IIGS-Emu/claude/configs/*.cfg /root/iigs-test/work/
   ```
4. Provide the ROM. Loading a config changes the working directory to `/root/iigs-test/work`, so the link goes there, not in `/root/iigs-test`. ROM 3 is the image that boots (see `claude/CLAUDE.md`, Runtime Requirements):
   ```
   ln -sfn /path/to/your/rom /root/iigs-test/work/ROM.03
   ```
5. Build: see `claude/CLAUDE.md`, Build Commands.

Run:
```
/root/iigs-env/start-displays.sh                 # Xorg :1 and headless sway wayland-1
/root/iigs-env/run-emu.sh x11 -cfg /root/iigs-test/work/boot-nodisk.cfg
/root/iigs-env/stop-displays.sh
```

- Smoke test: the log shows `Found ROM at path: ROM.03`, and the screen shows "Check startup device!" with the Applesoft `]` prompt.
- Stderr is noisy with `GRIND` debug lines. Filter with `grep -a -v GRIND`. ALSA and XDG_RUNTIME_DIR warnings are environmental.
- `start-displays.sh` skips starting Xorg if `/tmp/.X11-unix/X1` exists. If Xorg won't start because of a stale socket, remove `/tmp/.X11-unix/X1` and `/tmp/.X1-lock`, then rerun it.
- Configs (`claude/configs/`, copied in step 3):
  - `boot-nodisk.cfg`: boots to `]` with no disks.
  - `boot-sys604.cfg`: `boot-nodisk.cfg` with `s7d1 = /root/iigs-assets/System6_and_Games/System 604.hdv`. Use it for Hermes and clipboard tests.
  - `boot-28.cfg`: `boot-sys604.cfg` plus `g_limit_speed = 2` (2.8 MHz).
- Gamepads: `/root/iigs-env/fakepad.py xbox360|snes` creates a uinput pad and reads commands from `/root/iigs-env/pad.fifo`. It needs root.

Checking state:

- `. /root/iigs-env/env.sh` (copy in `claude/env/env.sh`) sets `DISPLAY=:1`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, and dummy audio for ad-hoc commands.
- `/root/iigs-test/work/input.map` is the live input map used by the test configs. It is not committed. Remap Controls rewrites it.
- The window id changes each launch: `DISPLAY=:1 xdotool search --name IIGS-Emu`. Find the PID with `ps -eo pid,args | grep iigs-emu`.
- Screenshot: `DISPLAY=:1 import -window <id> file.png`, then view it.
- Boots to the Applesoft `]` prompt with no disk. Read registers with `PRINT PEEK(-16347)` ($C025), `PEEK(-16287)` ($C061), `PEEK(-16384)` ($C000).
- Hold a key during Return: `xdotool keydown F7; xdotool key Return; xdotool keyup F7`.
- Baseline `$C025` with no keys held is 32 (latch bit) or 0.
- Host clipboard: `DISPLAY=:1 xclip -selection clipboard -o` reads it, and `xclip -selection clipboard` writes it.
- Display check: `xdpyinfo -display :1`.

## Verified (emulator only, not hardware)
- F1 held: `$C061`=0, `$C025`=16, no character.
- Nav keys: no character, `$C025` bit 0x10 while held.
- Remap host F7 → adb:0x73 (Home): `$C025`=48 while held, `$C061`=0, no stray character.
- Stuck-bit check: releasing Shift before F1, and F1 before Shift, both leave `$C025` clear.
- Config panel: F-keys and nav keys do not move the menu.
- Clipboard, harness programs (hand-written 65816): text copy and paste, PutScrap CR→LF with high-bit mask, append, hotkey isolation.
- System 6.0.4, 8 MHz, Hermes: Edit>Paste of two host lines is intact. Edit>Copy of line 1, then line 2, leaves only line 2 on the host clipboard (ZeroScrap clears).
- Multi-line Copy from Hermes: works (user test in the guest).
- WordPerfect (GS/OS): Edit>Copy and Edit>Paste work through the bridge (user test). Ctrl+Shift+V works. Ctrl+Shift+C now shows the text-mode OSD. Its Read.Me screen is SHR with TEXT still set, so the copy used to read a stale text page ("Word.Perfect"). Fixed by checking SHR in `clip_copy_text_screen()`.
- Keystroke paste into Hermes at 2.8 MHz: 580/580 characters matched with pacing. Before pacing, drops grew with paste rate and lower speed. At 16 MHz, rapid pastes showed no drops (user test, before pacing).

## Test harness gotchas

- Config: a `#` in front of an `s<slot>d<drive>` line ejects that drive. An earlier "no error" result was a false negative from this (commit 30c4661). Verify mounts before trusting results.
- Saves: dynamic folders cannot create new files, and a save to one silently does nothing. To test saves, use a blank ProDOS `.po` image (819200 bytes; no generator is kept in the repo, so create one first) and check free space in the Save dialog ("N free of M K"). GS/OS volumes in these images fill quickly.
- Slow guests: to test paste against a slow guest, set `g_limit_speed = 2` (2.8 MHz). With pacing in place, 580/580 keys matched at that speed. Drops there point to a pacing regression.
- GS/OS menus (Hermes and others) need press-and-hold. A quick click does not open them. Use `xdotool mousedown 1`, sleep about 0.8 s, move, then `xdotool mouseup 1`. In Hermes, the entry is near the bottom of the Apple logo menu.
- Hand-written 65816 programs: keep code and data out of `$0400–$07FF` (the text page). Load them through the monitor, run with `300G`, and end with `jmp $ff69` to return to the monitor.
- gdb batch pattern: `timeout 120 gdb -batch -x script.gdb --args /root/build-dbg/iigs-emu -cfg x.cfg`. End scripts with `kill` so the emulator exits. `$fp` is a built-in register alias, so use other names for convenience variables. Calling emulator functions while stopped works, for example `call dynapro_handle_write_file($dskp, $fileh)`. This exercises real code paths without guest I/O.

## Test fixtures

- `/root/iigs-assets/System6_and_Games/System 604.hdv` (the image `boot-sys604.cfg` mounts) contains ShrinkIt as `/ARCHIVE.UTILS/GS.SHRINKIT` (type $B3, aux $DB07).
- `GS.GS.Diag.sdk` and `GSMEMTESTv20.sdk` are NuFX archives, not AppleSingle.
- ShrinkIt creates AppleSingle through the Special menu, "Create AppleSingle...". Drag from Special to the item (mousedown, mouseup). The File menu has no AppleSingle option.
- The desktop wallpaper, drawn by DeskMaker, contains fake disk-like icons. Trust only real volume icons and window contents.

## Test programs and tools

- `claude/harness/`: hand-written 65816 programs. Assemble with `64tass -b -o out.bin file.asm` (raw bytes at `$0300`), then load and run them as in the harness gotchas above. Each header describes its inputs and results.
  - `scrap-test8.asm`: ZeroScrap error isolation. Results are at `$0A30–$0A42`, plus `$0A84–$0A90` for SP and user ID.
  - `scrap-test9.asm`: host clipboard accumulation. Set the guest text at `$00:6000` and `$00:6008` first. After a correct ZeroScrap the host clipboard reads `BBBB`.
  - `kbd-skip-test.asm`: counts keystroke skips. Results are at `$1000–$103D`. Paste 62 characters and compare. Use `boot-28.cfg` for a slow guest.
- `claude/tools/`:
  - `mkhex.py BIN START`: prints a binary as monitor hex lines (`ADDR: XX ...`, CR-separated). START is hex.
  - `prodos_tool.py IMAGE CMD`: reads ProDOS `.po`/`.hdv` images. Commands: `ls`, `get NAME OUTFILE`, `find PATTERN`, `tools NAME...`, and the Toolbox-call scans `scan`, `scrap`, `le`.
- Not kept: the `kbd-blind-test` positive control. It needs `DIAG-PASTE` logging, which the normal build does not have.

## Open items
- PutScrap append vs replace for the same scrap type is unconfirmed. The bridge appends. The three local Toolbox PDFs (`/root/iigs-assets`) have no PutScrap text, so check the Scrap Manager chapter (Toolbox Reference Vol. 2) if you have it.
- Other GS/OS apps' Copy and Paste are untested.
- Whether GS/OS sends every Toolbox call through `$E10000` is unverified beyond the GetScrap family used by Hermes. AppleWorks GS uses other `$E1` entries (`$E10064`, `$E10068`), but none carried Scrap Manager calls during Edit>Copy or Edit>Paste.
- AppleWorks GS Edit>Copy and Edit>Paste do not use the Scrap Manager (no `$16` calls on any `$E1` entry, with Cmd+C and Cmd+V). Out of scope by decision. Ctrl+Shift+C only copies in text or MIX mode.
- A leading `# ` was lost once in an 8 MHz keystroke paste. Not reproduced.

## Working conventions
- CMake and GCC only. Do not suggest or use Clang tooling (clang-format, clang-tidy).
- Linux is the only supported platform. Do not add macOS or Windows code paths.
- Ask before assuming design details or changing behavior. Use `ask_user` with one question at a time, and do not repeat a question already asked without acknowledging it.
- Confirm each bug at runtime (ASan/UBSan, gdb) before changing code.
- Comment code only where it needs clarification.
- Keep replies concise (≤100 words unless the task needs more).
- Commit and push only on explicit request. Write a short imperative subject and a body that explains why. Use the author `nerdistmonk <5949983+nerdistmonk@users.noreply.github.com>` and this trailer:
  `Co-authored-by: copilot-swe-agent[bot] <198982749+Copilot@users.noreply.github.com>`
  Git identity is not set globally, so pass the author through `GIT_AUTHOR_*` and `GIT_COMMITTER_*`.
- Do not create planning or notes markdown in the repo, except this `claude/` folder when asked.
- Check outside advice against the code and the uploaded docs before adopting it. Some of it has been wrong.
- Uploaded reference docs are not in the repo and are copyrighted. Local copies: the Apple firmware reference text (353 pages), the IIgs hardware reference PDF (text), and GSHARDWAREREF.pdf (scanned, no text layer). Extract text with pypdf in a venv, not system pip.
