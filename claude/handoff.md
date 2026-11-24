# IIGS-Emu handoff

Read this first in a new session. It captures the working state, conventions, and open items. Restore steps are in `claude/README.md`, so be sure to read it.

## Project
- IIgs-Emu: Apple IIgs emulator forked from KEGS/GSplus, SDL3 build.
- Repo: `/root/IIGS-Emu`, remote `origin` = `nerdistmonk/IIGS-Emu`, branch `main`.
- HEAD = `origin/main` = `a5ca71b` ("Fix ZeroScrap tool call number").
- Configure: `cmake -S src -B build -G Ninja`. Build: `cmake --build build --target gsplus-sdl`. Binary: `build/iigs-emu`.

## Recent commits
- `a5ca71b` Fix ZeroScrap tool call number (`$0B16`, not `$0816`)
- `f078e93` Redirect GetScrapHandle and GetScrapSize to the clipboard shim
- `20fe722` Pace keyboard paste to one key every four frames
- `492ea00` Add clipboard copy/paste with GS/OS GetScrap support
- `131a15e` Backlog: spec clipboard copy/paste
- `34f44cf` Update backlog: drop display and overlay items, confirm gamepad remap
- `3875e3f` Rename Fatal System Errors and hide Page 2 shadow option
- Run `git log --oneline -10` for anything newer.

## Key code facts
- `src/adb.c`
  - `g_a2_key_to_ascii[]` (~156–295). Table bytes: `[1]` is the unshifted column, `[2]` shifted, `[3]` ctrl.
  - `hard_key` is set from the unshifted column `[1] & 0xef00`. It queues a char to `$C000` only when set.
  - `adb_key_is_nonchar_extended()` (~1611) forces `hard_key = 0` for F-keys and nav keys (~1659, ~1827).
  - `special = (ascii >> 8) & 0xff` is ORed into `$C025` on press and recomputed on release.
  - `adb_key_event` (~1624) ← `adb_virtual_key_update` (~1989) ← `adb_maybe_virtual_key_update` (~1952). Raw ADB register `adb_kbd_reg0_data` (~558) is written only when autopoll is off (~1973, ~1980).
  - Keystroke paste: `adb_paste_add_buf()` (~1572), `adb_paste_update_state()` (~1549). Paced at `PASTE_FRAMES_PER_KEY` = 4 (line 111), so 15 keys/s. See Clipboard below.
- `$C025` (KEYMODREG) bits, per the IIgs firmware reference: 0x80 Open Apple, 0x40 Solid Apple, 0x20 modifier latch updated, 0x10 keypad key active, 0x08 repeat, 0x04 caps, 0x02 control, 0x01 shift.
- `$C061` (Open Apple) reads `adb_is_cmd_key_down()` (`src/adb.c` ~1881, used by `src/moremem.c` ~1321). Any 0x80 in the table holds Open Apple, which is why F-keys were changed.
- `src/input_map.c`
  - Default map `g_default_map` (~187). `key:PAUSE = adb:0x71` (F15) is a default binding. Copy and paste are `ctrl+shift+key:C` and `ctrl+shift+key:V` (287–288).
  - Remap catalog entries "Copy text screen" and "Paste clipboard" (Mouse and Actions, 1519–1520). `imap_remap_bind` ~1832.
  - Actions enum `enum imap_action` in `src/input_map.h` (~34); name table `g_action_names` in `src/input_map.c` (~164–180). Default-unbound actions: fullscreen, scanlines, crt, status, debug_go, warp_pointer. Bound: mouse_grab (Ctrl+F2 and middle-click), config_panel (Ctrl+F1), reset (Ctrl+F12), zoom_in (Ctrl+1), zoom_out (Ctrl+2), screenshot (Ctrl+F5), speed (Ctrl+F3), fast_disk (Ctrl+F4), copy (Ctrl+Shift+C), paste (Ctrl+Shift+V).
- `g_vbl_count` (defined `src/sim65816.c` 1016, incremented 1106) advances on wall-clock time, not per guest cycle. Unpaced keys therefore arrive at about 60/s, which is why slow guests drop characters.

## Environment
- Xorg `:1` (dummy driver) and headless sway `wayland-1`. Start with `/root/iigs-env/start-displays.sh`.
  - The script skips starting Xorg if `/tmp/.X11-unix/X1` exists. If Xorg isn't running, first remove that socket and `/tmp/.X1-lock`.
- Launcher: `/root/iigs-env/run-emu.sh x11 -cfg /root/iigs-test/work/boot-nodisk.cfg`. It runs from `/root/iigs-test`.
- Test config: `claude/configs/boot-nodisk.cfg`, copied to `/root/iigs-test/work/`.
- `/root/iigs-test/work/input.map` holds live test bindings (F6 → 0x39, APPLICATION → 0x61) and is not committed. Remap Controls rewrites it.
- Window id changes each launch: `DISPLAY=:1 xdotool search --name IIGS-Emu`.
- Screenshot: `DISPLAY=:1 import -window <id> file.png`, then view it.
- Boots to the Applesoft `]` prompt with no disk. Read registers with `PRINT PEEK(-16347)` ($C025), `PEEK(-16287)` ($C061), `PEEK(-16384)` ($C000).
- Hold a key during Return: `xdotool keydown F7; xdotool key Return; xdotool keyup F7`.
- Baseline `$C025` with no keys held is 32 (latch bit) or 0.
- System 6.0.4 for Hermes tests: `/root/iigs-test/work/boot-sys604.cfg` (`s7d1 = /root/iigs-assets/System6_and_Games/System 604.hdv`). `boot-28.cfg` adds `g_limit_speed = 2` (2.8 MHz). Details and gotchas are in `technotes.md`.
- Clipboard checks: `DISPLAY=:1 xclip -selection clipboard -o` reads the host clipboard. `xclip -selection clipboard` writes it.

