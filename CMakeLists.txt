# ============================================================================
# GoldenEye 007 — PC port build
#
# This is a PARALLEL build system to the N64 `Makefile`. It compiles the same
# game C sources (src/game, src) for a modern host (Windows/Linux/macOS)
# against the PC shims in `port/`. The N64 build is untouched.
#
# See docs/PCPortResearch.md for the architecture and plan.
# Modelled on the Perfect Dark PC port (perfect-dark-pc-port/perfect_dark).
#
# Usage:
#   cmake -S . -B build-pc -DROMID=ntsc-final
#   cmake --build build-pc -j
# ============================================================================

cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# The decomp uses the `#define inherits struct` pattern (47x in
# src/bondtypes.h): a nested `struct Base;` inside a derived struct. GCC
# treats this as embedding the full base struct (correct layout, matches the
# N64/IDO behaviour the decomp relies on); Clang treats it as a bare forward
# declaration (no members) and rejects base-member access. So prefer GCC.
# (GCC does hard-error on the one struct that declares a member with the same
# name as an inherited one — CCTVRecord's `pad` — see docs/PCPortResearch.md.)
# An explicit -DCMAKE_C_COMPILER=... always wins.
if(NOT DEFINED CMAKE_C_COMPILER)
  find_program(_GE007_GCC NAMES gcc)
  if(_GE007_GCC)
    set(CMAKE_C_COMPILER "${_GE007_GCC}" CACHE FILEPATH "C compiler" FORCE)
    get_filename_component(_GE007_GCC_DIR "${_GE007_GCC}" DIRECTORY)
    find_program(_GE007_GXX NAMES g++ PATHS "${_GE007_GCC_DIR}" NO_DEFAULT_PATH)
    if(_GE007_GXX)
      set(CMAKE_CXX_COMPILER "${_GE007_GXX}" CACHE FILEPATH "CXX compiler" FORCE)
    endif()
  endif()
endif()

if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
  else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
  endif()
endif()

project(ge007 C CXX)

include(TargetArch)
include(FindPkgConfig)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
# Which ROM the executable expects in the data/ dir.
set(ROMID "ntsc-final" CACHE STRING "ROM id: ntsc-final (US), pal-final (EU), jpn-final (JP)")

# ----------------------------------------------------------------------------
# Target arch / platform
# ----------------------------------------------------------------------------
target_architecture(TARGET_ARCH)
if(TARGET_ARCH MATCHES "64")
  set(TARGET_IS_64BIT TRUE)
else()
  set(TARGET_IS_64BIT FALSE)
endif()
message(STATUS "Target arch: ${TARGET_ARCH} (64bit=${TARGET_IS_64BIT})")

if(WIN32)
  set(TARGET_OS "windows")
elseif(APPLE)
  set(TARGET_OS "osx")
elseif(UNIX)
  set(TARGET_OS "linux")
else()
  message(FATAL_ERROR "Unknown target OS")
endif()
set(TARGET_PLATFORM "${TARGET_ARCH}-${TARGET_OS}")
message(STATUS "Target platform: ${TARGET_PLATFORM}")

# Region -> compile definitions.
#
# The game code #ifdefs a full per-region macro set (see the N64 Makefile,
# the VERSION blocks). e.g. VERSION_EU is used in 60+ files, LEFTOVERDEBUG in
# 26, BUGFIX_R1 in 21. We MUST emit the same set the N64 build uses for the
# matching region, or the PC build takes different conditional branches and
# silently diverges (and fails to link). These mirror Makefile LCDEFS exactly.
if(ROMID STREQUAL "ntsc-final")
  set(VERSION_IS_PAL 0)
  set(BIN_REGION "")
  set(REGION_DEFS
    -DVERSION_US -DLANG_US -DREFRESH_NTSC
    -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DBYTEMATCH)
