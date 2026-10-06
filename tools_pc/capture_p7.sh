#!/bin/bash
# P7 golden capture on Linux (mirrors tools_pc/verify.sh's golden recipe).
#
# Usage: capture_p7.sh [-b BUILD_DIR] [-p PLATFORM] [-j N] <level_name>:<num> [...]
#   BUILD_DIR  build dir holding ge007.x86_64, relative to the repo root
#              (default: build-linux). The repo root is the parent of the
#              directory this script lives in, so the script can be invoked
#              from anywhere; no machine-specific path is baked in.
#   PLATFORM   golden set to write: tools_pc/golden/<level>/<PLATFORM>/
#              (default: linux; `deck` = Steam Deck set, same exe/recipe).
#   -j N       run up to N levels concurrently (default 1); result lines
#              print in the original level order. Each game quits via
#              GE_QUITFRAME (the only kill is `timeout`'s own-child watchdog).
#              PIXEL RESULTS ASSUME EACH INSTANCE HOLDS 60 fps (D117: the
#              intro flyby is wall-clock paced): use -j 2-3 only on a strong
#              GPU with nothing else loading it (no local LLM loaded); use
#              -j 1 on weak boxes (Steam Deck, Intel HD 3000, ...).
#   Isolation: every level runs with CWD = a fresh temp work dir holding its
#   OWN data/ (copy of ge007.<romid>.z64 + pccg-<romid>/ + pcmodels-<romid>/ +
#   the canonical save tools_pc/golden/ge007.eep + the pinned 640x480 ini) and
#   its own ppm/ (the game resolves data/ from its CWD first). The repo's
#   data/ is only READ, never modified; the work dir is removed after each
#   level. ROMID comes from <BUILD_DIR>/CMakeCache.txt, else the single
#   data/ge007.*.z64 present, else ntsc-final (override: GE_ROMID).
#   Writes only: <repo>/tools_pc/golden/<level>/<PLATFORM>/ and the per-level
#   run logs <repo>/cap_<level>.log (scratch, never tracked).
#   Windows: run from Git Bash/MSYS2 with -b build-pc -p win (no DISPLAY needed;
#   the .exe resolves from <BUILD_DIR>/ge007.x86_64).
#   Runs on a stripped copy of the repo on the device (Steam Deck Desktop
#   Mode: DISPLAY=:0 bash, python3 + PIL, no compiler): put the prebuilt binary
#   at <BUILD_DIR>/ge007.x86_64 and data/ + tools_pc/{verify.sh,golden/ge007.eep}
#   in the copy.
#   DISPLAY must be inherited from the caller -- the capture needs a real GL
#   context, and there is deliberately no default display/XAUTHORITY here.
#
# GE_FAKE_DECK=0 on every run: on a Steam Deck the D283 preset would force
# 1280x800 fullscreen over the pinned 640x480 ini (harmless elsewhere).
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
BUILD_DIR=build-linux; PLATFORM=linux; JOBS=1
while getopts "b:p:j:" opt; do
  case "$opt" in
    b) BUILD_DIR="$OPTARG" ;;
    p) PLATFORM="$OPTARG" ;;
    j) JOBS="$OPTARG" ;;
    *) echo "usage: capture_p7.sh [-b BUILD_DIR] [-p PLATFORM] [-j N] <level_name>:<num> ..."; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
case "$JOBS" in ''|*[!0-9]*|0) echo "error: -j needs a positive integer (got '$JOBS')"; exit 2 ;; esac
case "$PLATFORM" in ''|*[!A-Za-z0-9_-]*) echo "error: bad PLATFORM '$PLATFORM'"; exit 2 ;; esac
[ "$#" -gt 0 ] || { echo "error: no <level_name>:<num> specs given"; exit 2; }
EXE="$ROOT/$BUILD_DIR/ge007.x86_64"
[ -x "$EXE" ] || { echo "error: no executable at $EXE (pass -b BUILD_DIR)"; exit 2; }
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;   # Windows (Git Bash / MSYS2): the desktop is the GL context; no DISPLAY
  *) [ -n "${DISPLAY:-}" ] || { echo "error: DISPLAY is unset; the capture needs a real GL context (no default provided)"; exit 2; } ;;
esac
PY=""; for c in python3 python; do "$c" -c "import PIL" >/dev/null 2>&1 && { PY=$c; break; }; done
[ -n "$PY" ] || { echo "error: no python with Pillow (PIL) on PATH (needed to write the PNGs)"; exit 2; }
VSH="$ROOT/tools_pc/verify.sh"
[ -f "$VSH" ] || { echo "error: no tools_pc/verify.sh under $ROOT (the recipe lives there)"; exit 2; }
CANON_EEP="$ROOT/tools_pc/golden/ge007.eep"
[ -f "$CANON_EEP" ] || { echo "error: canonical golden save missing: $CANON_EEP (D529)"; exit 2; }
cd "$ROOT"   # golden paths are root-relative (tools_pc/golden/)

