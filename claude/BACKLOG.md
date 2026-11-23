# IIGS-Emu Backlog

Planned and possible work for IIGS-Emu. Everything below targets the SDL3 build
(the focus — see the README). Items marked **(core)** already exist in the GSplus
core and mainly need wiring into the SDL driver; everything else is new.

Effort: **S** small · **M** medium · **L** large. Priority: High / Med / Low.

---

## Done

- Foundation: SDL3 driver (video/input/audio), CMake build,
  version-from-tag.
- **F1 — Window & display options**: `-fullscreen`, `-borderless`, `-noaspect`,
  `-highdpi`, `-nohwaccel`, window position; fullscreen action (unbound by default). (`-novsync`
  was later removed — see the vsync item under Display.)
- **F2 — Scanline simulator**: `-scanline <0-100>`, scanlines action (unbound by default).
- **F3 — Drag-and-drop**: drop a disk image on the window to mount it (WOZ slot
  from its INFO disk type; other images guessed from file size).
- **Curved CRT effect**: `-crt 1` + `-crtcurve <0-100>` + `-crtmask <0-100>`,
  crt action (unbound by default). Curvature (RenderGeometry mesh) + RGB phosphor mask + bloom +
  vignette, composes with `-scanline`. All on the 2D renderer, no extra libraries.
- **Gamepad/controller support**: SDL3 high-level Gamepad API → IIgs
  joystick, with hotplug. Buttons and sticks are bound through Remap Controls.
- **Screenshots**: the screenshot action captures the framebuffer (Ctrl+F5 by default;
  `-ssdir` sets the output directory).
- **Zoom**: Ctrl+1 / Ctrl+2 magnify the final image 100–200% (saved as `zoom`).
- **On-screen display**: a 2-second message in the top-right corner for hotkey events
  (`g_osd_enable`, Show OSD in the Ctrl+F1 menu). Drawn in host pixels, so zoom has no effect.
- **Mouse capture**: Ctrl+F2 or middle-click confines the pointer to the window.
- **xBRZ 2x filter**: `-xbrz 1` or the Ctrl+F1 Display Effects menu. Ported from
  ProSystemSE's 2x corner-blend filter; toggles at runtime and works with CRT.
- **Input remapping**: SDL-free host-to-IIgs mapping in `input.map` (keys, mouse,
  gamepad buttons and sticks). Control Configuration > Remap Controls in the Ctrl+F1
  menu rebinds keys with press-to-bind (a chord binds as one combo; a lone modifier
  binds on release), Esc to cancel, and reset to defaults. Gamepad remapping is
  confirmed on real hardware.

---

## Legacy gsplus features (original roadmap)

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Clipboard** copy/paste | Med | M | Spec below. |

### Clipboard copy/paste (v1 spec)

- **Hotkeys:** remappable actions. Defaults: Ctrl+Shift+C (copy), Ctrl+Shift+V (paste). Ctrl+letter stays with the guest by default, so Ctrl+C is not used.
- **Copy, text mode:** whole text screen via `cfg_text_screen_str()` (`src/config.c`). Only when `ALL_STAT_TEXT` is set in `g_cur_a2_stat`, so graphics modes don't copy garbage.
- **Copy, GS/OS:** observe-only hook on the Toolbox entry `$E10000`. On `PutScrap` with scrap type 0 (text), read `numBytes`, `scrapType`, and `srcPtr` from the stack, convert CR to LF, and set the host clipboard. The hook must never change registers, stack, or PC. `src/debugger.c` already traces `$E10000`/`$E10004`; `toolbox_debug_c()` in `src/sim65816.c` is unused.
- **Paste:** host text goes into the existing ADB paste buffer (`adb_paste_add_buf()` / `adb_paste_update_state()`, `src/adb.c`). This reaches any app that reads `$C000`. Paste does not use the Scrap Manager.
- **Host side:** SDL3 clipboard (`SDL_GetClipboardText` / `SDL_SetClipboardText`).
- **Unverified:** whether GS/OS sends every Toolbox call through `$E10000`; the text scrap line-ending format (CR is assumed); whether GS/OS text fields accept pasted keystrokes.
- **Reference:** Scrap Manager is in Toolbox Reference Vol. 2, chapter 20 (text = type 0, picture = type 1; `ScrapStartUp` first; `ZeroScrap` then `PutScrap` appends; `GetScrap` sizes the handle).

