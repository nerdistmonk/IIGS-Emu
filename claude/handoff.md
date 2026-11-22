# IIGS-Emu handoff

Read this first in a new session. It captures the working state, conventions, and open items. Restore steps are in `claude/README.md`.

## Project
- IIgs-Emu: Apple IIgs emulator forked from KEGS/GSplus, SDL3 build.
- Repo: `/root/IIGS-Emu`, remote `origin` = `nerdistmonk/IIGS-Emu`, branch `main`.
- HEAD = `origin/main` = `3875e3f` ("Rename Fatal System Errors and hide Page 2 shadow option").
- Configure: `cmake -S src -B build -G Ninja`. Build: `cmake --build build --target gsplus-sdl`. Binary: `build/iigs-emu`.

## Recent commits
- `3875e3f`: "Fatal System Errors" label. Page 2 shadow option hidden via `g_cfg_hidden_menu` (still saved in config.kegs).
- `5da4e22`: speed option 3 label is "ZipGS Accelerator".
- `8bbbf32`: "Control Configuration" and "Function Keys" labels.
- `c836b17`: Ctrl+F3 speed, Ctrl+F4 fast disk, Ctrl+F5 screenshot defaults.
- `c8b7126`: Ctrl+1 / Ctrl+2 zoom (100–200%, saved as `zoom`).
- `4fc083b`: ROM path fix and `claude/README.md` restore steps.
- `542bd5d`: F1–F15 and the six nav keys no longer type a character or hold Open Apple. F-keys use `0x10NN` instead of `0x80NN`. Comments fixed at 0x75/0x77/0x79. README paragraph added.
- `0e3fcfa`: removed an empty "Testing / infrastructure" table from `BACKLOG.md`.

## Key code facts
- `src/adb.c`
  - `g_a2_key_to_ascii[]` (~153–295). Table bytes: `[1]` is the unshifted column, `[2]` shifted, `[3]` ctrl.
  - `hard_key` is set from the unshifted column `[1] & 0xef00`. It queues a char to `$C000` only when set.
  - `adb_key_is_nonchar_extended()` (~1602) forces `hard_key = 0` for F-keys and nav keys (~1652).
  - `special = (ascii >> 8) & 0xff` is ORed into `$C025` on press and recomputed on release.
  - `adb_maybe_virtual_key_update` (~1936) → `adb_virtual_key_update` (~1979) → `adb_key_event` (~1617). Raw ADB register `adb_kbd_reg0_data` (~555) is written only when autopoll is off (~1963, ~1970).
- `$C025` (KEYMODREG) bits, per the IIgs firmware reference: 0x80 Open Apple, 0x40 Solid Apple, 0x20 modifier latch updated, 0x10 keypad key active, 0x08 repeat, 0x04 caps, 0x02 control, 0x01 shift.
- `$C061` (Open Apple) reads `adb_is_cmd_key_down()` (`src/moremem.c` ~1312). Any 0x80 in the table holds Open Apple, which is why F-keys were changed.
- `src/input_map.c`
  - Default map ~270–291. `key:PAUSE = adb:0x71` (F15) is a default binding.
  - Remap catalog ~1308–1421. `imap_remap_bind` ~1731.
  - Actions enum in `src/input_map.h` (~34); name table `g_action_names` in `src/input_map.c` (~160–178). Default-unbound actions: fullscreen, scanlines, crt, status, debug_go, warp_pointer. Bound: mouse_grab (Ctrl+F2 and middle-click), config_panel (Ctrl+F1), reset (Ctrl+F12), zoom_in (Ctrl+1), zoom_out (Ctrl+2), screenshot (Ctrl+F5), speed (Ctrl+F3), fast_disk (Ctrl+F4).

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

## Verified (emulator only, not hardware)
- F1 held: `$C061`=0, `$C025`=16, no character.
- Nav keys: no character, `$C025` bit 0x10 while held.
- Remap host F7 → adb:0x73 (Home): `$C025`=48 while held, `$C061`=0, no stray character.
- Stuck-bit check: releasing Shift before F1, and F1 before Shift, both leave `$C025` clear.
- Config panel: F-keys and nav keys do not move the menu.

## Open items
- Hardware check: whether F-keys and nav keys set `$C025` bit 0x10 on a real IIgs. The firmware reference defines bit 4 as "Keypad key active" but does not list which keys set it. Unverified.
- Backlog (`BACKLOG.md`):
  - Fast-forward/turbo (High). Ctrl+F3 already cycles speeds, including uncapped. Open: hold-to-uncap. Ask the user first.
  - Pause/resume hotkey (Med). Empty notes.
  - Integer/pixel-perfect scaling (Med). Not implemented; the renderer uses `SDL_SCALEMODE_NEAREST`.
  - Vsync re-add (Low). Vsync is forced off in `src/sdl_driver.c` ~598.
  - Clipboard (Med). Not implemented.
  - Save states (Med). Not implemented.
  - Overlay config menu (Low).
- Gamepad remapping tested only with uinput fake pads.
- ROM 04 (unreleased prototype, 256 KB) is not supported. Its size selects ROM 3, and the ROM 3 patch block in `src/config.c` (~1533) turns byte `0x357c9` (AND) into BRK, so it halts with "Halting for native break!". The user decided not to pursue it.
- Keep README.md and BACKLOG.md in sync when default hotkeys or menu labels change. The last docs pass checked them against `src/input_map.c` and `src/config.c`.

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
