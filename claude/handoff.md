# IIGS-Emu handoff

Read this first in a new session. It covers repo state, setup and run, test gotchas, verified results, open items, and working conventions. Architecture and build are in `claude/CLAUDE.md`.

## Resume here
- Status: the clipboard bridge handles ASCII and Mac Roman text (`clipboard.c`). Copy and paste are verified in WordPerfect, Hermes, and System 6. Ctrl+Shift+V types one `?` per non-ASCII character, and waits for Ctrl and Shift to be released first (see `claude/CLAUDE.md`, Clipboard).
- FileWriter virtual printer: the user's build prints correctly from Hermes and WordPerfect. Behavior and gaps are under "FileWriter status" below.
- Dropped by the user: the right-mouse bind discussion. Do not raise it again.
- Open items are under "Open items" below.
- Sandbox: Xorg `:1` may still be running from an earlier session. Start displays with `/root/iigs-env/start-displays.sh` (it skips Xorg if `:1` is up) and stop them with `/root/iigs-env/stop-displays.sh`. Test scripts and PNG compare tools are in `/root/iigs-test/work`, outside the repo.

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
- FileWriter printer (`src/filewriter.c`): `scc_port_open()` sets `cur_state = SCC_STATE_FILEWRITER` (4). The TX mask and 8th-bit handling bypass `g_serial_mask` for this state. `filewriter_flush_all()` runs after the main loop in `sdl_driver.c`, and `fw_atexit` runs it on any `exit()` in the owning process. A page idle for about 1 s is saved, and a job idle for about 5 s ends (see FileWriter status below).

## Setup and run

The scripts use absolute paths: `/root/iigs-env`, `/root/iigs-test`, and `/root/IIGS-Emu/build/iigs-emu`.

1. Install packages. Ubuntu 24.04 has no SDL3 package, so CMake fetches SDL3 `release-3.4.10` from GitHub (needs network). `xserver-xorg-input-void` is not in 24.04:
   ```
   apt-get install -y cmake git build-essential p7zip-full xdotool xclip imagemagick xserver-xorg-core xserver-xorg-video-dummy xauth x11-utils sway python3-venv 64tass libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxinerama-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev libegl-dev libgl-dev libgles-dev libpulse-dev libasound2-dev libpipewire-0.3-dev libsndio-dev libdbus-1-dev libudev-dev libibus-1.0-dev
   ```
2. Extract the uploaded assets. ROMs, disk images, and reference docs are not in the repo. The upload folder is `/root/.copilot/uploads/<session>/`, and its name and archive names change between sessions. Find the folder with `ls -d /root/.copilot/uploads/*/`, list it, and extract each file into `/root/iigs-assets/`:
   - `.tar.bz2`: `tar -xjf <file> -C /root/iigs-assets`
   - `.7z`: `7z x <file> -o/root/iigs-assets`
   - Disk images (`.hdv`, `.po`, `.2mg`): copy into `/root/iigs-assets/`
3. Copy the scripts, make them executable, and create the venv that `fakepad.py` needs:
   ```
   mkdir -p /root/iigs-env /root/iigs-test/work
   cp /root/IIGS-Emu/claude/env/* /root/iigs-env/
   chmod +x /root/iigs-env/*.sh /root/iigs-env/fakepad.py
   python3 -m venv /root/iigs-env/venv && /root/iigs-env/venv/bin/pip install evdev
   cp /root/IIGS-Emu/claude/configs/*.cfg /root/iigs-test/work/
   ```