elseif(ROMID STREQUAL "pal-final")
  set(VERSION_IS_PAL 1)
  set(BIN_REGION ".pal")
  set(REGION_DEFS
    -DVERSION_EU -DLANG_EU -DREFRESH_PAL
    -DBUGFIX_R1 -DBUGFIX_R2 -DBYTEMATCH)
elseif(ROMID STREQUAL "jpn-final")
  set(VERSION_IS_PAL 0)
  set(BIN_REGION ".jpn")
  set(REGION_DEFS
    -DVERSION_JP -DLANG_JP -DREFRESH_NTSC
    -DBUGFIX_R1 -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBYTEMATCH)
else()
  message(FATAL_ERROR "Unknown ROMID: ${ROMID}")
endif()

# git hash for version info
execute_process(COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT VERSION_HASH)
  set(VERSION_HASH "unknown")
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in"
  "${CMAKE_BINARY_DIR}/port/include/versioninfo.h" @ONLY)

# Host <string.h> path for port/shim/string.h. The N64 include/string.h
# declares strlen/strchr with `const unsigned char *`, which conflicts with
# the host declarations that SDL (and other system headers) pull in, and it
# sits earlier on the include path than the toolchain's own string.h — so the
# shim cannot use #include_next (it would find the N64 header first). Derive
# the toolchain include dir from the compiler location (MinGW/MSYS2 layout:
# bin/../include) and generate an absolute-path include.
get_filename_component(_PORT_GCC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(PORT_HOST_STRING_H "${_PORT_GCC_DIR}/../include/string.h" ABSOLUTE)
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hoststring.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hoststring.h" @ONLY)

# ----------------------------------------------------------------------------
# Compiler definitions & flags
# ----------------------------------------------------------------------------
add_definitions(
  -DPORT=1
  -D_LANGUAGE_C=1
  -DAVOID_UB=1
  -DPAL=${VERSION_IS_PAL}
  ${REGION_DEFS}
)
if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

# The decomp is big-endian N64 code with unaligned accesses; these flags are
# required to compile it cleanly on a little-endian host (same as the PD port).
add_compile_options(
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -include "versioninfo.h"
)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-Og)
endif()

add_compile_options(
  -Wall
  -Wno-address
  -Wno-misleading-indentation
  -Wno-missing-braces
  -Wno-multichar
  -Wno-tautological-compare
  -Wno-unused-but-set-variable
  -Wno-unused-value
  -Wno-unused-variable
)
# The decomp was written for IDO's leniency. These downgrade hard errors to
# warnings so the unmodified game code compiles; each class is a tracked
# decomp finding (see docs/PCPortResearch.md):
#   int-conversion: raw 32-bit ROM address constants assigned to pointers
#     (WIP decomp; genuine 64-bit hazards to fix upstream as phases progress)
#   incompatible-pointer-types: Mtxf union vs float[4][4], Gfx array idioms
#   implicit-function-declaration: missing header declarations (e.g.
#     get_ptr_long_watch_text_for_item, defined in gunfire.c)
add_compile_options(
  -Wno-error=int-conversion
  -Wno-error=incompatible-pointer-types
  -Wno-error=implicit-function-declaration
)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>)

# port/shim first: PC-only shadow headers (bondtypes.h, bondconstants.h,
# PR/gbi.h) that fix N64-compiler assumptions without touching src/ or
# include/. Inert in the N64 build (its Makefile never sees port/shim).
include_directories(
  "${CMAKE_SOURCE_DIR}/port/shim"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/include/PR"
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/game"
  "${CMAKE_SOURCE_DIR}/src/libultra"
  "${CMAKE_SOURCE_DIR}/src/libultra/audio"
  "${CMAKE_SOURCE_DIR}/port/include"
  "${CMAKE_BINARY_DIR}/port/include"
)

# ----------------------------------------------------------------------------
# Dependencies: SDL2, zlib, OpenGL
# ----------------------------------------------------------------------------
set(LIBS "")

find_package(SDL2 REQUIRED)
include_directories(SYSTEM "${SDL2_INCLUDE_DIR}")
list(APPEND LIBS "${SDL2_LIBRARY}")

