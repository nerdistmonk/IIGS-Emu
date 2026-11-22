![IIGS-Emu - GNU/Linux Apple IIgs emulator](gsplus-social.png)

# IIGS-Emu

Originally based on [GSplus](https://github.com/digarok/gsplus), an Apple IIgs
emulator derived from [KEGS](http://kegs.sourceforge.net) by Kent Dickey.

## You'll need a ROM

Like any IIgs emulator, IIGS-Emu needs an Apple IIgs ROM file to boot. Without one
it starts into a configuration screen with nothing to run. The ROM is Apple's
copyrighted firmware, so IIGS-Emu can't ship it — you supply your own.

**Which ROM:** the IIgs shipped with two ROM versions. **ROM 03** (256 KB, the
later model) is the usual choice; **ROM 01** (128 KB) is also fully supported.
IIGS-Emu does **not** support the early ROM 0.

**Where to put it:** name the file `ROM`, `ROM.01`, or `ROM.03` and place it in
your home directory, next to the app, or next to your `config.kegs`. IIGS-Emu picks
it up automatically. You can also select one at runtime: press **Ctrl+F1 → ROM File
Selection → ROM File** and browse to it. A handful of other common names
(`APPLE2GS.ROM`, `APPLE2GS.ROM2`, `xgs.rom`, `342-0077-b`, …) are recognized too.

**Did it work?** A valid ROM 01 file is exactly **128 KB** and a ROM 03 is **256
KB**. If IIGS-Emu boots to the "Check startup device" screen, the ROM loaded fine —
it just needs a disk. Press **Ctrl+F1** to mount a disk image, or drag one onto the
window.

### What's been added in IIGS-Emu

Some features that are a part of the SDL3 build:

- **Window & display options** - `-fullscreen`, `-borderless`, `-noaspect`,
  `-highdpi`, `-nohwaccel`, plus window position. Fullscreen has no
  default key; bind one under Remap Controls.
- **Screenshot capture** - writes the framebuffer to disk. No default key; bind
  one under Remap Controls.
- **Gamepad support** - SDL_Gamepad mapped to the IIgs joystick, with hotplug.
  Buttons and sticks can be rebound under Remap Controls, and any key can be
  bound to a joystick button or paddle there. The SDL controller
  database (`gamecontrollerdb.txt`, from
  [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)) is
  loaded from beside the binary, then from `~/.config/iigs-emu/`. D-pads and
  sticks both drive the joystick. The stick's circular travel is remapped to
  the square range Apple joysticks have, so games can reach the corners; turn
  this off with Gamepad Range under Joystick Configuration (Ctrl+F1).
- **Terminal debugger** - a REPL for the built-in 65816 debugger from the
  controlling terminal.
- **Drag-and-drop disk loading** - drop a disk image on the window and IIGS-Emu
  picks the slot from the WOZ disk type or the file size.
- **Centered 8-bit video modes** - 40/80-column text, GR, HGR and DHGR are
  560 pixels wide vs. super-hires' 640; IIGS-Emu centers them instead of
  GSplus's left-align. `-centera2 0` (or the Display Effects menu) restores
  the GSplus behavior.
- **CRT scanline simulator** - `-scanline <0-100>`. Bind the scanlines action under Remap Controls to toggle it.
- **Curved CRT effect** - `-crt 1` with `-crtcurve` / `-crtmask` / `-crtglow`
  / `-crtvignette`. Bind the crt action under Remap Controls to toggle it. Screen curvature, RGB phosphor mask,
  bloom, and vignette; composes with the scanline simulator.
- **xBRZ 2x filter** - `-xbrz 1` or the Display Effects menu (Ctrl+F1). Doubles the
  picture with a corner-smoothing filter and doubles the window size; CRT
  effects apply on top of it.
  - **Tip: reduce vertical lines** - the IIgs' colour stripes on solid areas
    like the desktop wallpaper can look like vertical lines. Set Horizontal
    Blur to about 70 (`-hblur 70`, or Ctrl+F1 > Display Effects > Horizontal Blur)
    to smooth them out. It is a little softer; lower values keep more detail.
- **CMake build** - a single SDL3 app built with CMake and Ninja, with prebuilt
  GNU/Linux downloads.

## Controls

By default the Apple IIgs gets the keyboard directly, F1–F12 included. The emulator
reserves only these host keys:

| Key | Action |
|---|---|
| **Ctrl + F1** | Open or close the configuration menu (mount disks, pick a ROM, settings) |
| **Ctrl + F2** or **middle-click** | Grab / release the mouse (confines the pointer to the window) |
| **Ctrl + F12** | Reset the IIgs |

The IIgs keys are named by their Apple II-era names, which map to the modern
keys: **Open Apple** is **Command** (either side, the "clover" key), and
**Closed Apple** is **Option** (either side). The Remap Controls menu lists them
as Command and Option.

Other emulator actions (status line, emulation speed, debugger, fast disk,
screenshot, fullscreen, CRT effects, and scanlines) have no default key. Bind them
under **Joystick Configuration > Remap Controls > Mouse and Actions**.

While the config menu is open, the IIgs receives no keyboard or mouse input.
By default, Caps Lock latches the IIgs Caps Lock key; the host Caps Lock light
does not show that state.

You can also **drag & drop** a disk image onto the window to mount it. WOZ images
go to slot 6 (5.25") or slot 5 (3.5") from their disk type; other images are
placed by file size.

## Building from source

IIGS-Emu builds with CMake (Ninja generator). SDL3 is used if installed, otherwise
built from source automatically. From the repository root:

```sh
cmake -G Ninja -B build -S src
cmake --build build --target gsplus-sdl
```

This produces the `build/iigs-emu` executable.

### Dependencies

You need a C compiler (GCC), CMake, Ninja, and git. SDL3 is used if installed;
otherwise it is built from source, which needs development headers for your
system's display and audio stacks:

- **X11 and Wayland:** the X11 extension libraries (Xext, Xrandr, Xcursor, Xi,
  Xfixes, Xss, Xtst, Xinerama), Wayland client and protocol files, libdecor, and
  xkbcommon.
- **Graphics:** DRM, GBM, EGL, and OpenGL/GLES.
- **Audio:** PulseAudio, ALSA, PipeWire, and sndio.
- **Other:** D-Bus, udev, and IBus.

Package names vary by distribution. Search your package manager for the matching
`-dev` or `-devel` packages.
