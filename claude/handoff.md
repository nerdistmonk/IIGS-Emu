# IIGS-Emu handoff

Read this first in a new session. It covers repo state, setup and run, test gotchas, verified results, open items, and working conventions. Architecture and build are in `claude/CLAUDE.md`.

## Resume here
- Status: clipboard bridge works for ASCII text. Copy and paste with WordPerfect, Hermes, and System 6 are verified (see Verified below).
- Non-ASCII on Edit>Copy/Paste is handled: guest bytes $80–$FF are Mac Roman, converted to and from UTF-8 in `clipboard.c`. Verified in Hermes (é ä ñ ç). Ctrl+Shift+V types one `?` per non-ASCII character, and waits for Ctrl and Shift to be released first (see `claude/CLAUDE.md`, Clipboard).
- FileWriter virtual printer is committed through `902000d`. History: `cde0f91` (printer), `e9e3ac7` (ESC l CR insertion), `7fd32ad` (skip `.txt` for pages with no text), `747cb72` (flush on exit), `4206152` (page length and ESC c), `6c5cca2` (glyph carry across page breaks), `902000d` (save the in-progress page after 1 s idle). The user's build prints correctly from Hermes. Status and gaps are under "FileWriter status" below.
- Dropped by the user: the right-mouse bind discussion. Do not raise it again.
- Open items are under "Open items" below. The user has no open request right now; wait for the next task.
- Sandbox: no emulator or displays are running. Start displays with `/root/iigs-env/start-displays.sh` and stop them with `/root/iigs-env/stop-displays.sh`. Test scripts and PNG compare tools are in `/root/iigs-test/work`, outside the repo.
- Working tree: clean at `902000d`. Commit and push only on request.
- Recent commits: `902000d`, `6c5cca2`, `4206152`, `747cb72`, `1d9cd03`. Run `git log --oneline -10` for the full list.

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
  - Mouse wheel: `wheel:UP = adb:0x3e` and `wheel:DOWN = adb:0x3d` are default bindings. `imap_mouse_wheel()` sends one press/release per notch, or captures the direction when a remap is pending. The config panel ignores wheel input.
  - Remap catalog `g_remap_items[]` entries "Copy text screen" and "Paste clipboard" (group `IMAP_RG_MOUSE`). `imap_remap_bind()`.
  - Actions enum `enum imap_action` in `src/input_map.h`; name table `g_action_names` in `src/input_map.c`. Default-unbound actions: fullscreen, scanlines, crt, status, debug_go, warp_pointer. Bound: mouse_grab (Ctrl+F2 and middle-click), config_panel (Ctrl+F1), reset (Ctrl+F12), zoom_in (Ctrl+1), zoom_out (Ctrl+2), screenshot (Ctrl+F5), speed (Ctrl+F3), fast_disk (Ctrl+F4), copy (Ctrl+Shift+C), paste (Ctrl+Shift+V).
- `g_vbl_count` (defined in `src/sim65816.c`, incremented in `run_16ms()`) advances on wall-clock time, not per guest cycle. Why paste is paced: see the Input section of `claude/CLAUDE.md`.
- FileWriter printer (`src/filewriter.c`): `scc_port_open()` sets `cur_state = SCC_STATE_FILEWRITER` (4). The TX mask and 8th-bit handling bypass `g_serial_mask` for this state. `filewriter_flush_all()` runs after the main loop in `sdl_driver.c`, and `fw_atexit` runs it on any `exit()` in the owning process. A page idle for about 1 s is also saved (see Status below).

## Setup and run

The scripts use absolute paths: `/root/iigs-env`, `/root/iigs-test`, and `/root/IIGS-Emu/build/iigs-emu`.

1. Install packages. Ubuntu 24.04 has no SDL3 package, so CMake fetches SDL3 `release-3.4.10` from GitHub (needs network). `xserver-xorg-input-void` is not in 24.04:
   ```
   apt-get install -y cmake ninja-build git build-essential p7zip-full xdotool xclip imagemagick xserver-xorg-core xserver-xorg-video-dummy xauth x11-utils sway python3-venv 64tass libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxinerama-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev libegl-dev libgl-dev libgles-dev libpulse-dev libasound2-dev libpipewire-0.3-dev libsndio-dev libdbus-1-dev libudev-dev libibus-1.0-dev
   ```
