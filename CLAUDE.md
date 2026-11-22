# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

IIGS-Emu is a Linux Apple IIgs emulator based on GSplus, which derives from KEGS (Kent's Emulated GS) by Kent Dickey. It emulates the 65816 CPU, all Apple IIgs graphics/sound modes, disk controllers, serial ports, and more. Licensed under GPLv3.

## Repository Structure

- `src/` — Active source code and build files (this is where you build). Also holds `gsplusicon.png` and the `make_icons` script that generates `gsplus_icon.h`

The `upstream` remote points at digarok/gsplus; `main` is the primary development branch.

## Build Commands

Linux is the only supported platform. From the repository root:
```bash
cmake -G Ninja -B build -S src
cmake --build build --target gsplus-sdl
```
Produces `build/iigs-emu`. SDL3 is used if installed, otherwise CMake fetches and builds it. See README.md for the apt package list.

### Clean
```bash
rm -rf build
```

## Architecture

### CPU & Core Loop
- `sim65816.c` — Main simulation loop, event scheduling, interrupt handling
- `engine_c.c` + `engine.h` — 65816 CPU instruction emulation (macro-heavy for performance)
- `defs_instr.h`, `instable.h`, `op_routs.h` — Instruction definitions, opcode table, operation macros

### Memory
- `moremem.c` — Memory management, page table fixup, I/O register reads/writes ($C000-$C0FF area)
- Page-table-based MMU for dynamic address mapping

### Video
- `video.c` — All Apple IIgs/II graphics mode rendering (text, lores, hires, super hires)

### Audio
- `sound.c` — Sound generation (mixing, output buffering)
- `doc.c` — Ensoniq DOC 32-voice synthesizer emulation
- `mockingboard.c` — Mockingboard A card (6522 VIA + AY-8913)

### Disk I/O
- `iwm.c` — IWM disk controller (5.25" and 3.5" drives, nibble-level accuracy)
- `smartport.c` — SmartPort controller for hard drive images
- `dynapro.c` — Host directory mounting as virtual ProDOS volumes
- `woz.c` — WOZ disk image format support
- `unshk.c`, `undeflate.c`, `applesingle.c` — Archive/compression format support

### Input
- `adb.c` — Apple Desktop Bus (keyboard, mouse); accepts only IIgs keycodes
- `input_map.c` + `input_map.h` — SDL-free host-to-IIgs mapping (keys, mouse, gamepad to ADB keys, mouse, paddles, BUTN, and emulator actions), loaded from `input.map`. The F4 menu's Joystick Configuration > Remap Controls (`config.c`) edits it through the `imap_remap_*` API; saving rewrites the file and drops comments.
- `paddles.c` — Game port paddles and push buttons
- `scc.c` + `scc_socket_driver.c` — Serial Communications Controller with TCP/IP modem emulation

### Configuration & Debug
- `config.c` — Runtime configuration UI, disk mounting, settings persistence (`config.kegs`)
- `debugger.c` — Built-in 65816 debugger/monitor

### Platform Drivers
The emulator core is platform-independent. Platform-specific code is isolated in the SDL driver files:

| Component | File |
|-----------|------|
| Display, input, gamepad | `sdl_driver.c` (SDL3) |
| Audio | `sdl_snd_driver.c` (SDL3) |
| Serial | `scc_unixdriver.c` |

### Key Headers
- `defc.h` — Global defines, structs, macros (included by nearly every .c file)
- `defcomm.h` — Shared defines for C and assembly
- `protos_base.h` — Function prototypes for all modules

## Build System Details

`src/CMakeLists.txt` defines the build. The core sources are listed explicitly in `GSPLUS_CORE_SOURCES`. The `gsplus_core_sdl` library is compiled with `-DSDL_AUDIO -DSDL_INPUT`, which selects the SDL audio and gamepad backends.

## Runtime Requirements

The emulator needs an Apple IIgs ROM file to run. Configuration is stored in `config.kegs`. Input bindings are stored in `input.map` beside it, written with commented defaults on first run; `-map <file>` selects another file.

## No Test Suite

There is no automated test infrastructure. Testing is done manually by running Apple IIgs software and ROM self-tests.
