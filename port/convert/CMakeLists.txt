# ROM -> PC sidecar converter: library linked into the game (first-run
# conversion, port/src/romconvert.c) + the standalone `ge007-convert` CLI.
#
# Freestanding (libc + zlib). It lives in its own directory scope so none of the
# game's directory-wide settings leak in: the decomp include path (its stdio.h /
# string.h / zlib.h shadow the host ones), the forced `-include versioninfo.h`,
# the region -D macros and the game-specific linker --defsym flags.
set_directory_properties(PROPERTIES
  COMPILE_OPTIONS "" COMPILE_DEFINITIONS "" INCLUDE_DIRECTORIES "")
set(CMAKE_C_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "")

find_package(ZLIB REQUIRED)

file(GLOB CONVERT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(FILTER CONVERT_SRC EXCLUDE REGEX "ge007_convert_cli[.]c$")
add_library(ge007_convert STATIC ${CONVERT_SRC})
target_include_directories(ge007_convert PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(ge007_convert PRIVATE "${ZLIB_INCLUDE_DIR}")
target_compile_options(ge007_convert PRIVATE -O2 -Wall -Wextra)

if(NOT GE_SWITCH)
  add_executable(ge007-convert ge007_convert_cli.c)
  target_compile_options(ge007-convert PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(ge007-convert ge007_convert ${ZLIB_LIBRARIES})
endif()