2. Extract the uploaded assets. ROMs, disk images, and reference docs are not in the repo. The upload folder is `/root/.copilot/uploads/<session>/`, and its name changes each session. Find it with `ls -d /root/.copilot/uploads/*/`. Then run:
   ```
   U=/root/.copilot/uploads/<session>
   mkdir -p /root/iigs-assets
   for a in Apple_Hardware_References Apple_II_Specific_References Apple_Software_References; do tar -xjf $U/$a.tar.bz2 -C /root/iigs-assets; done
   7z x $U/System6_and_Games.7z -o/root/iigs-assets/System6_and_Games
   7z x $U/Tech_Notes_roms_and_disks.7z -o/root/iigs-assets/Tech_Notes_roms_and_disks
   cp $U/Productivity_and_Graphics.hdv /root/iigs-assets/
   ```
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
   ln -sfn "/root/iigs-assets/Tech_Notes_roms_and_disks/Apple IIGS ROM 3 Tenspeed Late 1988 Early 1989 v25.bin" /root/iigs-test/work/ROM.03
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
- The emulator rewrites the `bramN[xx]` lines on config save (`clk_write_bram()`, called from `config.c:2049`). So the work copies in `/root/iigs-test/work` drift from `claude/configs/`. Copy the repo versions again for a clean baseline.
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
- FileWriter (scratch harness, not kept): 15 scenarios, and the PNG output was checked.
- FileWriter, user's latest build, Hermes Print...: prints correctly (user report). An earlier build gave a good PNG and an empty TXT on a bitmap-only page, which led to `7fd32ad`.
- Clipboard in Hermes after the Mac Roman fix: Option dead keys gave é ä ñ ç. Copy wrote UTF-8 (C3 A9, C3 A4, C3 B1, C3 A7), and paste rendered them.
- Ctrl+Shift+V after the paste-pending fix: host `xé€ü` gave `x???`. Before the fix, host `abc` gave only `c`. With Ctrl+Shift held 2.5 s after V, `abc` typed in full.
- Text-page copy with the alternate charset on: codes `$40 $41 $C1 $61 $21` copied as `??Aa!`. With it off, `@AAa!`.
- F1 held: `$C061`=0, `$C025`=16, no character.
- Nav keys: no character, `$C025` bit 0x10 while held.
- Remap host F7 → adb:0x73 (Home): `$C025`=48 while held, `$C061`=0, no stray character.
- Stuck-bit check: releasing Shift before F1, and F1 before Shift, both leave `$C025` clear.
- Config panel: F-keys and nav keys do not move the menu.
- Clipboard, harness programs (hand-written 65816): text copy and paste, PutScrap CR→LF with high-bit mask, append, hotkey isolation.
- System 6.0.4, 8 MHz, Hermes: Edit>Paste of two host lines is intact. Edit>Copy of line 1, then line 2, leaves only line 2 on the host clipboard (ZeroScrap clears).
- Multi-line Copy from Hermes: works (user test in the guest).
- WordPerfect (GS/OS): Edit>Copy and Edit>Paste work through the bridge (user test). Ctrl+Shift+V works. Ctrl+Shift+C now shows the text-mode OSD. Its Read.Me screen is SHR with TEXT still set, so the copy used to read a stale text page ("Word.Perfect"). Fixed by checking SHR in `clip_copy_text_screen()`.
- SHR text copy is intentionally off (user decision). WordPerfect's Read.Me title is drawn with the app's own pixel code, not a Toolbox text call (no Toolbox call carried the title or body text). The ASCII title is in guest RAM (bank $01 at $8997, bank $7F at $503D), so a RAM search might find the title, but it is unverified whether RAM holds unsaved edits or the on-screen text. OCR needs new tooling (no tesseract installed).
- Keystroke paste into Hermes at 2.8 MHz: 580/580 characters matched with pacing. Before pacing, drops grew with paste rate and lower speed. At 16 MHz, rapid pastes showed no drops (user test, before pacing).
- Mouse wheel (xdotool click 4/5): each notch sends one press/release of 0x3e (Up) or 0x3d (Down). In the Remap UI, scrolling up onto Right moved the wheel bind from Up to Right, and after the panel closed, wheel up sent 0x3c (Right) and wheel down still sent 0x3d.

