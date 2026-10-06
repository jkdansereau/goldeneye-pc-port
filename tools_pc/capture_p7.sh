#!/bin/bash
# P7 golden capture on Linux (mirrors tools_pc/verify.sh's golden recipe).
#
# Usage: capture_p7.sh [-b BUILD_DIR] <level_name>:<num> [<level_name>:<num> ...]
#   BUILD_DIR  build dir holding ge007.x86_64, relative to the repo root
#              (default: build-linux). The repo root is the parent of the
#              directory this script lives in, so the script can be invoked
#              from anywhere; no machine-specific path is baked in.
#   Writes only inside the repo: <repo>/data/ge007.ini + ge007.eep (the
#   pinned 640x480 ini and the canonical save, re-pinned before every level;
#   the local save is only snapshotted for the exit restore),
#   <repo>/ppm (the game's dump dir, removed after each level), and
#   <repo>/tools_pc/golden/<level>/linux/.
#   Per-level run logs go to <repo>/cap_<level>.log: scratch, never tracked.
#   DISPLAY must be inherited from the caller -- the capture needs a real GL
#   context, and there is deliberately no default display/XAUTHORITY here.
#
# Recipe: the per-level windows/scripts/seed are READ OUT OF tools_pc/verify.sh
# (golden_dump_for / golden_script_for / GOLDEN_SEED) so this tool and the gate
# cannot drift apart -- see tools_pc/golden/README.md for what the windows are
# and why (D522). GE_QUITFRAME = last requested frame + 2 (orderly quit, D344).
#
# D523 (SUPERSEDED by D529): the win/ set was captured with a save file, and
# the frame depends on the save's CONTENT (D529's A/B: facility 177.47 on the
# 2026-10-05 playtest save, 0.0-0.25 on the four other candidates). This tool
# installs the CANONICAL save (tools_pc/golden/ge007.eep, in-tree; verify.sh
# does the same for the gate) rather than trusting a local eep -- a local
# playtest save is exactly this class of drift. (History: the P7 round of
# 2026-10-04 ran with a cleared eep; that set was re-captured 2026-10-05, D525.)
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR=build-linux
if [ "${1:-}" = -b ]; then
  [ -n "${2:-}" ] || { echo "error: -b needs a BUILD_DIR argument"; exit 2; }
  BUILD_DIR="$2"; shift 2
fi
[ "$#" -gt 0 ] || { echo "error: no <level_name>:<num> specs given"; exit 2; }
EXE="$ROOT/$BUILD_DIR/ge007.x86_64"
[ -x "$EXE" ] || { echo "error: no executable at $EXE (pass -b BUILD_DIR)"; exit 2; }
[ -n "${DISPLAY:-}" ] || { echo "error: DISPLAY is unset; the capture needs a real GL context (no default provided)"; exit 2; }
VSH="$ROOT/tools_pc/verify.sh"
[ -f "$VSH" ] || { echo "error: no tools_pc/verify.sh under $ROOT (the recipe lives there)"; exit 2; }
CANON_EEP="$ROOT/tools_pc/golden/ge007.eep"
[ -f "$CANON_EEP" ] || { echo "error: canonical golden save missing: $CANON_EEP (D529)"; exit 2; }
cd "$ROOT"   # the loop's dump/golden paths are root-relative (ppm/, tools_pc/golden/)
EEP="$ROOT/data/ge007.eep"
EEP_ORIG=$(mktemp); EEP_HAD=0
[ -f "$EEP" ] && { EEP_HAD=1; cp "$EEP" "$EEP_ORIG"; }
cp "$CANON_EEP" "$EEP"
INI="$ROOT/data/ge007.ini"
INI_ORIG=""
[ -f "$INI" ] && { INI_ORIG=$(mktemp); cp "$INI" "$INI_ORIG"; }
cleanup() {
  [ -n "$INI_ORIG" ] && cp "$INI_ORIG" "$INI" 2>/dev/null
  if [ "$EEP_HAD" = 1 ]; then cp "$EEP_ORIG" "$EEP" 2>/dev/null; else rm -f "$EEP" 2>/dev/null; fi
  rm -f "$INI_ORIG" "$EEP_ORIG" 2>/dev/null
}
# D534: restore on EXIT only; INT/TERM just exit (which fires EXIT), so an
# interrupted capture cannot continue past the restore with the snapshot gone.
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# recipe comes from the tracked gate tool, not from this file
eval "$(grep -m1 '^GOLDEN_DUMP=' "$VSH")"
eval "$(grep -m1 '^golden_dump_for()'  "$VSH")"
eval "$(grep -m1 '^golden_script_for()' "$VSH")"
eval "$(grep -m1 '^GOLDEN_SEED=' "$VSH")"

for spec in "$@"; do
  name="${spec%%:*}"; num="${spec##*:}"
  [ "$name" != "$num" ] || { echo "error: bad spec $spec"; exit 2; }
  dump=$(golden_dump_for "$name")
  script=$(golden_script_for "$name")
  IFS='-:' read -r lo hi step <<< "$dump"
  last=$(( lo + step * ((hi - lo) / step) ))
  qf=$(( last + 2 ))
  printf '[Window]\nWidth = 640\nHeight = 480\n' > "$INI"
  # D531: re-pin the CANONICAL save before every level (the frame depends on
  # the save's CONTENT, D529). The D524-era re-pin restored $EEP_ORIG (the
  # local pre-capture save, or an empty file when none existed), so the
  # pre-loop canonical install below was silently overwritten from level 1:
  # the 2026-10-05 box re-round ran under the box's local save, not the
  # canonical one. EEP_ORIG is kept only for the exit restore.
  cp "$CANON_EEP" "$EEP"
  rm -f ge007.crash.log
  rm -rf ppm
  t0=$(date +%s)
  ( export GE_PCDUMP="$dump" GE_QUITFRAME="$qf" GE_INPUTSCRIPT="$script" \
      GE_RSEED="$GOLDEN_SEED"; timeout 300 "$EXE" -level_"$num" ) > "cap_$name.log" 2>&1
  rc=$?
  n=0
  if [ -d ppm ]; then
    mkdir -p "tools_pc/golden/$name/linux"
    for f in ppm/*.ppm; do
      stem=$(basename "$f" .ppm)
      python3 -c "from PIL import Image; Image.open('$f').save('tools_pc/golden/$name/linux/$stem.png')"
      n=$((n+1))
    done
    rm -rf ppm
  fi
  t1=$(date +%s)
  echo "== $name rc=$rc frames=$n secs=$((t1-t0)) window=$dump"
done
