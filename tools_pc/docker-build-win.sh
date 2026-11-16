#!/bin/sh
# Cross-compile the Windows (x86_64) PC port inside the Dockerfile.windows
# image. Run through compose:
#
#   docker compose run --rm windows                   # ntsc-final
#   docker compose run --rm -e ROMID=pal-final windows
#
# Output: ${BUILD_DIR:-build-win}/ge007*.exe plus the MinGW runtime DLLs it
# needs (SDL2, zlib, libgcc/libstdc++/winpthread), so the directory runs as-is
# on Windows next to the user's ROM (docs/building.md).
set -eu

ROMID="${ROMID:-ntsc-final}"
BUILD_DIR="${BUILD_DIR:-build-win}"
TOOLCHAIN="${TOOLCHAIN:-/usr/share/mingw/toolchain-mingw64.cmake}"
SYSROOT="/usr/x86_64-w64-mingw32/sys-root/mingw"
SYSROOT_BIN="${SYSROOT}/bin"
OBJDUMP="x86_64-w64-mingw32-objdump"

# cmake/FindSDL2.cmake only searches fixed Unix prefixes (re-rooted under the
# sysroot by the toolchain file, where they don't exist) plus $SDL2DIR.
export SDL2DIR="${SDL2DIR:-${SYSROOT}}"

cmake -S . -B "${BUILD_DIR}" -G Ninja \
    -DROMID="${ROMID}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}"
cmake --build "${BUILD_DIR}"

# Copy every non-system DLL the executable imports, transitively, from the
# MinGW sysroot. DLLs absent from the sysroot are Windows system DLLs
# (kernel32, opengl32, dbghelp, ...) and are left alone.
exe="$(ls -t "${BUILD_DIR}"/ge007*.exe | head -n 1)"
todo="${exe}"
seen=" "
while [ -n "${todo}" ]; do
    next=""
    for f in ${todo}; do
        for dll in $("${OBJDUMP}" -p "${f}" | sed -n 's/^[[:space:]]*DLL Name: //p'); do
            case "${seen}" in *" ${dll} "*) continue ;; esac
            seen="${seen}${dll} "
            if [ -f "${SYSROOT_BIN}/${dll}" ]; then
                cp -f "${SYSROOT_BIN}/${dll}" "${BUILD_DIR}/"
                echo "bundled ${dll}"
                next="${next} ${SYSROOT_BIN}/${dll}"
            fi
        done
    done
    todo="${next# }"
done

echo "==> ${exe}"
