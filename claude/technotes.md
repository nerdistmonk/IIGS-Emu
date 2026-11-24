# technotes.md

Working notes for future sessions on IIGS-Emu. Complements CLAUDE.md (architecture and build). Everything here was verified at runtime or from source unless marked otherwise.

## Working Preferences (from the user)

- Work carefully and avoid breakage. Confirm each bug at runtime (ASan/UBSan, gdb) before changing code.
- Ask before any behavior change. Use `ask_user` with one question at a time.
- Commit and push only on explicit instruction. Use the git identity and `Co-authored-by` trailer from the session instructions.
- The toolchain is CMake and GCC. Do not suggest or use Clang tooling (clang-format, clang-tidy).
- Linux is the only supported platform. Do not add macOS or Windows code paths.

## Build

- Repo build (from CLAUDE.md): `cmake -G Ninja -B build -S src && cmake --build build --target gsplus-sdl`.
- Release binary: `build/iigs-emu` in the repo (`cmake --build build --target gsplus-sdl`).
- Debug tree lives outside the repo at `/root/build-dbg`. Configure it once with `cmake -G Ninja -B /root/build-dbg -S /root/IIGS-Emu/src -DCMAKE_BUILD_TYPE=Debug`, then rebuild with `cmake --build /root/build-dbg -j4`. Do not use `make`; there is no Makefile there. The debug binary is `/root/build-dbg/iigs-emu`. It does not exist until configured.
- Emulator takes a config file with `-cfg <file>`. Config changes need an emulator restart.
- Stderr is noisy with `GRIND` debug lines. Filter with `grep -a -v GRIND`. ALSA and XDG_RUNTIME_DIR warnings are environmental.

## Runtime Testing

- No automated tests exist in the repo (see CLAUDE.md). Runtime checks are done with the emulator plus gdb, using configs and fixtures in `/root/iigs-test` (outside the repo).
- Test display: Xorg `:1` from `/root/iigs-env/start-displays.sh` (see `claude/README.md`). Check with `xdpyinfo -display :1`.
- Batch gdb pattern: `timeout 120 gdb -batch -x script.gdb --args /root/build-dbg/iigs-emu -cfg x.cfg`. End scripts with `kill` so the emulator exits.
- gdb gotcha: `$fp` is a built-in register alias (frame pointer). Use other names for convenience variables.
- Calling emulator functions from gdb works while stopped (for example `call dynapro_handle_write_file($dskp, $fileh)`). This exercises real code paths without guest I/O.
- Config gotcha: a `#` in front of an `s<slot>d<drive>` line ejects that drive. An earlier "no error" result was a false negative from this (commit 30c4661). Verify mounts before trusting results.
- Dynamic folders (`@500:/path`) mount host folders as ProDOS volumes. They cannot create new files. `dynapro_write` (dynapro.c ~1120-1190) updates only blocks of existing host files. A save to a dynamic folder silently does nothing.
- To test saves, use a blank ProDOS `.po` image (the `/tmp/mkpo.py` approach: 819200 bytes, ProDOS volume name). Check free space in the Save dialog ("N free of M K"). GS/OS volumes in these images fill quickly.

## Clipboard and Paste Testing

- Scrap Manager tool numbers are `$FFTT` (tool set `$16`). ZeroScrap is `$0B16`. `$0816` is not a valid tool and returns `$16FF`. Sources: ORCA-C `scrap.h` and `mrkite/regs/iigs/scrap.txt`.
- Keystroke paste is paced by `PASTE_FRAMES_PER_KEY` in `adb.c`. `g_vbl_count` advances on wall-clock time, so unpaced keys arrive at about 60/s. A guest that reads `$C000` once per frame drops keys, and the drops grew as the emulated speed fell. To test a slow guest, set `g_limit_speed = 2` (2.8 MHz).
- GS/OS menus (Hermes and others) need press-and-hold. A quick click does not open them. Use `xdotool mousedown 1`, sleep about 0.8 s, move, then `xdotool mouseup 1`.
- `xclip` needs `DISPLAY=:1`. The window id changes on every launch: `DISPLAY=:1 xdotool search --name IIGS-Emu`. Find the PID with `ps -eo pid,args | grep iigs-emu`.
- Harness programs: keep code and data out of `$0400–$07FF` (the text page). Load code through the monitor, then run it with `300G`. End with `jmp $ff69` to return to the monitor.
- Assemble the shim with `64tass -b -o clipboard_shim.bin clipboard_shim.asm`. The 159-byte image must match `clip_shim_code[]` in `clipboard.c`. The default `-a` output is 161 bytes.

