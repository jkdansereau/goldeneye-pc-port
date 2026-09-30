#!/usr/bin/env bash
#
# Package a macOS (Apple Silicon / arm64) GoldenEye 007 PC port build as a
# double-clickable .app for distribution.
#
# Run from anywhere, AFTER building (./build-pc.sh or a cmake --build):
#
#     tools_pc/bundle-mac.sh [VERSION]
#
# Produces, under dist/ :
#     GoldenEye.app/                    the app bundle (drop a ROM in to play)
#     goldeneye-pc-port-<VERSION>-macos-arm64.zip   + .zip.sha256
#
# The bundle contains ONLY: the engine executable, its two Homebrew-provided
# runtime libraries (SDL2, libstdc++/libgcc_s), a README, license texts, and
# the prepare-assets/ tool. It contains NO ROM and NO game assets. zlib, Cocoa
# and OpenGL come from macOS itself.
#
# The engine finds its data dir as `<exe dir>/data/` when there is no `data/`
# in the CWD (port/src/system.c sysResolvePath), so the user's ROM + generated
# sidecars live in GoldenEye.app/Contents/MacOS/data/.
#
# arm64 binaries must carry at least an ad-hoc signature to execute, and
# install_name_tool invalidates whatever signature the linker applied — so the
# script re-signs every binary (and the bundle) at the end.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="${1:-$(git describe --tags --always --dirty 2>/dev/null || echo 0.0.0-dev)}"
VERSION="${VERSION#v}"                                  # strip a leading "v"
NAME="goldeneye-pc-port-${VERSION}-macos-arm64"
APP="dist/GoldenEye.app"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"

# Accept a build from either a local build-pc/ tree or a CI macos tree.
EXE=""
for cand in build-macos/ge007.aarch64 build-pc/ge007.aarch64 build-pc/ge007; do
  if [ -f "$cand" ] && [ -x "$cand" ]; then EXE="$cand"; break; fi
done
[ -n "$EXE" ] || { echo "error: no arm64 ge007 binary found in build-macos/ or build-pc/ — build first" >&2; exit 1; }
file "$EXE" | grep -q 'arm64' || { echo "error: $EXE is not an arm64 binary" >&2; exit 1; }

echo "==> Bundling $EXE  ->  $NAME"
rm -rf "$APP" "dist/$NAME" "dist/${NAME}.zip" "dist/${NAME}.zip.sha256"
mkdir -p "$MACOS" "$RES/licenses"

cp "$EXE" "$MACOS/ge007"
chmod +x "$MACOS/ge007"
EXE_NAME="ge007"

# --- bundle the Homebrew runtime deps --------------------------------
# SDL2 and libstdc++ are the only non-system dynamic deps. Copy the exact
# files this exe was linked against next to it and rewrite the load commands
# to @executable_path so the bundled copies win, then make libstdc++ find its
# own libgcc_s via @loader_path.
copy_dep() {  # <path-in-otool-output> -> echoes the bundled basename
  local src="$1" base
  base="$(basename "$src")"
  if [ ! -f "$src" ]; then
    echo "error: cannot locate $src (linked but not on disk)" >&2
    return 1
  fi
  cp -L "$src" "$MACOS/$base"
  chmod 644 "$MACOS/$base"
  echo "$base"
}

SDL2_SRC="$(otool -L "$MACOS/$EXE_NAME" | awk '/libSDL2/ {print $1; exit}')"
STDC_SRC="$(otool -L "$MACOS/$EXE_NAME" | awk '/libstdc\+\+/ {print $1; exit}')"
[ -n "$SDL2_SRC" ] || { echo "error: no libSDL2 in $EXE_NAME's load commands" >&2; exit 1; }
[ -n "$STDC_SRC" ] || { echo "error: no libstdc++ in $EXE_NAME's load commands" >&2; exit 1; }

SDL2_BASE="$(copy_dep "$SDL2_SRC")"
STDC_BASE="$(copy_dep "$STDC_SRC")"
install_name_tool -change "$SDL2_SRC" "@executable_path/$SDL2_BASE" "$MACOS/$EXE_NAME"
install_name_tool -change "$STDC_SRC" "@executable_path/$STDC_BASE" "$MACOS/$EXE_NAME"

# Homebrew's `sdl2` is sdl2-compat, a thin shim over SDL3 that finds its
# backend through a RELATIVE rpath baked into the dylib:
#     @loader_path/../../../../opt/sdl3/lib
# That resolves correctly at /opt/homebrew/opt/sdl2-compat/lib/ and to
# nothing once the dylib is copied into the bundle -- sdl2-compat then shows
# a MODAL ERROR DIALOG from its initializer (dllinit -> error_dialog ->
# NSAlert runModal) and the app hangs before main(). Bundle SDL3 and repoint
# every Homebrew rpath at the bundle directory.
SDL3_SRC="$(dirname "$(readlink -f /opt/homebrew/opt/sdl3/lib/libSDL3.dylib 2>/dev/null || echo /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib)")"
if [ -f /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib ]; then
  cp -L /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib "$MACOS/libSDL3.0.dylib"
  chmod 644 "$MACOS/libSDL3.0.dylib"
  ( cd "$MACOS" && ln -sf libSDL3.0.dylib libSDL3.dylib )
  echo "    + libSDL3.0.dylib (+ libSDL3.dylib symlink; sdl2-compat backend)"