4. Provide the ROM. Loading a config changes the working directory to `/root/iigs-test/work`, so the link goes there, not in `/root/iigs-test`. ROM 3 is the image that boots (see `claude/CLAUDE.md`, Runtime Requirements). In the current extraction it is `ROM03.ROM` (262144 bytes):
   ```
   ln -sfn "/root/iigs-assets/Apple IIGS Software/ROM03.ROM" /root/iigs-test/work/ROM.03
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
- Clipboard: Edit>Copy and Edit>Paste work in WordPerfect (user test), Hermes, and System 6.0.4. Multi-line copy from Hermes works (user test). Two host lines pasted into Hermes stay intact.
- Copy of separate lines: Edit>Copy of line 1, then line 2, leaves only line 2 on the host clipboard (ZeroScrap clears).
- Non-ASCII: Hermes Option dead keys give é ä ñ ç. Copy writes UTF-8 (C3 A9, C3 A4, C3 B1, C3 A7), and paste renders them. Host `xé€ü` pastes as `x???`.
- Paste waits for Ctrl and Shift to be released. Before this fix, host `abc` gave only `c`. Now `abc` types in full with Ctrl+Shift held 2.5 s after V.
- Text-page copy with the alternate charset on: codes `$40 $41 $C1 $61 $21` copy as `??Aa!`. With it off, `@AAa!`.
- Copy modes: TEXT copies all 24 rows, MIX the bottom four. SHR blocks copy (user decision).
- Keystroke paste into Hermes at 2.8 MHz: 580/580 characters matched with pacing (`PASTE_FRAMES_PER_KEY`).
- Keys: F1 held gives `$C061`=0, `$C025`=16, and no character. Nav keys give no character and set `$C025` bit 0x10 while held. Remap F7 → adb:0x73 (Home) gives `$C025`=48 while held, with no stray character. Releasing Shift and F1 in either order leaves `$C025` clear. The config panel ignores F-keys and nav keys.
- Mouse wheel (xdotool click 4/5): each notch sends one press/release of 0x3e (Up) or 0x3d (Down). A Remap-UI rebind of wheel Up to Right persists after the panel closes.
- PutScrap appends to the host clipboard unless ZeroScrap runs first (Toolbox Reference Vol. 2, p.346). The bridge matches it.
- Toolbox hook: PutScrap (135-byte table) and GetScrapSize (7 for `Héllo ü`, 6 for `Café ñ`) work through `JSL $E10004` and `JML $E10000`.
- Harness programs (hand-written 65816): text copy and paste, PutScrap CR→LF with high-bit mask, append, and hotkey isolation.
- FileWriter, user's build: Hermes Print produces a page, and WordPerfect prints correctly. A WordPerfect job followed by a Print Shop job starts a new sheet. Sandbox results are under FileWriter status.

## Test harness gotchas

- Config: a `#` in front of an `s<slot>d<drive>` line ejects that drive. Verify mounts before trusting results.
- Saves: dynamic folders cannot create new files, and a save to one silently does nothing. To test saves, use a blank ProDOS `.po` image (819200 bytes; no generator is kept in the repo, so create one first) and check free space in the Save dialog ("N free of M K"). GS/OS volumes in these images fill quickly.
- Slow guests: to test paste against a slow guest, set `g_limit_speed = 2` (2.8 MHz). With pacing in place, 580/580 keys matched at that speed. Drops there point to a pacing regression.
- GS/OS menus (Hermes and others) need press-and-hold. A quick click does not open them. Use `xdotool mousedown 1`, sleep about 0.8 s, move, then `xdotool mouseup 1`. In Hermes, the entry is near the bottom of the Apple logo menu. Radio buttons and popups in GS/OS dialogs need the same slow press (mousedown, sleep about 0.15 s, mouseup). A fast `xdotool click` did not register. Print dialog fields reset each time it opens.
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
- `GS.Diag.v2.1.SDK` and `GSMEMTESTv20.SDK` are NuFX archives, not AppleSingle. They are not in the current `/root/iigs-assets` extraction, so find them in the uploads before use.
- ShrinkIt creates AppleSingle through the Special menu, "Create AppleSingle...". Drag from Special to the item (mousedown, mouseup). The File menu has no AppleSingle option.
- The desktop wallpaper, drawn by DeskMaker, contains fake disk-like icons. Trust only real volume icons and window contents.

## Test programs and tools

- `claude/harness/`: hand-written 65816 programs. Assemble with `64tass -b -o out.bin file.asm` (raw bytes at `$0300`), then load and run them as in the harness gotchas above. Each header describes its inputs and results.
  - `scrap-test9.asm`: host clipboard accumulation. Set the guest text at `$00:6000` and `$00:6008` first. After a correct ZeroScrap the host clipboard reads `BBBB`.
  - `kbd-skip-test.asm`: counts keystroke skips. Results are at `$1000–$103D`. Paste 62 characters and compare. Use `boot-28.cfg` for a slow guest.
