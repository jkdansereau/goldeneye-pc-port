#!/usr/bin/env bash
#
# Package a Windows GoldenEye 007 PC port build for distribution.
#
# Run from anywhere, inside the MSYS2 MINGW64 shell, AFTER ./build-pc.sh:
#
#     tools_pc/bundle-win.sh [VERSION]
#
# Produces, under dist/ :
#     goldeneye-pc-port-<VERSION>-win64/        the unpacked bundle
#     goldeneye-pc-port-<VERSION>-win64.zip     + .zip.sha256
#
# The bundle contains ONLY: the engine executable, its MinGW / SDL2 / zlib
# runtime DLLs, a README, and license texts. It contains NO ROM and NO game
# assets (textures, audio, models, levels, text) — the user supplies those at
# runtime from a ROM they own. The script hard-fails if a ROM image or an
# oversized blob ends up inside the bundle.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="${1:-$(git describe --tags --always --dirty 2>/dev/null || echo 0.0.0-dev)}"
VERSION="${VERSION#v}"                                  # strip a leading "v"
NAME="goldeneye-pc-port-${VERSION}-win64"
OUT="dist/${NAME}"
MINGW="${MINGW_PREFIX:-/mingw64}"

EXE="$(ls build-pc/ge007*.exe 2>/dev/null | head -n1 || true)"
[ -n "$EXE" ] || { echo "error: no build-pc/ge007*.exe found — run ./build-pc.sh first" >&2; exit 1; }

echo "==> Bundling $EXE  ->  $NAME"
rm -rf "$OUT"
mkdir -p "$OUT/licenses"
cp "$EXE" "$OUT/"

# --- runtime DLLs ---------------------------------------------------------
# Explicit allowlist; the closure check below fails the build if the exe needs
# a MinGW DLL that is not on this list.
DLLS=(
  SDL2.dll
  zlib1.dll
  libwinpthread-1.dll
  libstdc++-6.dll
  libgcc_s_seh-1.dll
  libssp-0.dll
)
for d in "${DLLS[@]}"; do
  if [ -f "$MINGW/bin/$d" ]; then
    cp "$MINGW/bin/$d" "$OUT/"
    echo "    + $d"
  else
    echo "    . $d (not in $MINGW/bin — skipped)"
  fi
done

# --- verify the dependency closure --------------------------------------
missing=0
if command -v ldd >/dev/null 2>&1; then
  while read -r name _ path _; do
    case "$path" in
      "$MINGW"/*|*/mingw64/*)
        [ -f "$OUT/$name" ] || { echo "    MISSING: $name  ($path)" >&2; missing=1; } ;;
    esac
  done < <(ldd "$EXE")
else
  echo "    (ldd unavailable — skipping closure check)"
fi
[ "$missing" -eq 0 ] || { echo "error: exe needs MinGW DLLs that were not bundled (see above)" >&2; exit 1; }

# --- docs + licenses ---------------------------------------------------
sed "s/@VERSION@/${VERSION}/g" tools_pc/dist/README.md.in > "$OUT/README.md"
cp NOTICE  "$OUT/licenses/NOTICE"
cp LICENSE "$OUT/licenses/LICENSE-port-MIT.txt"
[ -f port/fast3d/LICENSE.txt ] && cp port/fast3d/LICENSE.txt "$OUT/licenses/LICENSE-fast3d.txt"
for l in SDL2 zlib gcc-libs libwinpthread mingw-w64; do
  [ -d "$MINGW/share/licenses/$l" ] && cp -r "$MINGW/share/licenses/$l" "$OUT/licenses/$l"
done

# --- guard: no ROM / game data snuck in --------------------------------
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

# --- zip + checksum --------------------------------------------------
( cd dist && rm -f "${NAME}.zip" "${NAME}.zip.sha256" \
          && zip -qr "${NAME}.zip" "$NAME" \
          && sha256sum "${NAME}.zip" > "${NAME}.zip.sha256" )

echo "==> dist/${NAME}.zip  ($(du -h "dist/${NAME}.zip" | cut -f1))"
cat "dist/${NAME}.zip.sha256"
( cd dist && unzip -l "${NAME}.zip" ) 2>/dev/null || find "$OUT" -type f | sed "s#^dist/##" | sort