else
  echo "error: SDL3 not found at /opt/homebrew/opt/sdl3/lib — required by sdl2-compat" >&2
  exit 1
fi
# Repoint the bundled SDL2's Homebrew rpaths at @loader_path so the SDL3
# dlopen finds the copy sitting next to it.
while IFS= read -r rp; do
  case "$rp" in
    /opt/*|@loader_path/../../..*)
      install_name_tool -rpath "$rp" "@loader_path" "$MACOS/$SDL2_BASE" && \
        echo "    ~ SDL2 rpath: $rp -> @loader_path" ;;
  esac
done < <(otool -l "$MACOS/$SDL2_BASE" | awk '/LC_RPATH/{getline; getline; sub(/^ *path /,""); sub(/ \(offset.*/,""); print}')
install_name_tool -id "@rpath/$SDL2_BASE" "$MACOS/$SDL2_BASE" 2>/dev/null || true

GCC_S_SRC="$(otool -L "$MACOS/$STDC_BASE" | awk '/libgcc_s/ {print $1; exit}')"
if [ -n "$GCC_S_SRC" ]; then
  if [ ! -f "$GCC_S_SRC" ]; then
    # libstdc++ records it as @rpath/... — resolve against the libstdc++ dir.
    GCC_S_SRC="$(dirname "$STDC_SRC")/$(basename "$GCC_S_SRC")"
  fi
  GCC_S_BASE="$(copy_dep "$GCC_S_SRC")"
  install_name_tool -change "@rpath/$GCC_S_BASE" "@loader_path/$GCC_S_BASE" "$MACOS/$STDC_BASE" 2>/dev/null || \
    install_name_tool -change "$GCC_S_SRC" "@loader_path/$GCC_S_BASE" "$MACOS/$STDC_BASE"
  install_name_tool -id "@rpath/$STDC_BASE" "$MACOS/$STDC_BASE" 2>/dev/null || true
  echo "    + $STDC_BASE + $GCC_S_BASE (bundled)"
else
  echo "    + $STDC_BASE (bundled)"
fi
echo "    + $SDL2_BASE (bundled)"

# --- remaining deps should all be system ------------------------------
echo "    remaining dynamic dependencies (expected: libz + Apple frameworks):"
otool -L "$MACOS/$EXE_NAME" | tail -n +2 | grep -vE "@executable_path|@loader_path" | sed 's/^/      /' || true

# --- Info.plist -------------------------------------------------------
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>GoldenEye 007</string>
	<key>CFBundleDisplayName</key><string>GoldenEye 007</string>
	<key>CFBundleIdentifier</key><string>org.goldeneye.pcport</string>
	<key>CFBundleExecutable</key><string>ge007</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>${VERSION}</string>
	<key>CFBundleVersion</key><string>${VERSION}</string>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
	<key>NSHighResolutionCapable</key><true/>
	<key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
PLIST

# --- docs + licenses --------------------------------------------------
DEPS_BLOCK=$'## 1a. Runtime libraries\n\nNothing to install. SDL2 and the GCC C++ runtime (libstdc++, libgcc_s)\nare bundled inside the app; zlib, Cocoa and OpenGL come from macOS.\n\n**Gatekeeper:** the app is ad-hoc signed (not notarised). On first launch,\nright-click the app and choose *Open*, or run\n`xattr -dr com.apple.quarantine GoldenEye.app`. This is only needed once.\n'
DECK_BLOCK=$'## 4a. Game controller (options overlay)\n\nThe options overlay is fully gamepad-driven: it opens with **Select**, the\nD-pad or left stick (up/down) moves between options, **A** steps the selected\noption forward, **B** steps it back, and **Start** (or Select again) closes.\nToggles flip, resolution / MSAA / filtering cycle, sliders step in\nincrements. With a keyboard attached the same overlay is `F10` + arrows/Enter.\n\n'
# BSD sed/awk reject embedded newlines in -v, so the two multi-line blocks go
# in via sed's `r` (read-file) command at their markers.
DEPS_FILE="$(mktemp)"; DECK_FILE="$(mktemp)"
printf '%s\n' "$DEPS_BLOCK" > "$DEPS_FILE"
printf '%s\n' "$DECK_BLOCK" > "$DECK_FILE"
sed -e "s|@VERSION@|${VERSION}|g" \
    -e "s|@PLATFORM@|macOS arm64 (Apple Silicon)|g" \
    -e "s|@EXE@|GoldenEye.app|g" \
    -e "s|@LICENSE_EXTRA@||g" \
    -e "/@DEPS@/r $DEPS_FILE" -e '/@DEPS@/d' \
    -e "/@DECK@/r $DECK_FILE" -e '/@DECK@/d' \
    tools_pc/dist/README.md.in > "$RES/README.md"