## Verified (emulator only, not hardware)
- F1 held: `$C061`=0, `$C025`=16, no character.
- Nav keys: no character, `$C025` bit 0x10 while held.
- Remap host F7 → adb:0x73 (Home): `$C025`=48 while held, `$C061`=0, no stray character.
- Stuck-bit check: releasing Shift before F1, and F1 before Shift, both leave `$C025` clear.
- Config panel: F-keys and nav keys do not move the menu.
- Clipboard, harness programs (hand-written 65816): text copy and paste, PutScrap CR→LF with high-bit mask, append, hotkey isolation.
- System 6.0.4, 8 MHz, Hermes: Edit>Paste of two host lines is intact. Edit>Copy of line 1, then line 2, leaves only line 2 on the host clipboard (ZeroScrap clears).
- Keystroke paste into Hermes at 2.8 MHz: 580/580 characters matched with pacing. Before pacing, drops grew with paste rate and lower speed. At 16 MHz, rapid pastes showed no drops (user test, before pacing).

## Clipboard
- `src/clipboard.c` has no SDL code. Host hooks are registered from `sdl_driver.c` (~846). `src/clipboard_shim.asm` is the 65816 shim. `clip_shim_code[]` in `clipboard.c` is its 159-byte image (`64tass -b`).
- Ctrl+Shift+C: `sdl_clip_copy_text_screen()` (`sdl_driver.c` ~910). Text mode only. Otherwise the OSD says Copy works in text mode.
- Ctrl+Shift+V: `sdl_clip_paste_host()` (~928) → `clip_paste_text()` → `adb_paste_add_buf()`. It types the text as paced keystrokes, so it reaches any app that reads `$C000`. Edit>Paste in GS/OS does not use this path.
- GS/OS Edit>Copy and Edit>Paste: the JSL `$E10000` hook (`instable.h` 247) calls `clip_toolbox_jsl()`. ZeroScrap clears the observed scrap. PutScrap text appends to it and is published to the host. A guest GetScrap, GetScrapHandle, or GetScrapSize (text type) is redirected to the shim at `$FB0000` when host text differs from the last sync.
- `$FB0000` is a 64 KB host-only RAM bank (`HOST_RAM_BASE`, `protos_base.h` 149–150; allocated in `moremem.c` ~899). Shim calls are never redirected or published.
- Tool numbers are `$FFTT`: ZeroScrap is `$0B16`. `$0816` is invalid and returns `$16FF`. Sources: ORCA-C `scrap.h`, `mrkite/regs/iigs/scrap.txt`.
- Text conversion. Guest → host: CR → LF, other control characters dropped, 7-bit. Host → guest: LF → CR, tabs → spaces, control and non-ASCII bytes dropped.

## Open items
- Multi-line Copy from Hermes is tested only in the harness. Test it in Hermes.
- PutScrap append vs replace for the same scrap type is unconfirmed. The bridge appends. The three local Toolbox PDFs (`/root/iigs-assets`) have no PutScrap text, so check the Scrap Manager chapter (Toolbox Reference Vol. 2) if you have it.
- Other GS/OS apps' Copy and Paste are untested.
- Whether GS/OS sends every Toolbox call through `$E10000` is unverified beyond the GetScrap family used by Hermes.
- A leading `# ` was lost once in an 8 MHz keystroke paste. Not reproduced.

## Working conventions
- CMake and GCC only. Do not suggest or use Clang tooling (clang-format, clang-tidy).
- Ask before assuming design details. Use `ask_user` with one question at a time, and do not repeat a question already asked without acknowledging it.
- Comment code only where it needs clarification.
- Commit and push only on explicit request. Use the author `nerdistmonk <5949983+nerdistmonk@users.noreply.github.com>` and this trailer:
  `Co-authored-by: copilot-swe-agent[bot] <198982749+Copilot@users.noreply.github.com>`
- Keep replies concise (≤100 words unless the task needs more).
- Do not create planning or notes markdown in the repo, except this `claude/` folder when asked.
- Gemini's advice on this code was checked and partly rejected. Verify claims against the code and the uploaded docs before adopting them.
- Uploaded reference docs are not in the repo and are copyrighted. Local copies: the Apple firmware reference text (353 pages), the IIgs hardware reference PDF (text), and GSHARDWAREREF.pdf (scanned, no text layer). Extract text with pypdf in a venv, not system pip.
- Earlier session checkpoints lived under `/root/.copilot/session-state/…/checkpoints/`. They are local and may not be present in a new session.