## GS/ShrinkIt and Sample Files

- The user's supplied `System_604.hdv` and `System_Addons.hdv` contain ShrinkIt. `GS.SHRINKIT` is in `/ARCHIVE.UTILS` on System_604. Both volumes are nearly full (about 52K and 67K free).
- `GS.GS.Diag.sdk` and `GSMEMTESTv20.sdk` are NuFX archives, not AppleSingle.
- ShrinkIt creates AppleSingle via the Special menu, "Create AppleSingle...". Drag from Special to the item (mousedown, mouseup). The File menu has no AppleSingle option.
- The desktop wallpaper, drawn by DeskMaker, contains fake disk-like icons. Trust only real volume icons and window contents.
- Reference: `/root/iigs-test/applesingle_real.bin` is a 130,827-byte real ShrinkIt AppleSingle v1 file. Its entries are 7 (ProDOS file info), 4 (ShrinkIt resource header/comment, all zeros in this file), 3 (real name `GS.ShrinkIt`), 2 (resource fork, 18,071 bytes), and 1 (data fork, 112,443 bytes). File type 0xB3 and aux 0xDB07 match the catalog.

## AppleSingle Code

File: `src/applesingle.c`.

- Reader: `applesingle_from_unix`. Called from `dynapro_file_from_unix` (dynapro.c ~1940-1945) when the storage nibble is 0x50 (forked file).
- Writer: `applesingle_map_from_prodos`. Called from `dynapro_map_file` (dynapro.c ~2157-2166) when the storage type is 5 (forked). It runs on a guest write via `dynapro_handle_write_file` (dynapro.c ~870-890), which remaps with `do_file_data=1`.
- v1 File Info (entry 7) is decoded only when bytes 8..13 read "ProDOS". The file type is at +11 and the aux type is at +14..15 (big-endian).
- v2 ProDOS File Info (entry 11) is v2 only. It is not a v1 feature. A Gemini claim that v1 uses entry 11 was wrong.
- Byte order: AppleSingle fields are big-endian. Reads already use `applesingle_get_be32` and `applesingle_get_be16`.
- The writer emits v2 with entries 11 (ProDOS info), 9 (Finder info, when present), 1 (data fork), and 2 (resource fork). It drops entries 3 and 4 and v1 dates. This is documented in a comment above `applesingle_map_from_prodos` (commit 7982d49). The user chose to keep this behavior.
- Verified round trip: real v1 file -> mount -> `dynapro_handle_write_file` -> v2 file. The data and resource forks are byte-identical, type and aux are preserved, and the reader parses the v2 output with type 0xB3 and aux 0xDB07.

## Dynapro and Config

- Dynamic folder config errors are truncated at `CFG_ERR_BUFSIZE` (256, config.c ~100). Raised from 80 in commit 8f96a48.
- Dynamic folders need a valid `.applesingle` file name for forked files. The name mapping is in `dyna_type.c` (~285-286).
- WOZ: a `#` on woz-525 is an expected TMAP rejection. SmartPort images (woz-short) are refused silently by design. The user declined adding a message.
- Drag-and-drop (`cfg_inspect_maybe_insert_file`, config.c): WOZ files route by the INFO chunk disk type (byte 21: 1 = 5.25" to slot 6, 2 = 3.5" to slot 5). Other files use the size guess. Verified with gdb `call` on crafted files.

## Git and Commits

- Remote: origin, branch `main`.
- Commit template used: short imperative subject, body explaining the why, then the `Co-authored-by` trailer.
- Only commit or push when the user says so.

## Open Items

See `handoff.md` for the current open items and the latest verified results.
