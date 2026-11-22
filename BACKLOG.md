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
- **Screenshots**: the screenshot action captures the framebuffer (unbound by default;
  `-ssdir` sets the output
  directory).
- **Mouse capture**: Ctrl+F2 or middle-click confines the pointer to the window.
- **xBRZ 2x filter**: `-xbrz 1` or the Ctrl+F1 Display Effects menu. Ported from
  ProSystemSE's 2x corner-blend filter; toggles at runtime and works with CRT.
- **Input remapping**: SDL-free host-to-IIgs mapping in `input.map` (keys, mouse,
  gamepad buttons and sticks). Joystick Configuration > Remap Controls in the Ctrl+F1
  menu rebinds keys with press-to-bind (a chord binds as one combo; a lone modifier
  binds on release), Esc to cancel, and reset to defaults. Gamepad remapping is
  tested only with uinput fake pads, not real hardware.

---

## Legacy gsplus features (original roadmap)

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Clipboard** copy/paste | Med | M | Wire SDL clipboard to the core's copy/paste hooks **(core)**. |

## Display

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Integer / pixel-perfect scaling** toggle | Med | S | Crisp 1×/2×/3× vs. letterbox stretch; also stops scanline shimmer at non-integer scales. |
| Re-add **vsync** option properly | Low | M | Removed pre-1.38.0 (double-pacing caused half-speed + audio gaps). Right fix: when vsync is on, let it drive pacing and drop the sleep-based clock — one pacer, not two. |

## Runtime / system

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Fast-forward / turbo** hotkey | High | S | Hold to uncap speed. |
| **Pause/resume** hotkey | Med | S | |
| **Save states / snapshots** | Med | L | Full IIgs machine state; KEGS has none. Stretch. |

## Quality of life

| Item | Pri | Eff | Notes |
|---|---|---|---|
| Modern **overlay config menu** (vs. Ctrl+F1 text panel) | Low | L | Big polish, big effort. |

## Testing / infrastructure

| Item | Pri | Eff | Notes |
|---|---|---|---|
