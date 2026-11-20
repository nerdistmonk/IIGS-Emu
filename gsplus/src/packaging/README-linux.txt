IIGS-Emu on Linux
===============

IIGS-Emu on Linux
===============

IIGS-Emu needs the SDL3 library installed on your system. If launching
fails with an error like:

    error while loading shared libraries: libSDL3.so.0:
    cannot open shared object file: No such file or directory

install the SDL3 runtime package from your distribution's package manager.
It is usually named "sdl3" or "libsdl3". If your distribution does not
package SDL3 yet, build it from source:

    # Development packages SDL needs for video/audio support; see
    # https://wiki.libsdl.org/SDL3/README-linux for the full list.
    # Package names vary by distribution.
    # Needed: a C compiler, cmake, git, and the X11, Wayland, EGL,
    # PulseAudio, and ALSA development headers.

    git clone https://github.com/libsdl-org/SDL
    cmake -S SDL -B SDL/build -DCMAKE_BUILD_TYPE=Release
    cmake --build SDL/build -j$(nproc)
    sudo cmake --install SDL/build
    sudo ldconfig

Running
-------

IIGS-Emu also needs an Apple IIgs ROM file and a config file to be useful.
See README.md (included alongside this file) for setup instructions,
hotkeys, and everything else.
