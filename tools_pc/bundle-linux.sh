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
# The bundle contains ONLY: the engine executable, its SDL2 library, a README,
# license texts, and the prepare-assets/ tool. It contains NO ROM and NO game
# assets. SDL2 is bundled (copied next to the exe, whose rpath is set to
# $ORIGIN) so the tarball runs as-is on any distro — including a sideloaded
# Steam Deck — with nothing installed; zlib and libGL are expected from the
# system (preinstalled everywhere that matters, incl. SteamOS). The script
# hard-fails if patchelf is missing or if a ROM image / oversized blob ends up
# inside the bundle.
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
EXE_NAME="$(basename "$EXE")"

# --- bundle SDL2 (turnkey on any distro / Steam Deck) ------------------
# SDL2 is the engine's only non-universal runtime dep. Copy the exact file
# this exe was linked against next to it and point the rpath at $ORIGIN so
# the bundled copy wins over whatever the target system has.
command -v patchelf >/dev/null 2>&1 || {
  echo "error: patchelf is required to bundle SDL2 (apt install patchelf / pacman -S patchelf)" >&2; exit 1; }
SDL2_LIB="$(ldd "$EXE" | awk '/libSDL2/ {print $3; exit}')"
[ -n "$SDL2_LIB" ] && [ -f "$SDL2_LIB" ] || {
  echo "error: cannot locate libSDL2 for $EXE (unexpected ldd output)" >&2; exit 1; }
cp -L "$SDL2_LIB" "$OUT/"
chmod 644 "$OUT/$(basename "$SDL2_LIB")"
patchelf --set-rpath '$ORIGIN' "$OUT/$EXE_NAME"
# ldd reports absolute paths, so match both the relative $OUT and its
# absolute form ($(pwd) == $REPO_ROOT after the cd above).
RESOLVED="$(ldd "$OUT/$EXE_NAME" | awk '/libSDL2/ {print $3; exit}')"
case "$RESOLVED" in
  "$OUT"/*|"$REPO_ROOT/$OUT"/*) echo "    + $(basename "$SDL2_LIB") (bundled; rpath \$ORIGIN)" ;;
  *) echo "error: bundled libSDL2 is not picked up via \$ORIGIN (ldd -> ${RESOLVED:-nothing})" >&2; exit 1 ;;
esac

# --- report the remaining dynamic-library needs (system-provided) ------
if command -v ldd >/dev/null 2>&1; then
  echo "    remaining system dependencies (zlib / libGL / libc — preinstalled on desktop distros and SteamOS):"
  ldd "$OUT/$EXE_NAME" | grep -v "/${NAME}/" | sed 's/^/      /' || true
fi

# --- docs + licenses --------------------------------------------------
DEPS_BLOCK=$'## 1a. Runtime libraries\n\nSDL2 is bundled in this folder and found automatically (the executable\npoints at its own directory first). zlib and OpenGL come from your system —\npreinstalled on every desktop distro and on SteamOS / Steam Deck.\n\n**Steam Deck:** sideload this folder (USB or a file manager), do steps 2–4\nbelow, then add `@EXE@` to Games → *Add Game* as a non-Steam game.\n'
# @DECK@: the in-game settings (F10 overlay) controller-mapping guide — only
# meaningful where Steam input mapping exists (Deck / Linux); Windows bundles
# substitute it with nothing.
DECK_BLOCK=$'## 4a. Steam Deck — in-game settings (options overlay)\n\nThe options overlay is fully gamepad-driven: it opens with **Select**, the\nD-pad or left stick (up/down) moves between options, **A** steps the selected\noption forward, **B** steps it back, and **Start** (or Select again) closes.\nToggles flip, resolution / MSAA / filtering cycle, sliders step in\nincrements. With a keyboard attached the same overlay is `F10` + arrows/Enter.\n\n'
sed -e "s|@VERSION@|${VERSION}|g" \
    -e "s|@PLATFORM@|Linux x86-64|g" \
    -e "s|@EXE@|${EXE_NAME}|g" \
    -e "s|@LICENSE_EXTRA@||g" \
    tools_pc/dist/README.md.in > "$OUT/README.md.tmp"
# @DEPS@ / @DECK@ are multi-line blocks — substitute them via awk, not sed.
awk -v deps="$DEPS_BLOCK" -v deck="$DECK_BLOCK" \
     '{ if ($0 == "@DEPS@") print deps; else if ($0 == "@DECK@") print deck; else print }' \
    "$OUT/README.md.tmp" > "$OUT/README.md"
rm -f "$OUT/README.md.tmp"

cp NOTICE  "$OUT/licenses/NOTICE"
cp LICENSE "$OUT/licenses/LICENSE-port-MIT.txt"
[ -f port/fast3d/LICENSE.txt ] && cp port/fast3d/LICENSE.txt "$OUT/licenses/LICENSE-fast3d.txt"
# SDL2 ships under the zlib license; carry the distro's copyright file when
# it exists (Debian/Ubuntu), skip silently elsewhere.
SDL2_LIC="$(dpkg -L libsdl2-2.0-0 2>/dev/null | grep -m1 '/copyright$' || true)"
[ -n "$SDL2_LIC" ] && [ -f "$SDL2_LIC" ] && cp "$SDL2_LIC" "$OUT/licenses/SDL2.txt"

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

# Frozen first-run converter (drop-in ROM support): the engine spawns this
# when data/pcmodels-* / data/pccg-* are missing (port/src/romconvert.c).
# CI builds it with PyInstaller before bundling; a local bundle needs the
# same step:  python3 -m PyInstaller --onefile --name ge007-convert \
#                --distpath tools_pc/dist/prepare-assets \
#                tools_pc/dist/prepare-assets/prepare-assets.py
CV="$REPO_ROOT/tools_pc/dist/prepare-assets/ge007-convert"
[ -f "$CV" ] || { echo "error: $CV not found — freeze it with PyInstaller first (see above)" >&2; exit 1; }
cp "$CV" "$PREP/"
chmod 755 "$PREP/ge007-convert"
echo "    + prepare-assets/ge007-convert ($(du -h "$CV" | cut -f1))"

# --- guard: no ROM / game data snuck in ------------------------------
if find "$OUT" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' \) | grep -q .; then
  echo "error: bundle contains a ROM image — aborting" >&2
  exit 1
fi
# 120 MB: engine + SDL2 are a few MB; the frozen ge007-convert (PyInstaller
# --onefile CPython) adds ~15 MB. Anything far beyond that is game data.
BYTES="$(du -sb "$OUT" | cut -f1)"
LIMIT=$((120 * 1024 * 1024))
if [ "$BYTES" -gt "$LIMIT" ]; then
  echo "error: bundle is $BYTES bytes (> 120 MB) — likely contains game data, aborting" >&2
  exit 1
fi

# --- tarball + checksum ---------------------------------------------
( cd dist && rm -f "${NAME}.tar.gz" "${NAME}.tar.gz.sha256" \
          && tar -czf "${NAME}.tar.gz" "$NAME" \
          && sha256sum "${NAME}.tar.gz" > "${NAME}.tar.gz.sha256" )

echo "==> dist/${NAME}.tar.gz  ($(du -h "dist/${NAME}.tar.gz" | cut -f1))"
cat "dist/${NAME}.tar.gz.sha256"
find "$OUT" -type f | sed "s#^dist/##" | sort