## Test harness gotchas

- Config: a `#` in front of an `s<slot>d<drive>` line ejects that drive. An earlier "no error" result was a false negative from this (commit 30c4661). Verify mounts before trusting results.
- Saves: dynamic folders cannot create new files, and a save to one silently does nothing. To test saves, use a blank ProDOS `.po` image (819200 bytes; no generator is kept in the repo, so create one first) and check free space in the Save dialog ("N free of M K"). GS/OS volumes in these images fill quickly.
- Slow guests: to test paste against a slow guest, set `g_limit_speed = 2` (2.8 MHz). With pacing in place, 580/580 keys matched at that speed. Drops there point to a pacing regression.
- GS/OS menus (Hermes and others) need press-and-hold. A quick click does not open them. Use `xdotool mousedown 1`, sleep about 0.8 s, move, then `xdotool mouseup 1`. In Hermes, the entry is near the bottom of the Apple logo menu.
- FileWriter in the sandbox (Hermes):
  - Open Hermes from the bottom item of the Apple menu. Type text first, or Print... does nothing.
  - Print... is in the document's own File menu (about 84,96), not the desktop File menu (greyed out).
  - Enable FileWriter in the serial config: Ctrl+F1, then Serial Port Configuration, so the printer slot is 4. Use Slot 1 (port 0, `g_serial_cfg[0] = 4`). Slot 2 (`g_serial_cfg[1]`) receives nothing from Hermes.
  - Control Panel > DC Printer: Port = Printer, Type = ImageWriter.
  - Quit with Special > Shut Down (press-and-hold). At "Save changes?", choose Discard.
  - SIGTERM flushes a buffered page normally (SDL turns SIGTERM into a quit event).
  - Mouse input needs a keypress first to focus the guest.
- Hand-written 65816 programs: keep code and data out of `$0400–$07FF` (the text page). Load them through the monitor, run with `300G`, and end with `jmp $ff69` to return to the monitor.
- gdb batch pattern: `timeout 120 gdb -batch -x script.gdb --args /root/build-dbg/iigs-emu -cfg x.cfg`. End scripts with `kill` so the emulator exits. `$fp` is a built-in register alias, so use other names for convenience variables. Calling emulator functions while stopped works, for example `call dynapro_handle_write_file($dskp, $fileh)`. This exercises real code paths without guest I/O.

## Test fixtures

- `/root/iigs-assets/System6_and_Games/System 604.hdv` (the image `boot-sys604.cfg` mounts) contains ShrinkIt as `/ARCHIVE.UTILS/GS.SHRINKIT` (type $B3, aux $DB07).
- `GS.Diag.v2.1.SDK` and `GSMEMTESTv20.SDK` (in `/root/iigs-assets/Tech_Notes_roms_and_disks/`) are NuFX archives, not AppleSingle.
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

