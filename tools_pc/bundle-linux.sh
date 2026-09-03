#!/usr/bin/env bash
#
# Package a Linux GoldenEye 007 PC port build for distribution.
#
# Run from anywhere, AFTER building (./build-pc.sh or a cmake --build):
#
#     tools_pc/bundle-linux.sh [VERSION]
#
# Produces, under dist/ :
#     goldeneye-pc-port-<VERSION>-linux-x86_64/        the unpacked bundle
#     goldeneye-pc-port-<VERSION>-linux-x86_64.tar.gz  + .tar.gz.sha256
#
# The bundle contains ONLY: the engine executable, a README, license texts, and
# the prepare-assets/ tool. It contains NO ROM and NO game assets, and — unlike
# the Windows bundle — NO bundled shared libraries: the user installs SDL2 / zlib
# / libGL from their distro (see the README this generates). The script hard-
# fails if a ROM image or an oversized blob ends up inside the bundle.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="${1:-$(git describe --tags --always --dirty 2>/dev/null || echo 0.0.0-dev)}"
VERSION="${VERSION#v}"                                  # strip a leading "v"
NAME="goldeneye-pc-port-${VERSION}-linux-x86_64"
OUT="dist/${NAME}"

# Accept a build from either the CI Linux tree or a local build-pc/ tree.
EXE=""
for cand in build-linux/ge007.x86_64 build-pc/ge007.x86_64 build-linux/ge007* build-pc/ge007*; do
  if [ -f "$cand" ] && [ -x "$cand" ] && [[ "$cand" != *.exe ]]; then EXE="$cand"; break; fi
done
[ -n "$EXE" ] || { echo "error: no Linux ge007 binary found in build-linux/ or build-pc/ — build first" >&2; exit 1; }

echo "==> Bundling $EXE  ->  $NAME"
rm -rf "$OUT"
mkdir -p "$OUT/licenses"
cp "$EXE" "$OUT/"
chmod +x "$OUT/$(basename "$EXE")"

# --- report the dynamic-library needs (informational; not bundled) -----
if command -v ldd >/dev/null 2>&1; then
  echo "    dynamic dependencies (must be present on the target system):"
  ldd "$EXE" | sed 's/^/      /' || true
fi

# --- docs + licenses --------------------------------------------------
EXE_NAME="$(basename "$EXE")"
DEPS_BLOCK=$'## 1a. Install the runtime libraries\n\nThis Linux build links against your distro\'s SDL2, zlib and OpenGL. Install\nthem first:\n\n```\n# Debian / Ubuntu\nsudo apt install libsdl2-2.0-0 zlib1g libgl1\n\n# Fedora\nsudo dnf install SDL2 zlib libglvnd-glx\n\n# Arch\nsudo pacman -S sdl2 zlib libglvnd\n```\n'
sed -e "s|@VERSION@|${VERSION}|g" \
    -e "s|@PLATFORM@|Linux x86-64|g" \
    -e "s|@EXE@|${EXE_NAME}|g" \
    -e "s|@LICENSE_EXTRA@||g" \
    tools_pc/dist/README.md.in > "$OUT/README.md.tmp"
# @DEPS@ is a multi-line block — substitute it via awk, not sed.
awk -v repl="$DEPS_BLOCK" '{ if ($0 == "@DEPS@") print repl; else print }' \
    "$OUT/README.md.tmp" > "$OUT/README.md"
rm -f "$OUT/README.md.tmp"

cp NOTICE  "$OUT/licenses/NOTICE"
cp LICENSE "$OUT/licenses/LICENSE-port-MIT.txt"
[ -f port/fast3d/LICENSE.txt ] && cp port/fast3d/LICENSE.txt "$OUT/licenses/LICENSE-fast3d.txt"

# --- asset-prep tool (identical assembly to bundle-win.sh) -----------
PREP="$OUT/prepare-assets"
mkdir -p "$PREP/vendor/scripts" "$PREP/vendor/assets/obseg"
cp tools_pc/dist/prepare-assets/prepare-assets.py "$PREP/"
cp tools_pc/d43_emit.py tools_pc/d69_emit.py       "$PREP/"
cp tools_pc/d88_emit.py tools_pc/d88_propdefs.py   "$PREP/"
cp scripts/filelist.u.csv                          "$PREP/vendor/scripts/"
cp assets/obseg/file_resource_table.inc.c          "$PREP/vendor/assets/obseg/"
find assets -iname 'modelfileheader.inc.c' -print0 \
  | xargs -0 -I{} cp --parents {} "$PREP/vendor/"
nmh="$(find "$PREP/vendor/assets" -iname 'modelfileheader.inc.c' | wc -l)"
echo "    + prepare-assets/ (emit scripts + $nmh model headers)"
[ "$nmh" -gt 400 ] || { echo "error: prepare-assets vendored only $nmh model headers — expected ~512" >&2; exit 1; }

# --- guard: no ROM / game data snuck in ------------------------------
if find "$OUT" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' \) | grep -q .; then
  echo "error: bundle contains a ROM image — aborting" >&2
  exit 1
fi
BYTES="$(du -sb "$OUT" | cut -f1)"
LIMIT=$((60 * 1024 * 1024))
if [ "$BYTES" -gt "$LIMIT" ]; then
  echo "error: bundle is $BYTES bytes (> 60 MB) — likely contains game data, aborting" >&2
  exit 1
fi

# --- tarball + checksum ---------------------------------------------
( cd dist && rm -f "${NAME}.tar.gz" "${NAME}.tar.gz.sha256" \
          && tar -czf "${NAME}.tar.gz" "$NAME" \
          && sha256sum "${NAME}.tar.gz" > "${NAME}.tar.gz.sha256" )

echo "==> dist/${NAME}.tar.gz  ($(du -h "dist/${NAME}.tar.gz" | cut -f1))"
cat "dist/${NAME}.tar.gz.sha256"
find "$OUT" -type f | sed "s#^dist/##" | sort
