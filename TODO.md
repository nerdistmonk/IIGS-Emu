# TODO

## Pre-release polish

- [ ] Repo hygiene: root `.gitignore` should ignore `build/` and `.DS_Store`.
- [ ] Small style fixes: revert the extra tab on `CFGTYPE_INT`/`CFGTYPE_STR` in `config.h:23,27`; rename `SDL_MAX_WIDTH/HEIGHT` macros (`sdl_driver.c:111-112`) to `GSPLUS_` (reserved prefix); reword the VOC comment at `moremem.c:534` after confirming what loop index `i` selects.
- [ ] Optional perf: skip the clear/draw/present in `sdl_update_display` when no rects changed and no effect/expose needs a redraw (full-window blit at 60 fps on the software renderer).

