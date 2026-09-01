#!/usr/bin/env bash
#
# Build the GoldenEye 007 PC port.
#
# Usage:
#   ./build-pc.sh [ntsc-final|pal-final|jpn-final]
#
# Dependencies (see docs/internals.md):
#   - CMake >= 3.16
#   - SDL2 dev
#   - zlib dev
#   - OpenGL dev (opengl32 on Windows, GL on Linux, OpenGL.framework on macOS)
#
# Example (Linux):
#   sudo apt install cmake libsdl2-dev zlib1g-dev libgl4-mesa-dev
# Example (macOS):
#   brew install cmake sdl2 zlib
# Example (Windows/MSYS2):
#   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 \
#             mingw-w64-x86_64-zlib mingw-w64-x86_64-cmake
#
set -euo pipefail

ROMID="${1:-ntsc-final}"
BUILD_DIR="build-pc"

echo "==> Configuring PC port (ROMID=${ROMID})"
cmake -S . -B "${BUILD_DIR}" -DROMID="${ROMID}"

echo "==> Building"
cmake --build "${BUILD_DIR}" -j

echo "==> Done."
echo "    Binary: ${BUILD_DIR}/ge007.*"
echo "    Put your ROM in ./data/ (see README) and run the binary."
