# TODO

## Pre-release polish

- [ ] Repo hygiene: root `.gitignore` should ignore `build/` and `.DS_Store`.
- [ ] Version-string consistency: `sim65816.c:581` prints `argv[%d] = %s` debug noise on every launch; `g_kegs_version_str` (`sim65816.c:86`, "1.38") differs from `GSPLUS_VERSION_STR` ("1.38.0"); unify the fallbacks (`sim65816.c:20` "1.38.0" vs `sdl_driver.c:106` "dev").
- [ ] Small style fixes: revert the extra tab on `CFGTYPE_INT`/`CFGTYPE_STR` in `config.h:23,27`; rename `SDL_MAX_WIDTH/HEIGHT` macros (`sdl_driver.c:111-112`) to `GSPLUS_` (reserved prefix); reword the VOC comment at `moremem.c:534` after confirming what loop index `i` selects; set `g_audio_enable = 0` when SDL audio init fails (`sdl_snd_driver.c:52-69`) so the core skips dead mixing work.
- [ ] Optional perf: skip the clear/draw/present in `sdl_update_display` when no rects changed and no effect/expose needs a redraw (full-window blit at 60 fps on the software renderer).

