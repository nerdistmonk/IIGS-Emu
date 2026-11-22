# IIGS-Emu handoff

Read this first in a new session. It captures the working state, conventions, and open items. Restore steps are in `claude/README.md`.

## Current focus: StyleWriter II printing (stopped, blocked)

- Goal: GS/OS prints to a real StyleWriter II through a Linux RS-422 USB adapter. The emulated printer is SCC port 0. Port 1 is the unix socket for 6502.
- Status: blocked. The user stopped driver patching after the last build failed ("didn't work, i think this is hopeless"). Every test shows the same pattern: the printer readies the head at handshake, then the job stops silently. No sheet has come out in any test.
- Technical notes (driver offsets, protocol, BII comparison) are in `claude2.md`, section "SWII-Driver (StyleWriter printer)".

### Serial layer (on `main`, pushed)
- `a3e08a8` handshake fix. `fd17dec` polls host serial on RR0 reads and keeps DTR/RTS at open. `295bfb0` sets speed after termios flags. The B0 bug: `change_params` in `src/scc_unixdriver.c` cleared CBAUD, so Linux hung up.
- Debug-only commits on `debug/serial-trace`, not for merge: `0fac914` (DBGTRACE printfs, `src/scc_unixdriver.c` ~189 and ~251) and `166fd2d` (guest TX trace, `src/scc.c` ~989). Remove both before any merge.

### Driver patching (on `debug/serial-trace`)
- Source: GS/OS StyleWriter driver `/SYSTEM/DRIVERS/STYLEWRITER` (type $BB, aux $0001, EOF 20047) from `/root/iigs-assets/System6_and_Games/System 604.hdv`. The image is copyrighted and gitignored. Never commit it.
- Builder: `SWII-Driver/build_swii_hdv.py`. It takes no flags. Usage: `python3 SWII-Driver/build_swii_hdv.py "/root/iigs-assets/System6_and_Games/System 604.hdv" [output.hdv]`. The default output is `SWII-Driver/SWII-Driver.hdv`. It checks the source sha (`61e9b406…`), refuses pre-patched sources, and reads back the output.
- Every build applies these patches:
  1. Status2 0x80 is masked out of the serious-error test (`AND #$D902` → `#$5902`, file 0x474D).
  2. The Ready-light alert branch is BNE → BRA (file 0x4768), so it never fires.
  3. The R rectangle frame is 9 bytes, not 10 (template 0x3C08). Uncommitted.
  4. The white 1-byte G frame gets a 00 terminator (0x3C13, length 04 → 05). Black frames are zeroed (0x3C18), so black pixels are dropped.
  5. Setup letters are `I`, then `nu`/`d<`, then `A`/`M`, so A goes last (`SETUP_OFF` 0x3AB7).
  6. The setup record is `h u d D`, replacing both `nu`/`d<` records (`II_REC_OFF` 0x3AE4). Uncommitted. This was the last test, and it failed.
- Committed builder commits (pushed to `debug/serial-trace`): `483ee16`, `78feb07`, `3050cc8`, `21e43d7`, `e5bf75d`, `b9dd75e`.
- Uncommitted: patches 3 and 6 in `SWII-Driver/build_swii_hdv.py`. The docstring does not describe patch 6 yet. Commit only on request.
- Latest build `SWII-Driver/SWII-Driver.hdv` (sha `80ff2863…`) matches the uncommitted script. Stale hdv files: `SWII-Driver-white-term.hdv` and `SWII-Driver-ready-check.hdv`. Delete only with the user's approval. All `.hdv` files are gitignored.

### Test history (user's hardware)
Traces are in `/root/.copilot/uploads/2026-11-22-ff1456d0/`. The mapping of the `upload-…` names to build numbers is inferred from upload time.

| Build | Result | Trace |
|---|---|---|
| Ready-light alert | "Ready light error again" | `serial-trace-2.log` |
| Skip-ready (default from `21e43d7`) | Hermes acts printed, nothing comes out. TX 17,018 bytes. RX `b5 d4 00 80`. About 3,700 unterminated white frames | `serial-trace-3.log` |
| Unterminated frames | Trace shows unterminated white frames, so it did not run the white-term image | `serial-trace-4.log` |
| White-term (length 04 → 05) | Nothing printed. 3,700 terminated white frames, 0 black. RX `b5 d4 00 80 a9 00 80` | `upload-cdb780c4…` |
| A-last setup | Nothing printed. Setup `I n u A`. RX `a7 d8 00 80 ab 00 80` | `upload-a1521468…` |
| R 9-byte, run twice (second at 2.8 MHz) | No sheet either time. Two identical 20,597-byte jobs, each with four full-width R bands, about 40 bulk raster G blocks, and one form feed | `upload-d82771ce…` |
| `h u d D` setup (patch 6) | "didn't work". No trace uploaded | none |