- `claude/tools/`:
  - `mkhex.py BIN START`: prints a binary as monitor hex lines (`ADDR: XX ...`, CR-separated). START is hex.
  - `prodos_tool.py IMAGE CMD`: reads ProDOS `.po`/`.hdv` images. Commands: `ls`, `get NAME OUTFILE`, `find PATTERN`, `tools NAME...`, and the Toolbox-call scans `scan`, `scrap`, `le`.

## FileWriter status
- Code: `src/filewriter.c`, `src/filewriter_font.h`, and `write_png_gray1()` in `src/png_write.c`. Serial config value 4 selects FileWriter in both serial menus (`config.c`).
- Output per page: a `.png` (pixel-faithful; 1-bit greyscale for black-only pages, RGB when any color ribbon printed) and a `.txt` (Mac Roman, CR line endings; `filewriter.c:12`). The `.txt` is written only if the page has ASCII text. Bitmap-only pages (ESC S/G, such as Hermes) get only the `.png`. The canvas is 1360×1584 px (8.5×11 in at 160×144 dpi). The 8th bit is included by default.
- Flush: FF, page end, port close, normal quit (`filewriter_flush_all()`, `sdl_driver.c`), and any `exit()` (`fw_atexit`, `filewriter.c`).
- Idle save: after `FW_IDLE_FRAMES` (60, about 1 s) with no byte, the in-progress page is saved under a stable stem (`fw_write_page`). Later saves overwrite the same files.
- Job end: after `FW_JOB_END_FRAMES` (300, about 5 s) with no byte, the sheet is flushed and the next job starts on a fresh sheet at the top and left margin (`fw_end_job`). A pause over 5 s mid-job also starts a new sheet.
- Implemented: printable text, CR, LF, FF, BS, and CTRL-_ n feed. ESC pitch (n N E e q Q p P; proportional uses 8-dot cells). ESC T L F H D Z R G S g V A B f r c l. ESC l 0 turns CR insertion on (default), ESC l 1 off. Values other than 0 or 1 are ignored.
- ESC ? replies `IW10C` plus CR (no sheet-feeder flag, by user choice). Paint256 enables Color only after this reply. The CR is required.
- Tab stops: `ESC ( list.` clears all and sets the list. `ESC u n` adds one. `ESC ) list.` clears the listed stops. `ESC ~` clears all. TAB goes to the next stop. Up to 32, stored in inches at entry. Values outside 1..dpi and a 33rd stop are dropped. A malformed list is ignored through its period.
- ESC H and ESC c page length: when the head is already past the new length, the page ends at its old length, so text above the head is kept (`fw_set_page_len`).
- ESC c (soft reset): clears tab stops, bold, sub/superscript, double width, and the ribbon (back to black). It restores CR/LF/FF printing (`fw_soft_reset`). It keeps custom characters and leaves the 8th bit Included (user decision; the manual says Ignore).
- Bold: ESC ! on, ESC " off. Printer reset clears it. Bold draws a one-canvas-dot overstrike of each glyph column. NLQ: ESC M or ESC a 2 on; ESC m or ESC a 0/1 off. NLQ draws a double strike of the existing glyphs, not the real 16×18 font. Bold and NLQ are approximations, not checked against a real printer.
- ESC a n (print quality) and ESC s n (proportional spacing) are consumed and ignored. Proportional spacing is not applied, so text keeps fixed cells. ESC m n reads one digit and ignores it (it also turns NLQ off, as ESC a 0 does). The user chose this behavior over the manual, which conflicts on ESC m.
- Double width: CTRL-N on, CTRL-O off. Each dot column prints twice. The .txt keeps one character per cell.
- Underline: ESC X on, ESC Y off.
- Sub/superscript: ESC x superscript (top 5 dot rows), ESC y subscript (bottom 5), ESC z normal. `fw_squash()` squashes each 9-row cell to 5 rows, and a band with any ink sets its row. This is crude, and not checked against a real printer.
- Custom characters: ESC - (8-dot max, default) and ESC + (16-dot max) start a load. Each load is KEY, WIDTH, and column DATA bytes, ended by CTRL-D. ESC ' or ESC . selects custom mode. ESC S after ESC ' key prints one symbol. ESC * maps 32–111 to keys 160–239. ESC $ returns to standard. An unloaded key prints blank. A character wider than the max is skipped, but its data is still read. ESC - and ESC + each erase all loaded characters. Loading bypasses the 8th-bit mask. Not checked against the manual: a skipped key keeps any earlier definition, and an invalid width code takes no data.
- Ribbon: ESC K 0–6 selects the ink (Table 8-6). Digits 7–9 are ignored. Overprinted inks multiply, so K4 (orange) renders as red (236,0,0), an approximation. There is no ribbon detection, so any ESC K prints in color. A color page is written as RGB, and a black-only page keeps the 1-bit PNG.
- MouseText: ESC & on, ESC $ off. Codes 0x40–0x5F use the KEGS glyphs in `fw_mousetext` (`filewriter_font.h`). High-ASCII MouseText (ESC Z) is not implemented (user decision). MouseText cells are blank in the .txt.
- Glyphs are hand-drawn in `filewriter_font.h` (7×9 cell): digit tops, 6, V, W, the tilde, lowercase a–z (x-height rows 2–6, ascenders from row 0, descenders to rows 7–8), accented lowercase (0x87–0x9F, ÿ), accented capitals Ä Å É Ñ Ö Ü (0x80–0x81, 0x83–0x86), and 0xDB ¤ (was €). Ç (0x82) is unchanged. These are approximations. Hermes does not use this table: its Standard and Text Only prints are guest-font bitmaps.
- Page carry: a glyph that crosses the page bottom is split. The part below the edge is carried to the top of the next page (`fw_page_end`, `fw_spill_rows`). A carry still pending at port close or exit is written as its own page (`fw_flush_port`). After FF, the carry sits at the top, so text printed right after FF overlaps it. The user chose to keep this.
- Verification: ASan/UBSan and harness runs in the sandbox, not on hardware. The user's build results are under Verified.

