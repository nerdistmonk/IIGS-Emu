# TODO

## Release blockers (July 2026 code review)

- [ ] **Stuck modifiers on focus loss** (`sdl_driver.c` event switch): no `SDL_EVENT_WINDOW_FOCUS_GAINED/LOST` handling, so window switching (e.g. Alt+Tab) leaves Open-Apple latched in the emulated ADB. Call `adb_mainwin_focus(1/0)` + `adb_kbd_repeat_off()` and send key-ups for modifier codes 0x37/0x3a/0x36/0x38 on focus lost (cf. `xdriver.c:1124` in the original KEGS tree).

## Bugs (pre-release, lower urgency)

- [ ] Blur breaks on the software renderer (`sdl_driver.c:1139-1144`): `SDL_ComposeCustomBlendMode` is reportedly unsupported there, so `-nohwaccel 1` + blur gives a dark picture. Not yet reproduced. Check the `SDL_SetTextureBlendMode` return and fall back to a plain copy.
- [ ] `-cfg missingdir/foo.gsp` silently autocreates `./foo.gsp` in the CWD when the `chdir` fails (`config.c:1469-1491`, reproduced) — skip autocreate and keep the fatal when the directory component doesn't resolve. Also `stat`/`S_IFDIR`-check `-cfg` pointing at an existing directory (currently prints "Could not read config.kegs" and keeps running).
- [ ] Window position never persists: no `SDL_EVENT_WINDOW_MOVED` handler, so the saved "Main Window X/Y position" config vars never update (cf. `xdriver.c:1208` in the original KEGS tree calling `video_update_xpos_ypos()`).
- [ ] Hardening: unchecked 5 MB `calloc` (`sdl_driver.c:492`); `localtime()` NULL → `strftime` UB in the screenshot path (`sdl_driver.c:989`); `printf("%s", NULL)` risk on renderer/gamepad names (`sdl_driver.c:540`, `:743`); `st_size` truncated to `int` in `cfg_guess_image_size()` (`config.c:2671`).
- [ ] Drag-and-drop misroutes ~230 KB 5.25" WOZ images into slot 5 — sniff the WOZ INFO chunk disk type before the size guess (`config.c:2662-2690`).

## Pre-release polish

- [ ] README: remove the stale `-novsync` flag (`README.md:34`); note the Debian runtime package name is `libsdl3-0`.
- [ ] Stale comments: "Shift+F12" screenshot references (`sdl_driver.c:84`, `:1317` — actual key is F10, `sdl_driver.c:891`); "MILESTONE STATUS (Phase 2a)" banner at `sdl_driver.c:18`.
- [ ] Repo hygiene: root `.gitignore` should ignore `build/` and `.DS_Store`.
- [ ] Version-string consistency: `sim65816.c:581` prints `argv[%d] = %s` debug noise on every launch; `g_kegs_version_str` (`sim65816.c:86`, "1.38") differs from `GSPLUS_VERSION_STR` ("1.38.0"); unify the fallbacks (`sim65816.c:20` "1.38.0" vs `sdl_driver.c:106` "dev").
- [ ] Small style fixes: revert the extra tab on `CFGTYPE_INT`/`CFGTYPE_STR` in `config.h:23,27`; rename `SDL_MAX_WIDTH/HEIGHT` macros (`sdl_driver.c:111-112`) to `GSPLUS_` (reserved prefix); reword the VOC comment at `moremem.c:534` after confirming what loop index `i` selects; set `g_audio_enable = 0` when SDL audio init fails (`sdl_snd_driver.c:52-69`) so the core skips dead mixing work.
- [ ] Optional perf: skip the clear/draw/present in `sdl_update_display` when no rects changed and no effect/expose needs a redraw (full-window blit at 60 fps on the software renderer).

## Backlog

- [ ] Remove `#ifdef INCLUDE_RCSID_C` / `#endif` guards from header files and `#define`/`#undef INCLUDE_RCSID_C` from `sim65816.c` and `sound.c` in `gsplus/src/`. The `const char rcsid_` lines were already removed; these are the leftover scaffolding.