## FileWriter status
- Committed in `cde0f91`, `e9e3ac7`, `7fd32ad`. Code: `src/filewriter.c`, `src/filewriter_font.h`, and `write_png_gray1()` in `src/png_write.c`. Serial config value 4 selects FileWriter in both serial menus (`config.c`).
- Output per page: a `.png` (pixel-faithful; 1-bit greyscale when only black ink printed, RGB when any color ribbon printed) and a `.txt` (Mac Roman, CR line endings; `filewriter.c:12`). The `.txt` is written only if the page has ASCII text. Bitmap-only pages (ESC S/G, such as Hermes printouts) get just the `.png`. The canvas is 1360×1584 px (8.5×11 in at 160×144 dpi). The 8th bit is included by default.
- Implemented: printable text, CR, LF, FF, BS, and CTRL-_ n feed. ESC pitch (n N E e q Q p P; proportional uses 8-dot cells). ESC T L F H D Z R G S g V A B f r c l. ESC l 0 turns CR insertion on (default), ESC l 1 turns it off. Values other than 0 or 1 are ignored.
- Tab stops (`fw_tab_*`, `fw_list_byte`): `ESC ( aaa,…,nnn.` clears all stops and sets the list. `ESC u nnn` adds one. `ESC ) aaa,…,nnn.` clears the listed stops. `ESC ~` clears all. TAB moves to the next stop, and does nothing with no stop ahead. Up to 32 stops, stored in inches at entry: column k is at (k-1)*8/dpi from the paper's left edge, using the pitch in force then. Values outside 1..dpi and a 33rd stop are dropped. A malformed list is ignored through its period. ESC c clears stops, and so does power-off. Verified with an ASan/UBSan unit test (scratch `fw_tabtest.c`, not committed). Checked on a printed page: the TXT and PNG from a tab run (stops at 10/30/60, then `ESC )` removing 30) put text at the expected columns. A WordPerfect byte stream with no tab bytes gave output identical to the pre-tab commit `9a602f6`. These are sandbox results, not the user's build.
- Tab stops per the manual: Chapter 5 says `ESC (` clears all previously-set tab stops, so `ESC (.` with no values clears all. Table A-2 lists horizontal tabs as cleared on soft reset, so ESC c clearing stops matches it. Tab positions are stored as absolute inches, as the manual requires when pitch or margin changes later. Proportional modes (ESC p/P) keep the 8-dot cell, so columns go up to 144/160. The manual caps them at 072/080, but the user chose the 8-dot cell.
- Boldface: ESC ! starts it and ESC " stops it. Printer reset clears it. Bold is drawn as a one-canvas-dot overstrike of each glyph column (`fw_text`). This is my approximation, not checked against a real printer. In the WordPerfect READ.ME stream the header and the Read.Me and LaserWtr.Help runs are bold. Before the lowercase redraw below, the PNG differed from HEAD only on those rows. Verified in the sandbox with the harness.
- ESC a n (print quality) is consumed and ignored. Before this, `ESC a` was unknown, so its parameter printed as a stray glyph (a "2" in the WordPerfect header).
- ESC s n (proportional dot spacing, one digit) is consumed and ignored, so its digit no longer prints. Proportional spacing is not applied; text keeps fixed cells. ESC m n is also one digit, read and ignored, and it turns NLQ off like ESC a 0. The manual conflicts on ESC m (Table 4-6 says insert n spaces; the summary table says ESC a 0). The user chose ESC a 0 behavior. Verified with the ASan feature tests and a synthetic stream.
- Custom characters: `ESC -` (8-dot max, power-on default) and `ESC +` (16-dot max) start a load. Each load is KEY, WIDTH, and column DATA bytes, ended by CTRL-D. `ESC '` or `ESC .` selects custom mode. `ESC S` after `ESC ' key` prints one symbol. `ESC *` maps 32–111 to keys 160–239. `ESC $` returns to standard. An unloaded key prints a blank cell. A character wider than the max is skipped, but its data is still read. Both ESC - and ESC + erase all loaded characters. ESC c keeps them. Loading bypasses the 8th-bit mask. Assumptions, not checked against the manual: a skipped key keeps any earlier definition, and an invalid width code takes no data. Verified with the ASan feature tests (scratch `fw_feattest.c`), not on hardware.
- Double width: CTRL-N turns it on and CTRL-O turns it off. ESC c clears it. Each dot column prints twice, so glyphs are twice as wide. The transcript keeps one character per cell. Checked with the ASan feature tests and the harness.
- Underline: ESC X turns it on and ESC Y turns it off. NLQ: ESC M or ESC a 2 turns it on, and ESC m or ESC a 0/1 turns it off. NLQ is a double strike of the existing glyphs, not the real 16×18 font. Bold and NLQ each add a one-canvas-dot strike, so both are approximations.
- MouseText: ESC & turns it on and ESC $ turns it off. Codes 0x40–0x5F use the KEGS glyphs in `fw_mousetext` (`filewriter_font.h`). High-ASCII MouseText (ESC Z) is not implemented, by the user's decision. MouseText cells are blank in the transcript.
- Ribbon: ESC K 0–6 selects the ink (Table 8-6). Digits 7–9 are ignored, and ESC c resets to black. Overprinted inks multiply, so K4 (orange, Y+M) renders as red (236,0,0), an approximation. There is no ribbon detection, so any ESC K prints in color. A color page is written as RGB, and a black-only page keeps the 1-bit PNG.
- Flush: pages flush on FF, page end, port close, normal quit (`filewriter_flush_all()`, `sdl_driver.c`), and any `exit()` (`fw_atexit`, `filewriter.c`).
- Crash protection: `filewriter_tick()` runs once per frame from the main loop (`sdl_driver.c`). After `FW_IDLE_FRAMES` (60, about 1 s) with no byte, the in-progress page is saved under a stable stem (`fw_write_page`). Later saves of the same page overwrite the same PNG and TXT, so FF or page end replaces the snapshot. Verified at runtime: `ABC\r` saved after the pause survived SIGKILL, and `DEF` plus FF rewrote the same files.
- User-verified on their build: Hermes Print... produces a page. Sandbox runs never flushed a page (cause unknown, sandbox-only).
- ESC H and ESC c: when the head is already past the new page length, the page ends at its old length, so text above the head is kept (`fw_set_page_len`). Verified at runtime. ESC c also restores CR/LF/FF printing (`fw_soft_reset` sets `FW_REG_A_PRINT_CMDS`, manual Table A-2); verified at runtime with ESC Z 0x40 0x00, ESC c, then LF, which now gives `A\rB\r`. ESC c leaves the 8th-bit setting as Include (decided by the user, not Ignore as the manual says), so accented Mac Roman bytes are kept. The W glyph's left diagonal had a one-row gap at row 2, and V had a break in row 3. Both are repaired (see the glyph notes below).
- A glyph that crosses the page bottom is split. The part below the edge is kept in spare canvas rows and carried to the top of the next page (`fw_page_end`, `fw_spill_rows`). Verified at runtime on a 490-row page (`ESC H 0490`): the D's bottom bar appears at rows 2–3 of page 2, and the D has no ink in dot rows 7–8.
- Digit tops: row 0 of 0, 2, 3, 5, 7, 8 and 9 was missing strokes, so the tops of printed digits looked cut off. Those rows are now hand-edited (`filewriter_font.h`). Verified with the glyph ASCII render and the harness-rendered PNG (not the emulator). Replaying WordPerfect's port-0 byte stream through the old and new FileWriter gives identical TXT, and the PNG differs only in the top dot row of the changed digits. Glyph 6 had a thin top. It is now redrawn with a full top curve (row 0 = `0x1c`), and V, W and the tilde were also redrawn (see below).
- Lowercase a-z (0x61-0x7A) were redrawn by hand in the 7×9 cell. The new x-height is rows 2–6 (5 rows; HEAD used rows 1–6), ascenders start at row 0, and descenders reach rows 7–8. Accented lowercase (0x87–0x9F and 0xD8 ÿ) was redrawn the same way: the base letter with the accent in rows 0–1, the cedilla in rows 7–8, and the i's dot replaced by its accent. The tilde is a two-row wave. Verified with ASCII dumps, an ASan harness render, and the WordPerfect byte stream (sandbox, not the emulator's print).
- Accented capitals 0x80–0x81 and 0x83–0x86 (Ä Å É Ñ Ö Ü) draw their accent in row 0 above a 6-row capital in rows 1–6. Before this, É, Ñ, Ö and Ü had no accent at all, and Ä and Å were a plain A. The bodies are the FreeMono capital with one duplicate row removed. Ç (0x82) is unchanged: a 7-row C with a cedilla in rows 7–8. 0xDB draws ¤ (was €), hand-drawn. Verified with ASCII grids and the ASan harness, not on a real printer or with `fw-glyph.gdb`. Hermes does not use this table. Its Standard and Text Only prints are guest-font bitmaps, so Ä/Å there come from the guest font, and $DB shows as "?". The `.txt` for the glyph run holds the bytes `41 80 81 DB 0D` (Mac Roman).
- A carry that is still pending at port close or exit is written as its own page (`fw_flush_port`, which loops `fw_page_end` while dirty). Verified at runtime: a second straddle at exit produced a carry-only page 3 holding the bar at rows 2–3.
- After FF, the carried part sits at the top of the next page, so text printed right after FF overlaps it. The user chose to keep this.

