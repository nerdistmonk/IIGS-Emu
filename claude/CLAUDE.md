# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

IIGS-Emu is a Linux Apple IIgs emulator based on GSplus, which derives from KEGS (Kent's Emulated GS) by Kent Dickey. It emulates the 65816 CPU, all Apple IIgs graphics/sound modes, disk controllers, serial ports, and more. Licensed under GPLv3.

## Repository Structure

- `src/` — Active source code and build files (this is where you build). Also holds `gsplusicon.png` and the `make_icons` script that generates `gsplus_icon.h`

The `upstream` remote points at digarok/gsplus; `main` is the primary development branch.

## Build Commands

Linux is the only supported platform. From the repository root:
```bash
cmake -G Ninja -B build -S src
cmake --build build --target gsplus-sdl
```
Produces `build/iigs-emu`. SDL3 is used if installed, otherwise CMake fetches and builds it. Ubuntu 24.04 has no SDL3 package, so CMake fetches SDL3 `release-3.4.10` there. The apt packages for the full test setup are listed in `handoff.md` (Setup and run).

### Debug build (outside the repo)
```bash
cmake -G Ninja -B /root/build-dbg -S /root/IIGS-Emu/src -DCMAKE_BUILD_TYPE=Debug
cmake --build /root/build-dbg -j4
```
Produces `/root/build-dbg/iigs-emu`. The directory does not exist until configured. Use `cmake --build`; there is no Makefile.

### Clean
```bash
rm -rf build
```

## Architecture

### CPU & Core Loop
- `sim65816.c` — Main simulation loop, event scheduling, interrupt handling
- `engine_c.c` + `engine.h` — 65816 CPU instruction emulation (macro-heavy for performance)
- `defs_instr.h`, `instable.h`, `op_routs.h` — Instruction definitions, opcode table, operation macros

### Memory
- `moremem.c` — Memory management, page table fixup, I/O register reads/writes ($C000-$C0FF area)
- Page-table-based MMU for dynamic address mapping
- Host-only 64 KB RAM bank at `$FB0000` (`HOST_RAM_BASE` in `protos_base.h`) holds the clipboard shim and staged text. Only the clipboard shim uses it.

### Video
- `video.c` — All Apple IIgs/II graphics mode rendering (text, lores, hires, super hires)

### Audio
- `sound.c` — Sound generation (mixing, output buffering)
- `doc.c` — Ensoniq DOC 32-voice synthesizer emulation
- `mockingboard.c` — Mockingboard A card (6522 VIA + AY-8913)

### Disk I/O
- `iwm.c` — IWM disk controller (5.25" and 3.5" drives, nibble-level accuracy)
- `smartport.c` — SmartPort controller for hard drive images
- `dynapro.c` — Host directory mounting as virtual ProDOS volumes
- `woz.c` — WOZ disk image format support
- `unshk.c`, `undeflate.c`, `applesingle.c` — Archive/compression format support
- Dynamic folders (`@500:/path`) mount host folders as ProDOS volumes. They cannot create new files. `dynapro_write()` in `dynapro.c` updates only blocks of existing host files, so a save to a dynamic folder silently does nothing.
- Dynamic folders need a valid `.applesingle` file name for forked files. The name mapping is `dynatype_fix_unix_name()` in `dyna_type.c`.
- AppleSingle reader: `applesingle_from_unix`, called from `dynapro_file_from_unix()` when the storage nibble is 0x50 (forked). Writer: `applesingle_map_from_prodos`, called from `dynapro_map_file()` for storage type 5. A guest write calls `dynapro_handle_write_file()`, which calls `dynapro_process_write_file()`. That remaps with `do_file_data=1`.
- AppleSingle v1 File Info (entry 7) is decoded only when bytes 8..13 read "ProDOS". The file type is at +11 and the aux type at +14..15. v2 ProDOS File Info (entry 11) is v2 only; v1 does not use it. AppleSingle fields are big-endian; use `applesingle_get_be32` and `applesingle_get_be16`.
- The writer emits v2 with entries 11 (ProDOS info), 9 (Finder info, when present), 1 (data fork), and 2 (resource fork). It drops entries 3 and 4 and v1 dates. This is documented in a comment above `applesingle_map_from_prodos`.
- Dynamic folder config errors are truncated at `CFG_ERR_BUFSIZE` (256 bytes, defined in `config.c`).
- WOZ: a `#` on woz-525 is an expected TMAP rejection. SmartPort images (woz-short) are refused silently by design.
- Drag-and-drop (`cfg_inspect_maybe_insert_file`, `config.c`): WOZ files route by the INFO chunk disk type (byte 21: 1 = 5.25" to slot 6, 2 = 3.5" to slot 5). Other files use the size guess.

### Input
- `adb.c` — Apple Desktop Bus (keyboard, mouse); accepts only IIgs keycodes. While the config panel is open, `adb_panel_*` feeds the menu from a separate key queue and IIgs input is blocked. Host text queued for keystroke paste (`g_kbd_paste_buf`) is fed one key per `PASTE_FRAMES_PER_KEY` (4) VBL frames (`PASTE_FRAMES_PER_KEY` in `adb.c`; gate in `adb_paste_update_state()`), which is 15 keys/s. Pacing is needed because `g_vbl_count` (`sim65816.c`, incremented in `run_16ms()`) advances on wall-clock time, so unpaced keys arrive at about 60/s. The guest keyboard routine reads `$C000` once per frame, so slow guests drop characters at that rate. Live host typing bypasses pacing.
- `input_map.c` + `input_map.h` — SDL-free host-to-IIgs mapping (keys, mouse buttons, mouse wheel, gamepad to ADB keys, mouse, paddles, BUTN, and emulator actions), loaded from `input.map`. Wheel notches are `wheel:UP`/`wheel:DOWN` sources (`imap_mouse_wheel()`, defaults `adb:0x3e`/`adb:0x3d`). Each notch is one press and release, or it captures the direction during a remap. The Ctrl+F1 config menu's Control Configuration > Remap Controls (`config.c`) edits it through the `imap_remap_*` API; saving rewrites the file and drops comments.
- `paddles.c` — Game port paddles and push buttons
- `scc.c` + `scc_socket_driver.c` — Serial Communications Controller with TCP/IP modem emulation
- `filewriter.c` + `filewriter_font.h` — FileWriter virtual printer (`SCC_STATE_FILEWRITER` = 4). `scc_add_to_writebuf()` hands bytes to `filewriter_put()` and skips `out_buf`. Each page is written as a 1-bit greyscale `.png` (`write_png_gray1()` in `png_write.c`) and, if the page has ASCII text, a Mac Roman `.txt` with CR line endings. Bitmap-only pages (ESC S/G) get no `.txt`. Glyphs are FreeMono 7×9 (GPL-3+ with the Font Exception). Do not use `kegsfont.h`, which is Apple's character ROM. Pages flush on FF, page end, port close, `filewriter_flush_all()` at normal quit (`sdl_driver.c`), and at any `exit()` (`fw_atexit`, registered by `filewriter_open()`, skipped in forked children). SIGTERM reaches normal quit through SDL. Crashes and SIGKILL still lose an unflushed page.

### Clipboard
- `clipboard.c` — Host clipboard bridge; no SDL code. Host hooks are registered in `sdl_input_init()` (`sdl_driver.c`). It observes GS/OS ZeroScrap and PutScrap through the Toolbox hook `clip_toolbox_call()`, which the JSL and JML handlers in `instable.h` call. It covers `$E10000` and `$E10004` (DISPATCH1 and DISPATCH2), not the `$E1000x` user vectors.
- `clipboard_shim.asm` — 65816 shim that runs at `$FB0000`. Its 159-byte image is `clip_shim_code[]` in `clipboard.c`. Assemble with `64tass -b -o clipboard_shim.bin clipboard_shim.asm`. The default `-a` output is 161 bytes and does not match.
- `$FB0000` is a 64 KB host-only RAM bank (`HOST_RAM_BASE` in `protos_base.h`; allocated in `setup_pageinfo()`, `moremem.c`). Only the shim uses it, and shim calls are never redirected or published. Shim parameters: LEN word at host RAM offset `$110`, tool number at `$114`, staged text at `$1000` (max `0xe000`).
- Guest reads: GetScrap, GetScrapHandle, and GetScrapSize (text type 0) are redirected to the shim when host text differs from the last sync. The shim runs ZeroScrap, then PutScrap of text staged at `$FB1000`, then the real tool with the caller's parameters. Calls through `$E10000` and `$E10004` (JSL or JML) are redirected; `$E1000x` user vectors are not covered (see `handoff.md`). AppleWorks GS Edit>Copy and Edit>Paste do not use the Scrap Manager, so the bridge does not see them.
- Guest writes: ZeroScrap clears the observed scrap. Text PutScrap appends to it and publishes to the host. Guest → host: CR → LF, other control characters dropped. Bytes $80–$FF are Mac Roman and become UTF-8. Host → guest: LF → CR, tabs → spaces, control characters dropped. UTF-8 non-ASCII becomes Mac Roman, and characters with no Mac Roman form become `?`. Ctrl+Shift+V (keystroke paste) types one `?` per non-ASCII UTF-8 character (`clip_paste_text`). Ctrl+Shift+C (text-page copy) turns MouseText cells (alt charset on, codes $40–$5F) into `?` (`cfg_text_screen_char` in `config.c`).
- Tool numbers are `$FFTT` (tool set `$16`): ZeroScrap `$0B16`, PutScrap `$0C16`, GetScrap `$0D16`, GetScrapHandle `$0E16`, GetScrapSize `$0F16`. `$0816` is invalid and returns `$16FF`. Sources: ORCA-C `scrap.h` and `mrkite/regs/iigs/scrap.txt`.
- Ctrl+Shift+C and Ctrl+Shift+V are the default `copy` and `paste` actions (`g_default_map` in `input_map.c`), dispatched in `sdl_imap_host_action()` (`sdl_driver.c`).
  - Copy: `sdl_clip_copy_text_screen()` (`sdl_driver.c`). Text (TEXT on) copies all 24 rows. MIX (TEXT off, MIX on) copies only the bottom four rows. SHR blocks copy because it overrides TEXT on screen. Otherwise the OSD says Copy works in text mode. Page selection matches the renderer: PAGE2 picks page 2 only when 80STORE is off (`cfg_text_screen_str()` in `config.c`).
  - Paste: `sdl_clip_paste_host()` → `clip_paste_text()` → `adb_paste_add_buf()`. It types the text as paced keystrokes, so it reaches any app that reads `$C000`. GS/OS Edit>Paste does not use this path. It waits until Ctrl and Shift are released (`sdl_clip_paste_pending()`, `sdl_driver.c`), because GS/OS reads held modifiers during the drain.

### Configuration & Debug
- `config.c` — Runtime configuration UI, disk mounting, settings persistence (`config.kegs`)
- `debugger.c` — Built-in 65816 debugger/monitor

### Platform Drivers
The emulator core is platform-independent. Platform-specific code is isolated in the SDL driver files:

| Component | File |
|-----------|------|
| Display, input, gamepad | `sdl_driver.c` (SDL3) |
| Audio | `sdl_snd_driver.c` (SDL3) |
| Serial | `scc_unixdriver.c` |

### Key Headers
- `defc.h` — Global defines, structs, macros (included by nearly every .c file)
- `defcomm.h` — Shared defines for C and assembly
- `protos_base.h` — Function prototypes for all modules

## Build System Details

`src/CMakeLists.txt` defines the build. The core sources are listed explicitly in `GSPLUS_CORE_SOURCES`. The `gsplus_core_sdl` library is compiled with `-DSDL_AUDIO -DSDL_INPUT`, which selects the SDL audio and gamepad backends.

## Runtime Requirements

The emulator needs an Apple IIgs ROM file to run. It looks for `ROM.03` by relative name in its working directory (`g_kegs_rom_names` in `src/config.c`) and logs `Found ROM at path: ...` (in `config_load_roms()`, `config.c`). The 256 KB "ROM 3 Tenspeed Late 1988 Early 1989 v25" image boots; ROM 04 does not. Loading a config changes the working directory (the log prints `CWD is now: ...`), so the ROM link must be in that directory. Configuration is stored in `config.kegs`. Pass a config with `-cfg <file>`; config changes need an emulator restart. Input bindings are stored in `input.map` beside it, written with commented defaults on first run; `-map <file>` selects another file.

## No Test Suite

There is no automated test infrastructure. Testing is done manually by running Apple IIgs software and ROM self-tests.
