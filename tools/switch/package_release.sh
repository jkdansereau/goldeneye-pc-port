#!/bin/sh
# project-dam: assemble the release archive from a finished Switch build.
#
#   tools/switch/package_release.sh <version> [out_dir]
#
# Run from the repo root after the NRO is built (build-switch/). Produces
# <out_dir>/project-dam-<version>.zip with:
#
#   switch/project-dam/project-dam.nro   copy switch/ to the root of the SD card
#   README.txt, LICENSE, NOTICE
#
# No game content: players supply their own ROM; the game converts it on
# first launch.
set -eu

VERSION="${1:?usage: package_release.sh <version> [out_dir]}"
OUT="${2:-dist}"
NRO=build-switch/ge007.aarch64.nro
[ -f "$NRO" ] || { echo "error: $NRO missing; build the NRO first" >&2; exit 1; }

STAGE="$OUT/project-dam-$VERSION"
rm -rf "$STAGE"
mkdir -p "$STAGE/switch/project-dam/data"

cp "$NRO" "$STAGE/switch/project-dam/project-dam.nro"

cp LICENSE NOTICE "$STAGE/"
sed "s/@VERSION@/$VERSION/g" tools/switch/README-release.txt > "$STAGE/README.txt"

(cd "$OUT" && rm -f "project-dam-$VERSION.zip" && zip -qr "project-dam-$VERSION.zip" "project-dam-$VERSION")
echo "$OUT/project-dam-$VERSION.zip"
