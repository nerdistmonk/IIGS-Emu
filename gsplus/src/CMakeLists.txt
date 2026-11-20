# ============================================================================
#  IIGS-Emu - CMake build (Linux, SDL3)
# ============================================================================
#
#  HOW TO READ THIS FILE (CMake in plain terms)
#  --------------------------------------------
#  This file DESCRIBES the project; CMake then GENERATES the real build for
#  your machine. You drive it with two commands:
#
#      cmake -B build          # read this file, set up a ./build folder
#      cmake --build build     # actually compile
#
#  It compiles the emulator core (platform-independent C) and links it with the
#  SDL3 front end into the `iigs-emu` executable. Linux is the only supported
#  platform.
# ----------------------------------------------------------------------------

# Minimum CMake version (needed for the SDL3 FetchContent fallback).
cmake_minimum_required(VERSION 3.20)

# Declare the project: a name, a version, and the language used.
project(gsplus
    VERSION 1.40.0
    DESCRIPTION "IIGS-Emu - Apple IIgs emulator (GSplus-based)"
    LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "IIGS-Emu supports Linux only (found ${CMAKE_SYSTEM_NAME})")
endif()

# ----------------------------------------------------------------------------
# Version, MAJOR.MINOR.PATCH, set in project() below.
# CI passes the exact git tag via -DGSPLUS_VERSION (e.g. -DGSPLUS_VERSION=1.40.0)
# so the tag, the app, and the release artifacts all carry the same version.
# Local builds without -DGSPLUS_VERSION fall back to the project() version above.
# ----------------------------------------------------------------------------
if(NOT DEFINED GSPLUS_VERSION OR GSPLUS_VERSION STREQUAL "")
    set(GSPLUS_VERSION "${PROJECT_VERSION}")
endif()
message(STATUS "IIGS-Emu: version ${GSPLUS_VERSION}")

# Make the version string available to all targets defined below (the app's
# window title/startup log in sdl_driver.c and the status line in sim65816.c).
add_compile_definitions("GSPLUS_VERSION_STR=\"${GSPLUS_VERSION}\"")

# ----------------------------------------------------------------------------
# The platform-independent core sources. We list them explicitly rather than
# globbing, so CMake never silently picks up or drops a file behind our back.
# ----------------------------------------------------------------------------
set(GSPLUS_CORE_SOURCES
    adb.c
    engine_c.c
    clock.c
    config.c
    debugger.c
    scc.c
    scc_socket_driver.c
    scc_unixdriver.c
    iwm.c
    joystick_driver.c
    moremem.c
    paddles.c
    mockingboard.c
    sim65816.c
    smartport.c
    doc.c
    sound.c
    sound_driver.c
    woz.c
    unshk.c
    undeflate.c
    dynapro.c
    dyna_type.c
    dyna_filt.c
    dyna_validate.c
    applesingle.c
    video.c
    voc.c
    compile_time.c
)

# If the user didn't pick a build type, default to an optimized one (-O2-ish).
# CMake maps "Release" to -O2/-O3 automatically, so we don't hard-code -O2.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "IIGS-Emu: build type: ${CMAKE_BUILD_TYPE}")


# ============================================================================
#  The SDL3 executable (gsplus-sdl -> iigs-emu)
# ============================================================================
#
#  SDL3 provides display, input, and audio from one set of driver files
#  (sdl_driver.c, sdl_snd_driver.c).
#
#  find_package(SDL3) locates SDL3 wherever it's installed (apt, source build,
#  etc.) and gives us the SDL3::SDL3 target carrying its include paths and link
#  flags -- no hand-written -I/-l bookkeeping.
# ----------------------------------------------------------------------------
# Find SDL3 on the system (Homebrew, apt, vcpkg, ...). If it isn't installed --
# common on CI runners, since SDL3 is new -- fetch and build it from source so
# the build is self-sufficient everywhere. Locally this is instant (Homebrew's
# SDL3 is found); only CI without SDL3 pays the one-time build cost.
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    message(STATUS "IIGS-Emu: SDL3 not found on system -- fetching from source")
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.10        # matches the version developed against
        GIT_SHALLOW TRUE)
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST   OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
else()
    message(STATUS "IIGS-Emu: using system SDL3 ${SDL3_VERSION}")
endif()

# The emulator core. SDL_AUDIO routes sound through the SDL backend.
add_library(gsplus_core_sdl STATIC ${GSPLUS_CORE_SOURCES})
target_include_directories(gsplus_core_sdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# SDL_INPUT compiles the native joystick backends out of joystick_driver.c so the
# SDL_Gamepad backend in sdl_driver.c provides joystick_init/update instead.
target_compile_definitions(gsplus_core_sdl PUBLIC SDL_AUDIO SDL_INPUT)
target_compile_options(gsplus_core_sdl PRIVATE -Wall)

# The portable executable: SDL driver + SDL audio backend, linked against the
# SDL-flavoured core and SDL3 itself. (PUBLIC SDL_AUDIO from the core propagates
# to these sources too, keeping the audio routing consistent.)
add_executable(gsplus-sdl
    sdl_driver.c
    sdl_snd_driver.c
    png_write.c
)
target_link_libraries(gsplus-sdl PRIVATE gsplus_core_sdl SDL3::SDL3)

# paddles.c uses sqrt(); glibc keeps it in libm
target_link_libraries(gsplus-sdl PRIVATE m)

# The binary is just "iigs-emu" (no -sdl). $ORIGIN lets it find a co-located
# SDL3 shared library at runtime.
set_target_properties(gsplus-sdl PROPERTIES
    OUTPUT_NAME "iigs-emu"
    INSTALL_RPATH "$ORIGIN")

message(STATUS "IIGS-Emu: configured SDL3 target (gsplus-sdl)")


