![IIGS-Emu - GNU/Linux Apple IIgs emulator](gsplus-social.png)

# IIGS-Emu

Originally based on [GSplus](https://github.com/digarok/gsplus), an Apple IIgs
emulator derived from [KEGS](http://kegs.sourceforge.net) by Kent Dickey.

## Requires Apple IIGS ROM (Not Included)

**Which ROM:** the IIgs shipped with two ROM versions. **ROM 03** (256 KB, the
later model) is the usual choice; **ROM 01** (128 KB) is also fully supported.
IIGS-Emu does **not** support the early ROM 0 or the unreleased ROM 04 prototype.

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

- **Window & display options** - `-fullscreen`, `-borderless`, `-noaspect`,
  `-highdpi`, `-nohwaccel`, plus window position. Fullscreen has no
  default key; bind one under Remap Controls.
- **Screenshot capture** - writes the framebuffer to disk. Ctrl+F5 by default; rebind
  under Remap Controls. Files go to the Screenshot Directory (`ssdir`), or the config.kegs folder if unset.
- **Gamepad support** - SDL_Gamepad mapped to the IIgs joystick, with hotplug.
  Buttons and sticks can be rebound under Remap Controls, and any key can be
  bound to a joystick button or paddle there. The SDL controller
  database (`gamecontrollerdb.txt`, from
  [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)) is
  loaded from beside the binary, then from `~/.config/iigs-emu/`. D-pads and
  sticks both drive the joystick. The stick's circular travel is remapped to
  the square range Apple joysticks have, so games can reach the corners; turn
  this off with Gamepad Range under Control Configuration (Ctrl+F1).
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
- **Zoom** - Ctrl+1 zooms in and Ctrl+2 zooms out, 100-200% in 1% steps. It
  magnifies the final processed image around the centre, so filters and CRT
  still apply. The window size stays fixed, so the picture edges (including the
  status bar) are cropped at higher zoom. The level is saved as `zoom` in
  config.kegs. Both keys are rebindable under Remap Controls > Mouse and Actions.
- **Speed control** - Ctrl+F3 cycles Unlimited, 1.024 MHz, 2.8 MHz, and ZipGS
  Accelerator (the default). ZipGS Accelerator runs at the clock set by **ZipGS
  Speed** (8–128 MHz, default 8) in the main Ctrl+F1 menu, but only while software
  has enabled the ZipGS card. Otherwise that clock is not used.
- **On-screen display** - a short message in the top-right corner when a hotkey
  changes something (zoom, speed, fast disk, scanlines, CRT, mouse capture,
  screenshots, and so on). It stays for 2 seconds and does not scale with zoom.
  Turn it off with **Show OSD** in the main Ctrl+F1 menu (on by default, saved
  as `g_osd_enable` in config.kegs).
- **xBRZ 2x filter** - `-xbrz 1` or the Display Effects menu (Ctrl+F1). Doubles the
  picture with a corner-smoothing filter and doubles the window size; CRT
  effects apply on top of it.
  - **Tip: reduce vertical lines** - the IIgs' colour stripes on solid areas
    like the desktop wallpaper can look like vertical lines. Set Horizontal
    Blur to about 70 (`-hblur 70`, or Ctrl+F1 > Display Effects > Horizontal Blur)
    to smooth them out. It is a little softer; lower values keep more detail.
- **Host clipboard** - Ctrl+Shift+C copies the text screen (text modes only) to the
  host clipboard. MouseText cells (alternate character set on) copy as `?`.
  Ctrl+Shift+V types the host clipboard into the IIgs as keystrokes, paced so slow
  programs keep up, and starts once Ctrl and Shift are released. Each non-ASCII
  character types as one `?`. GS/OS Edit>Copy and Edit>Paste share the same host
  clipboard (tested with Hermes) and convert accented letters between Mac Roman and
  UTF-8. Host characters with no Mac Roman form, such as €, paste as `?`. Both
  hotkeys can be rebound under Remap Controls > Mouse and Actions.
- **FileWriter virtual printer** - set a serial port's Main setting to
  `FileWriter printer` under Serial Port Configuration (Ctrl+F1). The printer
  emulates an Apple ImageWriter II and answers its ESC ? self-ID as a color
  ribbon printer (`IW10C`), so GS/OS apps such as Paint256 enable Color. Each
  page is saved as a pixel-faithful `.png` in the Screenshot Directory (`ssdir`),
  or the config.kegs folder if unset. A page with any colored ink is saved in
  RGB. A page with ASCII text also gets a text-only `.txt` (Mac Roman, CR line
  endings). Pages are written at form feed, when the paper runs past the page
  length, when the port is changed, at normal quit, and at emulator exit. A page
  is also saved after about 1 s without output, and a job ends after about 5 s
  without output, so the next job starts on a fresh sheet. Continuous printing
  can still lose the page on a crash or SIGKILL. The Serial Mask setting does not
  apply; the printer's own 8th-bit switch decides. Intentionally not supported:
  ESC v, software select, true proportional spacing, paper-out, and perforation
  skip. Not emulated: high-ASCII MouseText (ESC Z) and a real NLQ font.
  Sub/superscript (ESC x, ESC y, ESC z) squashes each 9-row glyph into 5 rows, so
  it is a rough approximation. MyWord has no color support, so its Color job
  prints black.
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
| **Ctrl + 1** / **Ctrl + 2** | Zoom in / zoom out |
| **Ctrl + F3** | Cycle speed (Unlimited, 1.024 MHz, 2.8 MHz, ZipGS Accelerator) |
| **Ctrl + F4** | Toggle fast disk emulation |
| **Ctrl + F5** | Save a screenshot |
| **Ctrl + Shift + C** | Copy the text screen to the host clipboard (text modes only) |
| **Ctrl + Shift + V** | Type the host clipboard into the IIgs as keystrokes |

The IIgs keys are named by their Apple II-era names, which map to the modern
keys: **Open Apple** is **Command** (either side, the "clover" key), and
**Closed Apple** is **Option** (either side). The Remap Controls menu lists them
as Command and Option.

F1–F15 and the six navigation keys (Help/Insert, Home, Page Up, Forward Delete,
End, Page Down) reach the IIgs as extended keyboard keys. They type no character
and do not hold Open Apple.

Other emulator actions (status line, debugger go, fullscreen, CRT effects,
scanlines, and warp pointer) have no default key. Bind them
under **Control Configuration > Remap Controls > Mouse and Actions**.

While the config menu is open, the IIgs receives no keyboard or mouse input.
By default, Caps Lock latches the IIgs Caps Lock key; the host Caps Lock light
does not show that state.

Only the US keyboard layout is supported. The emulator always reports layout 0
and character set 0, and it ignores a layout chosen in the Control Panel, so
non-US layouts do not change which characters keys produce. Option accent keys
work in GS/OS applications: Option+E, Option+U, and Option+N are dead keys (Option+E
then e gives é), and Option+C gives ç directly. Holding Option+E past the
emulator's key repeat delay (about 0.75 seconds by default) repeats the E, which
can leave extra bare accents (´´´´e).

The mouse wheel sends Up and Down arrows by default, one keystroke per notch.
To change that, select a row under **Remap Controls > Editing and Arrows** and
scroll the wheel, the same way a mouse button is captured. In `input.map` the
sources are `wheel:UP` and `wheel:DOWN`.

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
