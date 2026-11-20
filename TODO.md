# TODO

## Release blockers (July 2026 code review)

- [ ] **Stuck modifiers on focus loss** (`sdl_driver.c` event switch): no `SDL_EVENT_WINDOW_FOCUS_GAINED/LOST` handling, so window switching (e.g. Alt+Tab) leaves Open-Apple latched in the emulated ADB. Call `adb_mainwin_focus(1/0)` + `adb_kbd_repeat_off()` and send key-ups for modifier codes 0x37/0x3a/0x36/0x38 on focus lost (cf. `xdriver.c:1124` in the original KEGS tree).

## Bugs (pre-release, lower urgency)

- [ ] Blur breaks on the software renderer (`sdl_driver.c:1143-1148`): `SDL_ComposeCustomBlendMode` is unsupported there, so `-nohwaccel 1` + blur gives a dark picture. Check the `SDL_SetTextureBlendMode` return and fall back to a plain copy.
- [ ] `-cfg missingdir/foo.gsp` silently autocreates `./foo.gsp` in the CWD when the `chdir` fails (`config.c:1457-1482`) — skip autocreate and keep the fatal when the directory component doesn't resolve. Also `stat`/`S_IFDIR`-check `-cfg` pointing at an existing directory (currently dies later with a misleading "Could not read config.kegs").
- [ ] Window position never persists: no `SDL_EVENT_WINDOW_MOVED` handler, so the saved "Main Window X/Y position" config vars never update (cf. `xdriver.c:1208` in the original KEGS tree calling `video_update_xpos_ypos()`).
- [ ] Hardening: unchecked 5 MB `calloc` (`sdl_driver.c:485`); `localtime()` NULL → `strftime` UB in the screenshot path (~`sdl_driver.c:993`); `printf("%s", NULL)` risk on renderer/gamepad names (`sdl_driver.c:533`, `:747`); `st_size` truncated to `int` in `cfg_guess_image_size()` (`config.c:2645`).
- [ ] Drag-and-drop misroutes ~230 KB 5.25" WOZ images into slot 5 — sniff the WOZ INFO chunk disk type before the size guess (`config.c:2633-2660`).

## Pre-release polish

- [ ] README: remove the stale `-novsync` flag (line 78); update the "AppImage is planned" note once the bundled-.so fix lands; note the Debian runtime package name is `libsdl3-0`.
- [ ] Stale comments: "Shift+F12" screenshot references (`sdl_driver.c:95`, `:1308` — actual key is F10); "MILESTONE STATUS (Phase 2a)" banner at the top of `sdl_driver.c`.
- [ ] Repo hygiene: root `.gitignore` should ignore `build/` and `.DS_Store`; delete the stray `display` file at repo root; move personal configs (`config-builder.gsp`, `config-gsos.kegs`, `config-nucleus.kegs`) out of `gsplus/src` and ignore `config-*.kegs`; decide whether to commit `docs/promo-video-script.md`.
- [ ] CI: pin `softprops/action-gh-release` to a commit SHA; add workflow-level `permissions: contents: read`; drop the stale `sdl3-driver` branch trigger.
- [ ] Version-string consistency: `gsplus -h` prints `argv[1] = -h` debug noise before usage and says "KEGS/GSplus v1.38" while the status bar says "GSplus 1.38.0" — use `GSPLUS_VERSION_STR` in both; unify the fallbacks (`sim65816.c:20` "1.38.0" vs `sdl_driver.c:118` "dev").
- [ ] Small style fixes: revert the extra tab on `CFGTYPE_INT`/`CFGTYPE_STR` in `config.h`; rename `SDL_MAX_WIDTH/HEIGHT` macros to `GSPLUS_` (reserved prefix); reword the VOC fix comment at `moremem.c:534` (says "bank 0", code handles bank $E0); set `g_audio_enable = 0` when SDL audio init fails (`sdl_snd_driver.c:52-69`) so the core skips dead mixing work.
- [ ] Optional perf: skip the clear/draw/present in `sdl_update_display` when no rects changed and no effect/expose needs a redraw (full-window blit at 60 fps on the software renderer).

## Backlog

- [ ] Remove `#ifdef INCLUDE_RCSID_C` / `#endif` guards from header files and `#define`/`#undef INCLUDE_RCSID_C` from `sim65816.c` and `sound.c` in `gsplus/src/`. The `const char rcsid_` lines were already removed; these are the leftover scaffolding.
