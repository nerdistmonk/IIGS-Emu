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
  `-highdpi`, `-nohwaccel`, window position; F11 fullscreen toggle. (`-novsync`
  was later removed — see the vsync item under Display.)
- **F2 — Scanline simulator**: `-scanline <0-100>`, Shift+F11 toggle.
- **F3 — Drag-and-drop**: drop a disk image on the window to mount it (WOZ slot
  from its INFO disk type; other images guessed from file size).
- **Curved CRT effect**: `-crt 1` + `-crtcurve <0-100>` + `-crtmask <0-100>`,
  Ctrl+F11 toggle. Curvature (RenderGeometry mesh) + RGB phosphor mask + bloom +
  vignette, composes with `-scanline`. All on the 2D renderer, no extra libraries.
- **Gamepad/controller support**: SDL3 high-level Gamepad API → IIgs
  joystick/paddles, with hotplug. Select "Native Joystick 1" in the F4 config
  menu to route paddles to it.
- **Screenshots**: F10 captures the framebuffer (`-ssdir` sets the output
  directory).
- **Mouse capture**: Scroll Lock or middle-click confines the pointer to the window.

---

## Legacy gsplus features (original roadmap)

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Clipboard** copy/paste | Med | M | Wire SDL clipboard to the core's copy/paste hooks **(core)**. |

## Input

| Item | Pri | Eff | Notes |
|---|---|---|---|
| Configurable **key remapping** | Low | M | Currently a fixed scancode→ADB table. |
| **Gamepad button remapping** | Low | S | SDL_Gamepad's stable button names make a small remap table in the config menu cheap. |

## Display

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **Integer / pixel-perfect scaling** toggle | Med | S | Crisp 1×/2×/3× vs. letterbox stretch; also stops scanline shimmer at non-integer scales. |
| Linear-filter option | Low | S | Smooth-scaling alternative to nearest. |
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
| **Recent disks/ROMs** menu | Med | M | |
| Modern **overlay config menu** (vs. F4 text panel) | Low | L | Big polish, big effort. |

## Testing / infrastructure

| Item | Pri | Eff | Notes |
|---|---|---|---|
| **CI smoke test** | High | M | Headless boot (SDL `dummy` video driver) for N emulated frames, checksum the framebuffer. Catches CPU/memory/video regressions on every PR — highest-leverage single test, especially for upstream GSplus merges. Needs a redistributable boot image or ROM-free path. |