### Open leads (unverified; none confirmed as the cause)
- Setup dialect. BII sends `h u d D A` as five 1-byte writes (`bii-print-1.strace`, around lines 38430–38462). GS/OS takes its setup letters from the caller's config record at `[$0C]`. Bit 0 of `+0x18` picks `nu` vs `d<`. Bit 0 of `+0x20` picks `A` vs `M`. The driver dialog has no model setting, so there is nothing to change in GS/OS.
- Host baud. BII moves its host termios to 230400 before raster. GS/OS stays at 57600. Not checked.
- Status2 0x80. BII's own job shows status `00 81`, so bit 0x80 alone does not block BII. Patch 1 still masks it.
- R-frame content. GS/OS declares a 2880 × 3780 page and sends mostly white rows. BII's R frames are 1400 px × 256-row bands. The user's view is that the stall happens before any raster, so content is not the first problem.
- Printer-side state after the handshake is unknown.

### Next steps (need the user's decision)
1. Stop the driver line, or try one lead. Any emulator-side change (host baud, SCC timing) needs approval first.
2. Commit or drop the uncommitted builder change (patches 3 and 6, plus the docstring).
3. Remove stale hdv files and the debug commits before any merge.

### Scratch files (not in the repo, lost if `/tmp` is cleared)
- `/tmp/sw_dis.s`: driver disassembly. File offset = image address + 0xD8.
- `/tmp/disasm816.py`: 65816 disassembler.
- `/tmp/swref/`: reference protocol code (`sw_session.c`, `stylewriter.c`).
- `/tmp/tx_stream.py`: TX/RX trace parser.

## Project
- IIgs-Emu: Apple IIgs emulator forked from KEGS/GSplus, SDL3 build.
- Repo: `/root/IIGS-Emu`, remote `origin` = `nerdistmonk/IIGS-Emu`. The main branch is `main`. Printer work is on `debug/serial-trace`, which includes debug-only commits.
- `origin/main` = `295bfb0` (serial fixes on top of `3875e3f`). `debug/serial-trace` = `b9dd75e` (pushed).
- Configure: `cmake -S src -B build -G Ninja`. Build: `cmake --build build --target gsplus-sdl`. Binary: `build/iigs-emu`.

## Recent commits
- `b9dd75e`, `e5bf75d`, `21e43d7`, `3050cc8`, `483ee16`, `78feb07`: SWII builder and ignore rules (on `debug/serial-trace`).
- `295bfb0`, `fd17dec`, `a3e08a8`: serial fixes (on `main`).
- `bbaf779`: ZipGS default 8 MHz. `855c433`: on-screen display for hotkeys. `928d89c`: Character ROM settings moved to ROM File Selection. `8f5bd28`: video updates every line, 3200 Color removed. `804b413`: docs for hotkeys, speed, zoom, ROM 04.
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
- Uploaded assets (copyrighted, not in the repo): ROMs, disks, `System6_and_Games.7z`, `Apple_IIGS_Dev.tar.bz2`, `Apple_IIGS_StyleWriter_Printer_Driver.pdf`, and the BII and trace captures. The StyleWriter PDF is not yet checked against the protocol findings.
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
- Printer (StyleWriter II): stopped and blocked. See "Current focus" at the top.
- Hardware check: whether F-keys and nav keys set `$C025` bit 0x10 on a real IIgs. The firmware reference defines bit 4 as "Keypad key active" but does not list which keys set it. Unverified.
- Backlog (`BACKLOG.md`):
  - Integer/pixel-perfect scaling (Med). Not implemented; the renderer uses `SDL_SCALEMODE_NEAREST`.
  - Vsync re-add (Low). Vsync is forced off in `src/sdl_driver.c` ~598.
  - Clipboard (Med). Not implemented.
  - Overlay config menu (Low).
- Dropped by the user, do not re-add: pause/resume, fast-forward/hold-to-uncap, save states.
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
- Scripts take positional arguments only. Do not add command-line flags unless the user asks for them.
- Tell the user about every script change and why, in the reply. No silent changes.
- Do not claim the wrong build was tested. The user rejected that claim twice. State trace facts neutrally.
- Do not ask the user to change GS/OS driver or model settings. There is one StyleWriter driver, and its setup comes from runtime config.
