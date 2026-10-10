#!/usr/bin/env bash
#
# Package a macOS (Apple Silicon) GoldenEye 007 PC port build for distribution.
# Runs ON a Mac, after the build:
#
#     scripts/package-macos.sh BUILD_DIR OUT_DIR [VERSION]
#
#   BUILD_DIR  cmake build directory containing ge007.aarch64
#   OUT_DIR    where to write the bundle + tarball (created if missing)
#   VERSION    defaults to `git describe`
#
# Produces, under OUT_DIR:
#     goldeneye-pc-port-<VERSION>-macos-arm64/        the unpacked bundle
#     goldeneye-pc-port-<VERSION>-macos-arm64.tar.gz  + .tar.gz.sha256
#
# Same shape as tools_pc/bundle-linux.sh: engine exe, the non-system dylibs it
# needs (copied next to the exe, install names rewritten to @executable_path /
# @loader_path so nothing resolves into Homebrew), README, license texts, the
# prepare-assets tool and tools/eep_convert.py. NO ROM and NO game data.
# Every touched Mach-O is re-signed ad-hoc (mandatory on arm64 after
# install_name_tool). The bundle is NOT notarized; see the README it writes.
#
# Env: GE_REPO_ROOT overrides the repo root (default: parent of this script).
#      GE_ALLOW_NO_CONVERTER=1 permits packaging without the PyInstaller-frozen
#      ge007-convert (local testing only; CI always freezes it).
set -euo pipefail