## Open items
- PutScrap appends unless ZeroScrap runs first. Toolbox Reference Vol. 2, p.346 says so, and the bridge matches it. Vol. 1 and 2 PDFs are only in the upload folder (see Working conventions).
- Open: FileWriter ESC c on the user's build is untested. In the sandbox, WordPerfect 2.1 (Apple ImageWriter II driver, Printer #1 in slot 1 = port 0) sends ESC c at job start (`0D 1B 63 1B 4F 1B 3C 1B 5A 20 00`) and mid-job (`0D 1B 63 1B 41`). This was seen with a gdb byte log on `filewriter_put` (sandbox only). The ESC c hits in `/PRODUCTIVITY/WORD.PERFECT/WP.PRINTER` on `Productivity_and_Graphics.hdv` are the driver's command data.
- No Toolbox reference states the TEXT scrap encoding (Vol. 2 pp.341–357). None mentions Mac Roman. The Mac Roman reading rests on the GS/OS Reference's extended-ASCII note (bytes ≥128, high bit significant) and on the Copy bytes seen at runtime (`$8E` for é).
- Other GS/OS apps' Copy and Paste are untested.
- The Toolbox hook (`clip_toolbox_call()`, called from the JSL and JML handlers) covers `$E10000` (DISPATCH1) and `$E10004` (DISPATCH2). Runtime: PutScrap via JSL `$E10004` and JML `$E10000` published the 135-byte table to the host; GetScrapSize via JML `$E10000` and JSL `$E10004` returned the staged host length (7 for `Héllo ü`, 6 for `Café ñ`). Not covered: the `$E1000x` user dispatch vectors (UDISPATCH, `$E10008`/`$E1000C`). Static scans show them in MAZER.II, but whether they carry Scrap calls is unverified. AppleWorks GS uses other `$E1` entries (`$E10064`, `$E10068`), but none carried Scrap Manager calls during Edit>Copy or Edit>Paste.
- AppleWorks GS Edit>Copy and Edit>Paste do not use the Scrap Manager (no `$16` calls on any `$E1` entry, with Cmd+C and Cmd+V). Out of scope by decision. Ctrl+Shift+C only copies in text or MIX mode.
- A leading `# ` was lost once in an 8 MHz keystroke paste. Not reproduced.
- FileWriter: the idle save does not cover continuous printing. With no 1 s gap between bytes, a crash or SIGKILL still loses the page. The user chose not to add a periodic save.
- FileWriter sandbox: a Hermes print never flushed a file in the sandbox. The user's build works. Dropped by the user.

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
- Reference docs are copyrighted and are not in the repo. The user confirmed that all uploaded PDFs have OCR text layers. Local copies under `/root/iigs-assets/`:
  - `Apple Hardware References/`: `Apple_IIgs_Hardware_Reference_HiRes_text.pdf`, `IIgs_2523030_Hardware_Ref.pdf`, `apple_iigs_toolbox_reference_volume_3.pdf`, the ADB spec, and others.
  - `Apple Software References/`: `Apple_IIgs_Firmware_Reference_text.pdf`, `Apple IIgs GS_OS Reference.pdf.txt`, `TheAppleIIGSToolboxRevealed.pdf`, and others.
  - Toolbox Reference Vol. 1 and 2 and the ImageWriter II manual are only in the upload folder (`/root/.copilot/uploads/<session>/`).
  - Extract text with pypdf in a venv (`/root/iigs-env/pdf-venv` exists), not system pip.
