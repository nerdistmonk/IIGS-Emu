![IIGS-Emu - cross-platform Apple IIgs emulator](gsplus-social.png)

# IIGS-Emu

An accessible build of [GSplus](https://github.com/digarok/gsplus), an Apple IIgs
emulator derived from [KEGS](http://kegs.sourceforge.net) by Kent Dickey,
packaged with
[SDL3](https://www.libsdl.org/) for Linux.

## What it is

IIGS-Emu wraps the GSplus core in a single SDL3 application for Linux. IIGS-Emu
tracks the KEGS version GSplus is based on (currently 1.38).

## You'll need a ROM

Like any IIgs emulator, IIGS-Emu needs an Apple IIgs ROM file to boot. Without one
it starts into a configuration screen with nothing to run. The ROM is Apple's
copyrighted firmware, so IIGS-Emu can't ship it — you supply your own.

**Which ROM:** the IIgs shipped with two ROM versions. **ROM 03** (256 KB, the
later model) is the usual choice; **ROM 01** (128 KB) is also fully supported.
IIGS-Emu does **not** support the early ROM 0.

**Where to put it:** name the file `ROM`, `ROM.01`, or `ROM.03` and place it in
your home directory, next to the app, or next to your `config.kegs`. IIGS-Emu picks
it up automatically. You can also select one at runtime: press **F4 → ROM File
Selection → ROM File** and browse to it. A handful of other common names
(`APPLE2GS.ROM`, `APPLE2GS.ROM2`, `xgs.rom`, `342-0077-b`, …) are recognized too.

**Finding one:** the cleanest option is to dump the ROM from a real Apple IIgs you
own. The original KEGS documentation includes a short BASIC program that does it.
Otherwise, the IIgs ROM is long-unsupported software that's widely circulated
online: a web search for *"Apple IIgs ROM 03"* (or ROM 01) generally turns one
up, and the Internet Archive hosts MAME sets that contain a usable ROM. That same
`README.ROM.files.txt` lists the exact filenames KEGS accepts and how to extract
or convert a MAME-format dump.

**Did it work?** A valid ROM 01 file is exactly **128 KB** and a ROM 03 is **256
KB**. If IIGS-Emu boots to the "Check startup device" screen, the ROM loaded fine —
it just needs a disk. Press **F4** to mount a disk image, or drag one onto the
window.

## Status

This is an early reboot - expect rough edges. SDL3 **video, input, and audio**
are working on Linux.

### What's been added in IIGS-Emu

Some features that are a part of the SDL3 build:

- **Window & display options** - `-fullscreen`, `-borderless`, `-noaspect`,
  `-highdpi`, `-novsync`, `-nohwaccel`, plus window position; F11 toggles
  fullscreen.
- **Screenshot capture** - F10 writes the framebuffer to disk.
- **Gamepad support** - cross-platform SDL_Gamepad mapped to the IIgs joystick.
  A connected gamepad is used automatically (with hotplug); without one, the
  numeric keypad emulates the joystick. The stick's circular travel is remapped
  to the square range Apple joysticks have, so games can reach the corners.
  Override either behavior in the config menu (F4) under Joystick
  Configuration.
- **Terminal debugger** - a REPL for the built-in 65816 debugger from the
  controlling terminal.
- **Drag-and-drop disk loading** - drop a disk image on the window and IIGS-Emu
  guesses the slot from its size.
- **Centered 8-bit video modes** - 40/80-column text, GR, HGR and DHGR are
  560 pixels wide vs. super-hires' 640; IIGS-Emu centers them instead of
  GSplus's left-align. `-centera2 0` (or the Display Effects menu) restores
  the GSplus behavior.
- **CRT scanline simulator** - `-scanline <0-100>`, Shift+F11 to toggle.
- **Curved CRT effect** - `-crt 1` with `-crtcurve` / `-crtmask` / `-crtglow`
  / `-crtvignette`, Ctrl+F11 to toggle. Screen curvature, RGB phosphor mask,
  bloom, and vignette; composes with the scanline simulator.
- **CMake build** - a single SDL3 app built with CMake and Ninja, with prebuilt
  Linux downloads.

## Controls

The following hotkeys let you dynamically change many of the emulator settings,
but you can also edit most settings by hitting `F4` for the configuration menu. 

| Key | Action | Source |
|---|---|---|
| **F1** | Open-Apple (⌘) modifier, for keyboards without an Apple/Command key | GS+ |
| **F4** | Open the configuration menu (mount disks, pick a ROM, settings) | GS+ |
| **F5** | Toggle the status line | GS+ |
| **F6** | Cycle emulation speed (1 MHz / 2.8 MHz / fast) | GS+ |
| **F7** | Toggle the debugger | GS+ |
| **Shift + F7** | Toggle fast disk emulation | GS+ |
| **F8** | Grab / release the mouse (warp + hide pointer) | GS+ |
| **F9** | Invert paddles | GS+ |
| **Shift + F9** | Swap paddles | GS+ |
| **Ctrl + F9** | Copy screen text to the clipboard | GS+ |
| **F10** | Save a screenshot | GS+ |
| **F11** | Toggle fullscreen | GS+ |
| **Shift + F11** | Toggle the CRT scanline effect | GS+ |
| **Ctrl + F11** | Toggle the CRT curved screen effect | GS+ |
| **Ctrl + F12** | Reset the IIgs (Ctrl-Reset) | GS+ |

You can also **drag & drop** a disk image onto the window to mount it (slot
guessed from size). 

## Building from source

IIGS-Emu builds with CMake (Ninja generator). SDL3 is used if installed, otherwise
built from source automatically. From the repository root:

```sh
cmake -G Ninja -B build -S gsplus/src
cmake --build build --target gsplus-sdl
```

This produces the `build/iigs-emu` executable.

### Dependencies

**Debian/Ubuntu** - the X11/Wayland/audio packages are SDL3's build dependencies:

```sh
sudo apt install cmake ninja-build build-essential git \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev libxtst-dev libxinerama-dev \
  libwayland-dev wayland-protocols libdecor-0-dev libxkbcommon-dev \
  libdrm-dev libgbm-dev libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev \
  libpulse-dev libasound2-dev libpipewire-0.3-dev libsndio-dev \
  libdbus-1-dev libudev-dev libibus-1.0-dev
```