## Open items
- Toolbox hook: the `$E1000x` user dispatch vectors (`$E10008`, `$E1000C`) are not covered. Static scans show them in MAZER.II, but whether they carry Scrap calls is unverified.
- Toolbox hook: AppleWorks GS Edit>Copy and Edit>Paste make no Scrap Manager calls on any `$E1` entry, so the bridge does not cover them. Out of scope by decision.
- TEXT scrap encoding is not stated in any Toolbox reference (Vol. 2 pp.341–357, and none mention Mac Roman). The Mac Roman reading rests on the GS/OS Reference's extended-ASCII note and on Copy bytes seen at runtime (`$8E` for é).
- Other GS/OS apps' Copy and Paste are untested.
- A leading `# ` was lost once in an 8 MHz keystroke paste. Not reproduced.
- Accepted: FileWriter ESC c has no direct test on the user's build. WordPerfect sends ESC c at job start and mid-job, so the user's WordPerfect prints exercise it.
- FileWriter: the idle save does not cover continuous printing. With no 1 s gap between bytes, a crash or SIGKILL still loses the page. The user chose not to add a periodic save.
- Accepted: MyWord prints black. Its Color job sends only `ESC K 0` (72 commands, all digit 0), so this is not a FileWriter bug. Paint256 Color prints RGB after the `IW10C` reply.

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
- Reference docs are copyrighted and are not in the repo. The uploaded PDFs have OCR text layers, and text copies are under `/root/iigs-assets/Reference_Materials/`:
  - `Reference Docs - Hardware/`: hardware references, the ADB spec, the ImageWriter II manual, and others.
  - `Reference Docs - Software/`: Toolbox Reference Vol. 1, 2, and 3, the GS/OS Reference, the Firmware Reference, and others.
  - `AppleTechNotes/`.
- If a new PDF needs text, extract it with pypdf in its own venv, not system pip (`python3 -m venv /root/pdf-venv && /root/pdf-venv/bin/pip install pypdf`).
