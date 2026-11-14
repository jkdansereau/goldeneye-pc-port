# project-dam: the Nintendo Switch homebrew build (devkitA64 + libnx), kept
# out of CMakeLists.txt as much as possible. CMakeLists.txt includes this file
# right after project() and calls the macros below at marked hooks
# ("project-dam hook"). devkitPro's toolchain file
# (/opt/devkitpro/cmake/Switch.cmake) sets CMAKE_SYSTEM_NAME=NintendoSwitch.
#
# Address model (docs/switch/PLAN.md §2): the N64 window base is chosen at run
# time (g_portAddrBase, a free 4 GiB-aligned slot; nxChooseAddrBase), because the
# main thread's stack can land on the fixed 0x10000000 / 0x70000000 windows.
# port/src/switch/ places the cart, DRAM and stack blocks at base + their N64
# addresses. The ROM labels (romassets_*.s) stay link-time absolutes at their
# N64 values (they are only ever used as N64 cart addresses); the three DRAM
# symbols became macros (portaddr.h PORT_DRAM_SYM).

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
  set(GE_SWITCH ON)
  message(STATUS "project-dam: Nintendo Switch build (devkitA64 ${CMAKE_C_COMPILER_VERSION})")
else()
  set(GE_SWITCH OFF)
endif()

# newlib's headers live in the toolchain's target include dir, not at
# bin/../include like MinGW / a native Linux GCC (hoststring.h & co.).
macro(switch_host_headers)
  if(GE_SWITCH)
    set(_NX_SYSINC "${DEVKITPRO}/devkitA64/aarch64-none-elf/include")
    set(PORT_HOST_STRING_H "${_NX_SYSINC}/string.h")
    set(PORT_HOST_SCHED_H  "${_NX_SYSINC}/sched.h")
    set(PORT_HOST_STDLIB_H "${_NX_SYSINC}/stdlib.h")
  endif()
endmacro()

# Dependencies: SDL2 (static, pulls EGL/glapi/drm_nouveau/libnx), zlib. OpenGL
# entry points are loaded at run time through SDL_GL_GetProcAddress (glad), so
# no GL library is linked directly.
macro(switch_dependencies)
  find_package(SDL2 CONFIG REQUIRED)
  find_package(ZLIB REQUIRED)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIRS}")
  set(ZLIB_INCLUDE_DIR "${ZLIB_INCLUDE_DIRS}")
  list(APPEND LIBS SDL2::SDL2 ZLIB::ZLIB m)
endmacro()

# Project Dam's own version (its releases are tagged project-dam-vX.Y.Z); the
# release workflow passes it, local builds say dev-<commit>.
set(PROJECT_DAM_VERSION "" CACHE STRING "Project Dam version shown in the homebrew menu")

# NRO packaging: icon + NACP (title shown in the homebrew menu).
macro(switch_package target)
  if(GE_SWITCH)
    # newlib is linked statically: the game's own strtol (src/str.c, 32-bit
    # libultra flavour, only used for command-line tokens) would collide with
    # libc's. Rename it there; callers get newlib's standard strtol.
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/str.c"
      PROPERTIES COMPILE_DEFINITIONS "strtol=n64_strtol")
    # Shown in the homebrew menu. Project Dam is a Switch port of the
    # GoldenEye 007 PC port (https://github.com/jkdansereau/goldeneye-pc-port).
    if(NOT PROJECT_DAM_VERSION)
      set(PROJECT_DAM_VERSION "dev-${VERSION_HASH}")
    endif()
    nx_generate_nacp(ge007.nacp
      NAME    "Project Dam"
      AUTHOR  "Switch port of jkdansereau/goldeneye-pc-port"
      VERSION "${PROJECT_DAM_VERSION}")
    set(_ge_nx_icon "${CMAKE_SOURCE_DIR}/port/switch/icon.jpg")
    if(NOT EXISTS "${_ge_nx_icon}")
      set(_ge_nx_icon "${NX_DEFAULT_ICON}")
    endif()
    nx_create_nro(${target}
      NACP ge007.nacp
      ICON "${_ge_nx_icon}")
  endif()
endmacro()