rm -f "$DEPS_FILE" "$DECK_FILE"

cp NOTICE  "$RES/licenses/NOTICE"
cp LICENSE "$RES/licenses/LICENSE-port-MIT.txt"
[ -f port/fast3d/LICENSE.txt ] && cp port/fast3d/LICENSE.txt "$RES/licenses/LICENSE-fast3d.txt"

# --- asset-prep tool (same assembly as the Linux/Windows bundles) -----
# In Contents/Resources/, NOT Contents/MacOS/: codesign treats a directory
# under MacOS/ as nested code and refuses to sign the bundle if it holds
# non-Mach-O files (the vendored .inc.c headers). romconvert.c knows to look
# in ../Resources/prepare-assets/ on macOS.
PREP="$RES/prepare-assets"
mkdir -p "$PREP/vendor/scripts" "$PREP/vendor/assets/obseg"
cp tools_pc/dist/prepare-assets/prepare-assets.py "$PREP/"
cp tools_pc/d43_emit.py tools_pc/d69_emit.py       "$PREP/"
cp tools_pc/d88_emit.py tools_pc/d88_propdefs.py   "$PREP/"
cp scripts/filelist.u.csv                          "$PREP/vendor/scripts/"
cp assets/obseg/file_resource_table.inc.c          "$PREP/vendor/assets/obseg/"
# BSD cp has no --parents; recreate each header's tree under vendor/ by hand.
find assets -iname 'modelfileheader.inc.c' -print | while IFS= read -r f; do
  d="$PREP/vendor/$(dirname "$f")"
  mkdir -p "$d"
  cp "$f" "$d/"
done
nmh="$(find "$PREP/vendor/assets" -iname 'modelfileheader.inc.c' | wc -l)"
echo "    + prepare-assets/ (emit scripts + $nmh model headers)"
[ "$nmh" -gt 400 ] || { echo "error: prepare-assets vendored only $nmh model headers — expected ~512" >&2; exit 1; }

# Frozen first-run converter (drop-in ROM support). Same PyInstaller step as
# Linux/Windows, but a macOS freeze only runs on macOS: if it is not present,
# warn rather than fail — the app still runs with pre-generated sidecars.
CV="$REPO_ROOT/tools_pc/dist/prepare-assets/ge007-convert"
if [ -f "$CV" ]; then
  cp "$CV" "$PREP/"
  chmod 755 "$PREP/ge007-convert"
  echo "    + prepare-assets/ge007-convert ($(du -h "$CV" | cut -f1))"
else
  echo "    ! prepare-assets/ge007-convert not found — the app will only run"
  echo "      with pre-generated sidecars. Freeze it with:"
  echo "        python3 -m PyInstaller --onefile --name ge007-convert \\"
  echo "          --distpath tools_pc/dist/prepare-assets \\"
  echo "          tools_pc/dist/prepare-assets/prepare-assets.py"
fi

# --- guard: no ROM / game data snuck in ------------------------------
if find "$APP" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' \) | grep -q .; then
  echo "error: bundle contains a ROM image — aborting" >&2
  exit 1
fi
BYTES="$(du -sk "$APP" | cut -f1)"
LIMIT=$((120 * 1024))   # KiB
if [ "$BYTES" -gt "$LIMIT" ]; then
  echo "error: bundle is ${BYTES} KiB (> 120 MB) — likely contains game data, aborting" >&2
  exit 1
fi

# --- ad-hoc sign (arm64 requires a signature; install_name_tool voids it) --
if command -v codesign >/dev/null 2>&1; then
  for lib in "$MACOS"/*.dylib; do
    [ -f "$lib" ] && codesign --force --sign - "$lib" 2>/dev/null || true
  done
  codesign --force --sign - "$MACOS/$EXE_NAME"
  # No --deep: it tries to sign the prepare-assets/ .c sources as nested code
  # and fails. The only Mach-O in the bundle is the exe + the dylibs above.
  codesign --force --sign - "$APP"
  echo "    + ad-hoc signed ($(codesign -dv "$APP" 2>&1 | awk -F= '/Signature/ {print $2}'))"
else
  echo "    ! codesign not found — the app will not launch on arm64" >&2
fi

# --- zip + checksum ---------------------------------------------------
( cd dist && rm -f "${NAME}.zip" "${NAME}.zip.sha256" \
          && zip -qry "${NAME}.zip" "GoldenEye.app" \
          && shasum -a 256 "${NAME}.zip" > "${NAME}.zip.sha256" )

echo "==> dist/${NAME}.zip  ($(du -h "dist/${NAME}.zip" | cut -f1))"
cat "dist/${NAME}.zip.sha256"
echo "==> dist/GoldenEye.app  — drop your ROM at:"
echo "    GoldenEye.app/Contents/MacOS/data/ge007.ntsc-final.z64"
find "$APP" -type f | sed "s#^dist/##" | sort
