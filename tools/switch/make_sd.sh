#!/bin/sh
# project-dam: assemble the SD-card layout for the Switch build.
#
#   tools/switch/make_sd.sh /path/to/rom.z64 [out_dir]
#
# Runs inside the build container (local/project-dam-build), from the repo
# root, after `docker compose run --rm switch`. Produces:
#
#   <out_dir>/switch/project-dam/project-dam.nro
#   <out_dir>/switch/project-dam/data/ge007.ntsc-final.z64   (copy of your ROM)
#
# No converted sidecars are staged: on first launch the game converts the ROM
# itself (port/convert/, run in-process by port/src/romconvert.c) and writes
# data/pcmodels-<region>/ and data/pccg-<region>/ next to the NRO. Copy
# <out_dir>/switch to the root of the SD card. Nothing here is committed.
set -eu

ROM="${1:?usage: make_sd.sh /path/to/rom.z64 [out_dir]}"
OUT="${2:-build-switch/sd}"
GAME="$OUT/switch/project-dam"

[ -f build-switch/ge007.aarch64.nro ] || { echo "error: build the NRO first (docker compose run --rm switch)" >&2; exit 1; }

# --- SD layout --------------------------------------------------------------
mkdir -p "$GAME/data"
cp build-switch/ge007.aarch64.nro "$GAME/project-dam.nro"
cp "$ROM" "$GAME/data/ge007.ntsc-final.z64"

echo "SD layout ready: $OUT/switch  (copy it to the root of the SD card)"