find_package(ZLIB REQUIRED)
if(ZLIB_INCLUDE_DIR)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIR}")
endif()
list(APPEND LIBS "${ZLIB_LIBRARY}")

if(NOT GL_LIBRARY)
  if(WIN32)
    set(GL_LIBRARY opengl32)
  elseif(APPLE)
    set(GL_LIBRARY "-framework OpenGL")
  else()
    set(GL_LIBRARY GL)
  endif()
endif()
list(APPEND LIBS "${GL_LIBRARY}")

if(NOT EXTRA_LIBRARIES)
  if(WIN32)
    set(EXTRA_LIBRARIES dbghelp -static-libstdc++)
  else()
    set(EXTRA_LIBRARIES stdc++ m dl pthread)
  endif()
endif()
list(APPEND LIBS "${EXTRA_LIBRARIES}")

# ----------------------------------------------------------------------------
# Sources
# ----------------------------------------------------------------------------
# Port layer (shims + software RSP).
file(GLOB_RECURSE SRC_PORT      "${CMAKE_SOURCE_DIR}/port/src/*.c")
file(GLOB_RECURSE SRC_PORT_CXX  "${CMAKE_SOURCE_DIR}/port/src/*.cpp")
file(GLOB_RECURSE SRC_FAST3D    "${CMAKE_SOURCE_DIR}/port/fast3d/*.cpp")

# Game code — compiled unmodified.
file(GLOB_RECURSE SRC_GAME "${CMAKE_SOURCE_DIR}/src/game/*.c")

# libaudio (AL) — runs on the CPU, platform independent.
file(GLOB SRC_LIBAUDIO "${CMAKE_SOURCE_DIR}/src/libultra/audio/*.c")
# Rare's audio "New" driver (drvrNew/env/reverb) — synthesizer.c calls
# alSaveNew/alAuxBusNew/alMainBusNew/alLoadNew/alResampleNew/alEnvmixerNew/
# alEnvmixerParam/alFxParam, all defined here. Runs on the CPU; must be linked.
file(GLOB SRC_LIBULTRARE_AUDIO "${CMAKE_SOURCE_DIR}/src/libultrare/audio/*.c")
# gu matrix helpers.
file(GLOB SRC_GU "${CMAKE_SOURCE_DIR}/src/libultra/gu/*.c")

# VI mode table (DATA, not a driver): fr.c references osViModeTable ~15x.
# This is the only libultra/io file we compile — the rest are hardware
# drivers that get shimmed in port/src/libultra.c.
set(SRC_LIBULTRARE_DATA
  src/libultrare/io/vitbl.c
)
list(PREPEND SRC_LIBULTRARE_DATA "${CMAKE_SOURCE_DIR}")

# IDO printf engine: src/sprintf.c's sprintf() calls _Printf(prout, dst, fmt,
# va_list), the internal formatter from libultrare/libc (pure C, no hardware).
# xlitob/xldtob are its integer-conversion helpers (_Litob/_Ldtob). The
# _Printf prototype clash between xstdio.h and xprintf.c is resolved by the
# port/shim/libc/xstdio.h shadow header.
set(SRC_LIBULTRARE_LIBC
  src/libultrare/libc/xprintf.c
  src/libultra/libc/xlitob.c
  src/libultra/libc/xldtob.c
)
list(PREPEND SRC_LIBULTRARE_LIBC "${CMAKE_SOURCE_DIR}")