ROMID="${GE_ROMID:-$(sed -n 's/^ROMID:[A-Za-z]*=//p' "$ROOT/$BUILD_DIR/CMakeCache.txt" 2>/dev/null | head -n1)}"
if [ -z "$ROMID" ]; then
  roms=$(ls "$ROOT"/data/ge007.*.z64 2>/dev/null)
  if [ "$(printf '%s\n' "$roms" | grep -c .)" = 1 ]; then
    ROMID=$(basename "$roms" .z64); ROMID="${ROMID#ge007.}"
  else ROMID=ntsc-final; fi
fi
for need in "ge007.$ROMID.z64" "pccg-$ROMID" "pcmodels-$ROMID"; do
  [ -e "$ROOT/data/$need" ] || { echo "error: $ROOT/data/$need missing (needed for the per-run work dir; set GE_ROMID to pick another region)"; exit 2; }
done

# recipe comes from the tracked gate tool, not from this file
eval "$(grep -m1 '^GOLDEN_DUMP=' "$VSH")"
eval "$(grep -m1 '^golden_dump_for()'  "$VSH")"
eval "$(grep -m1 '^golden_script_for()' "$VSH")"
eval "$(grep -m1 '^GOLDEN_SEED=' "$VSH")"

TMPROOT=$(mktemp -d)
trap 'kill $(jobs -rp) 2>/dev/null; rm -rf "$TMPROOT"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# capture_one NAME NUM -> prints the result line on stdout
capture_one() {
  local name="$1" num="$2" dump script lo hi step last qf w t0 t1 rc n f stem
  dump=$(golden_dump_for "$name")
  script=$(golden_script_for "$name")
  IFS='-:' read -r lo hi step <<< "$dump"
  last=$(( lo + step * ((hi - lo) / step) ))
  qf=$(( last + 2 ))
  w="$TMPROOT/$name"; rm -rf "$w"; mkdir -p "$w/data"
  # D531/D529: every level gets the CANONICAL save (the frame depends on its
  # content) and the pinned defaults-only 640x480 ini -- in its OWN data/.
  cp "$ROOT/data/ge007.$ROMID.z64" "$w/data/" &&
  cp -r "$ROOT/data/pccg-$ROMID" "$ROOT/data/pcmodels-$ROMID" "$w/data/" &&
  cp "$CANON_EEP" "$w/data/ge007.eep" &&
  printf '[Window]\nWidth = 640\nHeight = 480\n' > "$w/data/ge007.ini" ||
    { echo "== $name rc=setup-failed frames=0 secs=0 window=$dump"; return; }
  t0=$(date +%s)
  ( cd "$w" && export GE_PCDUMP="$dump" GE_QUITFRAME="$qf" GE_INPUTSCRIPT="$script" \
      GE_RSEED="$GOLDEN_SEED" GE_FAKE_DECK=0; timeout 300 "$EXE" -level_"$num" ) > "$ROOT/cap_$name.log" 2>&1
  rc=$?
  n=0
  if [ -d "$w/ppm" ]; then
    mkdir -p "$ROOT/tools_pc/golden/$name/$PLATFORM"
    for f in "$w"/ppm/*.ppm; do
      [ -f "$f" ] || continue
      stem=$(basename "$f" .ppm)
      "$PY" -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" \
        "$f" "$ROOT/tools_pc/golden/$name/$PLATFORM/$stem.png"
      n=$((n+1))
    done
  fi
  rm -rf "$w"
  t1=$(date +%s)
  echo "== $name rc=$rc frames=$n secs=$((t1-t0)) window=$dump"
}

# validate every spec up front, then run up to JOBS at once; results are
# replayed in the original order
i=0
for spec in "$@"; do
  name="${spec%%:*}"; num="${spec##*:}"
  [ "$name" != "$num" ] || { echo "error: bad spec $spec"; exit 2; }
done
nxt=0
flush_done() {
  local blocking="${1:-0}"
  while [ "$nxt" -lt "$i" ] || { [ "$blocking" = 1 ] && [ "$nxt" -lt "$total" ]; }; do
    if [ ! -f "$TMPROOT/res.$nxt" ]; then
      [ "$blocking" = 1 ] && { sleep 0.2; continue; }
      return
    fi
    cat "$TMPROOT/res.$nxt"; nxt=$((nxt + 1))
  done
}
total=$#
for spec in "$@"; do
  name="${spec%%:*}"; num="${spec##*:}"
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.2; flush_done; done
  ( capture_one "$name" "$num" > "$TMPROOT/res.$i.tmp"; mv "$TMPROOT/res.$i.tmp" "$TMPROOT/res.$i" ) &
  i=$((i + 1))
  flush_done
done
flush_done 1
wait
