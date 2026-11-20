# claude2.md

Working notes for future sessions on IIGS-Emu. Complements CLAUDE.md (architecture and build). Everything here was verified at runtime or from source unless marked otherwise.

## Working Preferences (from the user)

- Work carefully and avoid breakage. Confirm each bug at runtime (ASan/UBSan, gdb) before changing code.
- Ask before any behavior change. Use `ask_user` with one question at a time.
- Commit and push only on explicit instruction. Use the git identity and `Co-authored-by` trailer from the session instructions.
- The toolchain is CMake and GCC. Do not suggest or use Clang tooling (clang-format, clang-tidy).
- Linux is the only supported platform. Do not add macOS or Windows code paths.

## Build

- Repo build (from CLAUDE.md): `cmake -G Ninja -B build -S gsplus/src && cmake --build build --target gsplus-sdl`.
- Debug build lives outside the repo at `/root/build-dbg` and is a CMake/Ninja tree. Rebuild with `cmake --build /root/build-dbg -j4`. Do not use `make`; there is no Makefile there.
- Release binary: `/root/build-normal/iigs-emu`. Debug binary: `/root/build-dbg/iigs-emu`.
- Emulator takes a config file with `-cfg <file>`. Config changes need an emulator restart.
- Stderr is noisy with `GRIND` debug lines. Filter with `grep -a -v GRIND`. ALSA and XDG_RUNTIME_DIR warnings are environmental.

## Runtime Testing

- No automated tests exist in the repo (see CLAUDE.md). Runtime checks are done with the emulator plus gdb, using configs and fixtures in `/root/iigs-test` (outside the repo).
- Headless display: `Xvfb :99` (`export DISPLAY=:99`). Check with `xdpyinfo -display :99`.
- Batch gdb pattern: `timeout 120 gdb -batch -x script.gdb --args /root/build-dbg/iigs-emu -cfg x.cfg`. End scripts with `kill` so the emulator exits.
- gdb gotcha: `$fp` is a built-in register alias (frame pointer). Use other names for convenience variables.
- Calling emulator functions from gdb works while stopped (for example `call dynapro_handle_write_file($dskp, $fileh)`). This exercises real code paths without guest I/O.
- Config gotcha: a `#` in front of an `s<slot>d<drive>` line ejects that drive. An earlier "no error" result was a false negative from this (commit 30c4661). Verify mounts before trusting results.
- Dynamic folders (`@500:/path`) mount host folders as ProDOS volumes. They cannot create new files. `dynapro_write` (dynapro.c ~1120-1190) updates only blocks of existing host files. A save to a dynamic folder silently does nothing.
- To test saves, use a blank ProDOS `.po` image (the `/tmp/mkpo.py` approach: 819200 bytes, ProDOS volume name). Check free space in the Save dialog ("N free of M K"). GS/OS volumes in these images fill quickly.

## GS/ShrinkIt and Sample Files

- The user's supplied `System_604.hdv` and `System_Addons.hdv` contain ShrinkIt. `GS.SHRINKIT` is in `/ARCHIVE.UTILS` on System_604. Both volumes are nearly full (about 52K and 67K free).
- `GS.GS.Diag.sdk` and `GSMEMTESTv20.sdk` are NuFX archives, not AppleSingle.
- ShrinkIt creates AppleSingle via the Special menu, "Create AppleSingle...". Drag from Special to the item (mousedown, mouseup). The File menu has no AppleSingle option.
- The desktop wallpaper, drawn by DeskMaker, contains fake disk-like icons. Trust only real volume icons and window contents.
- Reference: `/root/iigs-test/applesingle_real.bin` is a 130,827-byte real ShrinkIt AppleSingle v1 file. Its entries are 7 (ProDOS file info), 4 (ShrinkIt resource header/comment, all zeros in this file), 3 (real name `GS.ShrinkIt`), 2 (resource fork, 18,071 bytes), and 1 (data fork, 112,443 bytes). File type 0xB3 and aux 0xDB07 match the catalog.

## AppleSingle Code

File: `gsplus/src/applesingle.c`.

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

## Git and Commits

- Remote: origin, branch `main`. Recent AppleSingle commits: a43ae29 (read v1), 7982d49 (writer comment).
- Commit template used: short imperative subject, body explaining the why, then the `Co-authored-by` trailer.
- Only commit or push when the user says so.

## Open Items

- Whether v2 writing should preserve entries 3 and 4 on rewrite. Decided: keep current behavior and document it.
- Whether the `DBG` strings in `sdl_driver.c` should be cleaned up. Pre-existing, untouched.
- Whether Finder Info preservation (commit 30c4661) is enough for real IIGS files. Only one real sample was tested.