# ----------------------------------------------------------------------------
# Asset data — decompiled ROM arrays (pure data, no logic).
#
# Single-file assets compile unmodified. The per-model / per-level .c files
# under assets/obseg/{bg,brief,setup,stan} are NOT compiled: each one defines
# file-local globals with generic names (`header`, `room_data_table`,
# `intro`, ...) that the N64 build keeps apart by linking every file into its
# own ELF (see assets/obseg/Makefile.*). In a single host link they collide.
# The game loads those blobs from ROM at runtime anyway; their top-level
# symbols (C*Z, G*Z, P*Z, Tbg_*Z, U*Z, L*E/L*J, bg_*_all_p_seg, ...) are
# stubbed in port/src/assetstubs.c until Phase 2 ROM loading. The per-model
# data itself (chr/*/Model.c etc.) is gitignored ROM-derived and absent from
# this checkout. See docs/PCPortResearch.md §11.
set(SRC_ASSETS
  assets/animationtable_data.c
  assets/animationtable_entries.c
  assets/font_chardatae.c
  assets/font_chardataj.c
  assets/font_dl.c
  assets/oddtextures.c
  assets/rarewarelogo.c
)
list(PREPEND SRC_ASSETS "${CMAKE_SOURCE_DIR}")

# Engine files in src/ that are platform independent.
# NOTE: this list is a starting point and must be curated during Phase 1.
#
# INCLUDED here (platform-independent or CPU-side):
#   init.c    — provides mainproc() (the game's real main entry) + the thread
#               setup helpers. Its N64-only init() is compiled but NOT called;
#               the N64 symbols init() references are stubbed in
#               port/src/n64stubs.c (see A3 in docs/PCPortResearch.md).
#   cfb.c     — defines cfb_16 (the framebuffer), referenced by fr.c and game
#               code (bondview2.c, front.c). Pure data.
#   stacks.c  — thread-stack arrays (sp_boot/sp_rmon/sp_idle/sp_shed/sp_main/
#               sp_audi + sp_debug under LEFTOVERDEBUG). Pure data; referenced
#               by init.c (thread setup) and audi.c (sp_audi). The SP_*_SZ
#               sizes in bondconstants.h match these exactly, so sizeof() is
#               correct. No duplicate-definition risk (all other refs are
#               extern decls in bondgame.h / audi.c).
#
# EXCLUDED (hardware drivers, replaced by port/ shims):
#   sched.c crash.c rmon.c usb.c tlb_*.c pi.c vi.c motor.c
#   (there is no src/spectrum.c in this decomp)
#   All of src/libultra/io/*.c and src/libultrare/io/*.c EXCEPT vitbl.c.
#   motor.c / pfsinit.c / pfsisplug.c are NOT compiled: joy.c calls
#   osMotorInit/Start/Stop and osPfsInit/osPfsIsPlug only for accessory
#   (Rumble Pak / Memory Pak) detection, so they are shimmed as no-ops in
#   port/src/libultra.c (see B2 — saves are EEPROM, not PFS).
set(SRC_ENGINE
  src/init.c
  src/audi.c
  src/snd.c
  src/music.c
  src/joy.c
  src/fr.c
  src/cfb.c
  src/stacks.c
  src/boss.c
  src/mema.c
  src/memp.c
  src/deb.c
  src/debugmenu.c
  src/speed_graph.c
  src/token.c
  src/str.c
  src/sprintf.c
  src/ramrom.c
)
list(PREPEND SRC_ENGINE "${CMAKE_SOURCE_DIR}")

set(SRC
  ${SRC_GAME}
  ${SRC_PORT}
  ${SRC_PORT_CXX}
  ${SRC_FAST3D}
  ${SRC_LIBAUDIO}
  ${SRC_LIBULTRARE_AUDIO}
  ${SRC_LIBULTRARE_DATA}
  ${SRC_LIBULTRARE_LIBC}
  ${SRC_ASSETS}
  ${SRC_GU}
  ${SRC_ENGINE}
)

# ----------------------------------------------------------------------------
# Target
# ----------------------------------------------------------------------------
set(BIN_NAME "ge007${BIN_REGION}.${TARGET_ARCH}")
add_executable(ge007 ${SRC})
target_link_libraries(ge007 ${LIBS})
set_target_properties(ge007 PROPERTIES OUTPUT_NAME "${BIN_NAME}")
if(WIN32)
  set_target_properties(ge007 PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

message(STATUS "Output binary: ${BIN_NAME}")