[ "$(uname -s)" = "Darwin" ] || { echo "error: this script runs on macOS only" >&2; exit 1; }
[ $# -ge 2 ] || { echo "usage: $0 BUILD_DIR OUT_DIR [VERSION]" >&2; exit 1; }
BUILD_DIR="$1"; OUT_DIR="$2"
[ -d "$BUILD_DIR" ] || { echo "error: build dir not found: $BUILD_DIR" >&2; exit 1; }
mkdir -p "$OUT_DIR"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"; OUT_DIR="$(cd "$OUT_DIR" && pwd)"

REPO_ROOT="${GE_REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$REPO_ROOT"

VERSION="${3:-$(git describe --tags --always --dirty 2>/dev/null || echo 0.0.0-dev)}"
VERSION="${VERSION#v}"
NAME="goldeneye-pc-port-${VERSION}-macos-arm64"
OUT="${OUT_DIR}/${NAME}"

EXE=""
for cand in "$BUILD_DIR"/ge007.aarch64 "$BUILD_DIR"/ge007*; do
  if [ -f "$cand" ] && [ -x "$cand" ]; then EXE="$cand"; break; fi
done
[ -n "$EXE" ] || { echo "error: no ge007 binary in $BUILD_DIR - build first" >&2; exit 1; }
EXE_NAME="$(basename "$EXE")"
file "$EXE" | grep -q arm64 || { echo "error: $EXE is not an arm64 Mach-O" >&2; exit 1; }

echo "==> Bundling $EXE  ->  $NAME"
rm -rf "$OUT"
mkdir -p "$OUT/licenses"
cp "$EXE" "$OUT/$EXE_NAME"
chmod 755 "$OUT/$EXE_NAME"

# --- dylib bundling ----------------------------------------------------
is_system() { case "$1" in /usr/lib/*|/System/*) return 0 ;; *) return 1 ;; esac; }

deps_of() { otool -L "$1" | tail -n +2 | awk '{print $1}'; }
rpaths_of() { otool -l "$1" | awk '/cmd LC_RPATH/ {f=1} f && /path / {print $2; f=0}'; }

# Resolve an install name to a real file, searching the dependent's rpaths and
# the Homebrew gcc runtime dir (libgcc_s / libstdc++ are @rpath-linked there).
resolve() {
  local dep="$1" from="$2" r cand
  case "$dep" in
    @rpath/*)
      for r in $(rpaths_of "$from"); do
        cand="${r/@loader_path/$(dirname "$from")}"
        cand="${cand/@executable_path/$(dirname "$from")}/${dep#@rpath/}"
        [ -f "$cand" ] && { echo "$cand"; return 0; }
      done
      for cand in /opt/homebrew/opt/gcc/lib/gcc/current/"${dep#@rpath/}" \
                  /opt/homebrew/lib/"${dep#@rpath/}" /usr/local/lib/"${dep#@rpath/}"; do
        [ -f "$cand" ] && { echo "$cand"; return 0; }
      done
      return 1 ;;
    @loader_path/*)
      cand="$(dirname "$from")/${dep#@loader_path/}"
      [ -f "$cand" ] && { echo "$cand"; return 0; }
      return 1 ;;
    /*) [ -f "$dep" ] && { echo "$dep"; return 0; }; return 1 ;;
  esac
  return 1
}

BUNDLED=" "
SIGN_LIST=()
bundle_dep() {  # $1 = file (already inside $OUT) whose deps to process; $2 = new-name prefix
  local file="$1" prefix="$2" dep real base
  while IFS= read -r dep; do
    is_system "$dep" && continue
    case "$dep" in "@executable_path/"*|"@loader_path/"*) continue ;; esac
    real="$(resolve "$dep" "$file")" || { echo "error: cannot resolve $dep (needed by $file)" >&2; exit 1; }
    real="$(cd "$(dirname "$real")" && pwd -P)/$(basename "$real")"
    base="$(basename "$dep")"
    if [[ "$BUNDLED" != *" $base "* ]]; then
      BUNDLED+="$base "
      cp -L "$real" "$OUT/$base"; chmod 644 "$OUT/$base"
      install_name_tool -id "@loader_path/$base" "$OUT/$base" 2>/dev/null
      echo "    + $base  (from $real)"
      SIGN_LIST+=("$OUT/$base")
      bundle_dep "$OUT/$base" "@loader_path"
    fi
    install_name_tool -change "$dep" "$prefix/$base" "$file" 2>/dev/null
  done < <(deps_of "$file")
}

# Drop any rpath that points outside the bundle (Homebrew, build tree).
strip_rpaths() { local r; for r in $(rpaths_of "$1"); do install_name_tool -delete_rpath "$r" "$1" 2>/dev/null; done; }

# Homebrew's sdl2 is sdl2-compat: libSDL2 dlopen()s @loader_path/libSDL3.dylib at runtime, so it
# is not in otool -L. Bundle it too; sdl2-compat looks next to itself first.
bundle_sdl3_if_needed() {
  local sdl2 sdl3="" d
  sdl2="$(ls "$OUT"/libSDL2-*.dylib 2>/dev/null | head -1 || true)"
  [ -n "$sdl2" ] || return 0
  strings -a "$sdl2" | grep -q 'libSDL3' || return 0
  for d in /opt/homebrew/opt/sdl3/lib /opt/homebrew/lib /usr/local/lib; do
    if [ -f "$d/libSDL3.0.dylib" ]; then sdl3="$d/libSDL3.0.dylib"; break; fi
  done
  [ -n "$sdl3" ] || { echo "error: sdl2-compat found but libSDL3.0.dylib is not installed" >&2; exit 1; }
  cp -L "$sdl3" "$OUT/libSDL3.dylib"; chmod 644 "$OUT/libSDL3.dylib"
  install_name_tool -id "@loader_path/libSDL3.dylib" "$OUT/libSDL3.dylib" 2>/dev/null
  echo "    + libSDL3.dylib  (sdl2-compat runtime dependency, from $sdl3)"
  SIGN_LIST+=("$OUT/libSDL3.dylib")
  bundle_dep "$OUT/libSDL3.dylib" "@loader_path"
}

bundle_dep "$OUT/$EXE_NAME" "@executable_path"
bundle_sdl3_if_needed
strip_rpaths "$OUT/$EXE_NAME"
for f in ${SIGN_LIST[@]+"${SIGN_LIST[@]}"}; do strip_rpaths "$f"; done

# Ad-hoc sign everything we rewrote (libs first, exe last). Without this an
# arm64 binary is killed on launch ("Killed: 9").
for f in ${SIGN_LIST[@]+"${SIGN_LIST[@]}"} "$OUT/$EXE_NAME"; do
  codesign --force -s - "$f" 2>&1 | sed 's/^/    /'
done

# Guard: nothing may still point into Homebrew / the build machine.
for f in ${SIGN_LIST[@]+"${SIGN_LIST[@]}"} "$OUT/$EXE_NAME"; do
  if otool -L "$f" | tail -n +2 | awk '{print $1}' | grep -Eq '^(/opt/|/usr/local/|/Users/|/private/|@rpath/)'; then
    echo "error: $f still references a non-system path:" >&2; otool -L "$f" >&2; exit 1
  fi
  codesign --verify "$f" || { echo "error: bad signature on $f" >&2; exit 1; }
done
echo "    remaining system dependencies:"
otool -L "$OUT/$EXE_NAME" | tail -n +2 | awk '{print "      " $1}' | grep -v '@executable_path' || true

# --- docs + licenses ---------------------------------------------------
DEPS_BLOCK=$'## macOS (Apple Silicon, experimental)\n\nThis is an experimental build for Macs with Apple Silicon (M1 or later,\nmacOS @MINOS@ or newer). SDL2 and the C++ runtime are bundled in this folder and\nfound automatically; nothing needs installing, and Homebrew is not required.\n\nThe app is not signed with an Apple Developer ID or notarized, so Gatekeeper\nblocks it when this folder came from a download. After unpacking, open Terminal\nin this folder and run, once:\n\n    xattr -dr com.apple.quarantine .\n\nThen run `./@EXE@` from Terminal.\n'
MINOS="$(for f in ${SIGN_LIST[@]+"${SIGN_LIST[@]}"} "$OUT/$EXE_NAME"; do otool -l "$f" | awk '/LC_BUILD_VERSION/ {f=1} f && /minos/ {print $2; exit}'; done | sort -V | tail -1)"
echo "    minimum macOS (highest minos of the bundled Mach-Os): $MINOS"
DEPS_BLOCK="${DEPS_BLOCK//@EXE@/$EXE_NAME}"
DEPS_BLOCK="${DEPS_BLOCK//@MINOS@/$MINOS}"
sed -e "s|@VERSION@|${VERSION}|g" \
    -e "s|@PLATFORM@|macOS Apple Silicon (arm64, experimental)|g" \
    -e "s|@EXE@|${EXE_NAME}|g" \
    -e "s|@LICENSE_EXTRA@||g" \
    tools_pc/dist/README.md.in > "$OUT/README.md.tmp"
# BSD awk rejects newlines in -v; pass the block via the environment.
DEPS_BLOCK="$DEPS_BLOCK" awk '{ if ($0 == "@DEPS@") print ENVIRON["DEPS_BLOCK"]; else if ($0 == "@DECK@") print ""; else print }' \
    "$OUT/README.md.tmp" > "$OUT/README.md"
rm -f "$OUT/README.md.tmp"

cp NOTICE  "$OUT/licenses/NOTICE"
cp LICENSE "$OUT/licenses/LICENSE-port-MIT.txt"
[ -f port/fast3d/LICENSE.txt ] && cp port/fast3d/LICENSE.txt "$OUT/licenses/LICENSE-fast3d.txt"
# SDL2 is zlib-licensed; carry its text when the Homebrew prefix has one.
for d in /opt/homebrew/opt/sdl2/LICENSE.txt /opt/homebrew/opt/sdl2-compat/LICENSE.txt /opt/homebrew/opt/sdl2/share/licenses/*/LICENSE.txt; do
  if [ -f "$d" ]; then cp "$d" "$OUT/licenses/SDL2.txt"; break; fi
done

# --- save converter + asset-prep tool (same assembly as bundle-linux.sh) ---
mkdir -p "$OUT/tools"
cp tools_pc/eep_convert.py "$OUT/tools/"
PREP="$OUT/prepare-assets"
mkdir -p "$PREP/vendor/scripts" "$PREP/vendor/assets/obseg"
cp tools_pc/dist/prepare-assets/prepare-assets.py "$PREP/"
cp tools_pc/d43_emit.py tools_pc/d69_emit.py tools_pc/d88_emit.py tools_pc/d88_propdefs.py "$PREP/"
cp scripts/filelist.u.csv "$PREP/vendor/scripts/"
cp assets/obseg/file_resource_table.inc.c "$PREP/vendor/assets/obseg/"
# BSD cp has no --parents; cpio -pdm recreates the relative paths.
find assets -iname 'modelfileheader.inc.c' -print | cpio -pdm "$PREP/vendor" 2>/dev/null
nmh="$(find "$PREP/vendor/assets" -iname 'modelfileheader.inc.c' | wc -l | tr -d ' ')"
echo "    + prepare-assets/ (emit scripts + $nmh model headers)"
[ "$nmh" -gt 400 ] || { echo "error: prepare-assets vendored only $nmh model headers - expected ~512" >&2; exit 1; }

CV="$REPO_ROOT/tools_pc/dist/prepare-assets/ge007-convert"
if [ -f "$CV" ]; then
  cp "$CV" "$PREP/"; chmod 755 "$PREP/ge007-convert"
  codesign --force -s - "$PREP/ge007-convert" 2>&1 | sed 's/^/    /' || true
  echo "    + prepare-assets/ge007-convert ($(du -h "$CV" | cut -f1))"
elif [ "${GE_ALLOW_NO_CONVERTER:-0}" = "1" ]; then
  echo "    ! ge007-convert missing (allowed by GE_ALLOW_NO_CONVERTER=1)"
else
  echo "error: $CV not found - freeze it with PyInstaller first (see ci.yml macos-build)" >&2; exit 1
fi

# --- guards ------------------------------------------------------------
if find "$OUT" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' \) | grep -q .; then
  echo "error: bundle contains a ROM image - aborting" >&2; exit 1
fi
KIB="$(du -sk "$OUT" | cut -f1)"
[ "$KIB" -le $((120 * 1024)) ] || { echo "error: bundle is ${KIB} KiB (> 120 MB) - likely contains game data" >&2; exit 1; }

# --- tarball + checksum ------------------------------------------------
# COPYFILE_DISABLE keeps AppleDouble ._* files out of the archive.
( cd "$OUT_DIR" && rm -f "${NAME}.tar.gz" "${NAME}.tar.gz.sha256" \
    && COPYFILE_DISABLE=1 tar -czf "${NAME}.tar.gz" "$NAME" \
    && shasum -a 256 "${NAME}.tar.gz" > "${NAME}.tar.gz.sha256" )

echo "==> ${OUT_DIR}/${NAME}.tar.gz  ($(du -h "${OUT_DIR}/${NAME}.tar.gz" | cut -f1))"
cat "${OUT_DIR}/${NAME}.tar.gz.sha256"
( cd "$OUT_DIR" && find "$NAME" -type f | sort )
