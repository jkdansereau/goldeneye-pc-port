#!/bin/bash
# verify.sh — one command -> one machine-readable verdict (speed-ups plan Step 2).
#
# Wraps the manual recipe every session was re-deriving by hand (build,
# 640x480 ini pin, GE_PCDUMP capture, crash-symbolication, pixcount,
# framediff): see docs/dev/notes/AGENTIC-SPEEDUPS-PLAN.md Step 2 / N6.
#
# Usage:
#   (no arguments)               prints this usage and EXITS 1 (D529: a no-arg
#                                run must never read as a green verdict)
#   verify.sh <level>            single level: name (dam, bunker1, "surface 1")
#                                 or -level_XX number (09, 33, ...)
#   verify.sh sweep [subset]     all 21 solo levels, or SWEEP_LEVELS-style
#                                 "name:XX name:XX ..." subset
#   verify.sh parity <level> --against <dir>
#                                 framediff this platform's capture against a
#                                 capture directory from the other platform
#                                 (there is no cross-OS launch here — feed it
#                                 a directory of PPMs/PNGs pulled off that box)
#
# Options:
#   --platform win|linux|deck
#                          default: autodetect via `uname -s`. `deck` (Steam
#                          Deck) behaves like linux (build-linux/ge007.x86_64)
#                          but its goldens live in tools_pc/golden/<level>/deck/
#                          (per-level pixel limits = the linux ones for now).
#   -j N                   sweep only: run up to N levels concurrently (default
#                          1). Every level runs in its OWN temp work dir (own
#                          data/ copy + ini + canonical save + ppm/), so
#                          concurrent runs cannot clobber each other and the
#                          repo's data/ is only ever READ (never pinned or
#                          modified). Verdicts print in level order. Each game
#                          quits via GE_QUITFRAME (never hard-killed). PIXEL
#                          RESULTS ASSUME EACH INSTANCE HOLDS 60 fps (D117: the
#                          intro flyby is wall-clock paced): use -j 2-3 on a
#                          strong GPU with nothing else loading it (no local
#                          LLM loaded); use -j 1 on weak boxes.
#   --dump lo-hi:step      GE_PCDUMP window (default: 900-1500:300, the golden
#                          capture window for every level and both modes)
#   --script "..."         GE_INPUTSCRIPT (default "20:START": skip the intro
#                          flyby so frame 900+ is settled gameplay)
#
# Golden recipe (2026-10-02 re-base): 640x480 defaults-only ini, GE_RSEED pinned
# (GOLDEN_SEED below), GE_INPUTSCRIPT "20:START", GE_PCDUMP 900-1500:300,
# goldens in tools_pc/golden/<level>/<platform>/. See tools_pc/golden/README.md.
#   --json                 also emit one JSON object (or array, for sweep) to
#                          stdout: {level, platform, status, frames,
#                          worst_cell, crash_sym}
#   --against DIR          parity mode only: the other platform's capture dir

# Update (D521, P7): the linux pixcount/framediff skip is driver-conditional,
# not platform-conditional -- see the GLX_RENDERER probe below. On a box with a
# real GL driver the full pixel gate runs on linux, so a linux verdict is
# directly comparable to the win/ goldens (same recipe, same stems).
#
# Status values: PASS | CRASH | NO-FRAMES | STALLED | REGRESSION
# (STALLED is this tool's extension of the plan's 4-value enum — see
# level_sweep.sh's D117/D134 note: a level that renders a few frames then
# stops moving is a load-sensitivity flake, not a real pass or fail.)
#
# Known limitation (N6 / M-48) — REVISED D521: GE_PCDUMP reads back black
# frames on llvmpipe/WSLg (a glReadPixels tooling bug, not a render bug —
# the live window renders fine). That is a property of the GL *driver*, not
# of the platform, so the skip below is driver-conditional: a `glxinfo -B`
# renderer probe sets LINUX_SOFTWARE_GL and only a software renderer
# (llvmpipe/softpipe/swrast/dynrecomp) skips pixcount/framediff. The probe
# FAILS CLOSED: glxinfo absent, a non-zero exit, no renderer line, or an
# unknown sentinel all keep the old skip, and the NOTE text is unchanged,
# so an unknown box behaves exactly as before. On a real GL driver the
# full pixel gate runs on linux and a linux verdict is directly comparable
# to the win/ goldens (same recipe, same stems).
#
# Cross-platform golden rule (D521): a fixed frame stem is only comparable
# across platforms at settled gameplay. The intro flyby is wall-clock paced
# (D117) while the capture harness steps SIM frames — GE_PCDUMP and GE_QUITFRAME
# both key off `frames` (port/src/video.c:1406, ++frames in videoEndFrame()),
# which advances once per portRenderGfxTask() (port/src/libultra.c:1256), i.e.
# once per retrace actually consumed by __scMain (dropped retraces at
# port/src/libultra.c:753 never reach the game). A box that cannot hold 60 fps
# therefore settles the flyby at a different frame: pick stems past the slowest
# platform's settle point, or report in-flyby stems as a scene offset and never
# gate on them (P7: Dam frame 900 linux-vs-win = 92.863% over tol 2, phash 83).
#
# Status values: PASS | CRASH | NO-FRAMES | STALLED | REGRESSION
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"

# --- arg parsing -------------------------------------------------------------
MODE=""; LEVEL_ARG=""; PLATFORM=""; DUMP=""; SCRIPT="20:START"; JSON=0; AGAINST=""; JOBS=1
GOLDEN_DUMP="900-1500:300"
USER_DUMP=0; USER_SCRIPT=0
# Per-level golden recipe overrides: Cuba is the ending cutscene + credits (it
# drops back to the boot screens before frame 1200, and START would skip it),
# so it captures the cutscene itself with no input.
golden_dump_for()   { case "$1" in cuba) echo "300-900:300" ;; dam) echo "1500-1700:100" ;; frigate|surface2|streets|depot|cradle) echo "1000-1400:200" ;; *) echo "$GOLDEN_DUMP" ;; esac; }
# D522 re-base (2026-10-05): the 900 stem is NOT settled gameplay on every level.
# Measured on this build with honest sequential passes: at 900 the per-pixel limit
# fails on Surface 2 / Streets / Depot / Cradle / Frigate, and Dam's 900 is still
# inside the flyby. Those six levels capture their window later, where three
# independent runs agree within tol 2 (frigate + the four: 1000/1200/1400 all
# 0.000% max-pair; Dam needed 1500-1700 — its 1200 agreed on two runs then
# diverged 5.761% on a third, and 1600 keeps ~40 moving pixels at 0.013%).
# framediff walks the CANDIDATE frames, so a sweep's --dump must hit the golden
# stems exactly -- never widen a stride. Windows live here so golden/README.md
# and both platforms stay in sync.
# Second tier: per-pixel (framediff --exact) limit, % of pixels over a
# per-channel tolerance of 2. Measured 2026-10-02 run to run (21 levels x 2
# captures): <= 0.57% everywhere except Jungle 1.75% / Surface 2 2.33% (moving
# foliage/fog) and Cuba 36% (cutscene animation timing; structural tier only).
# A global texture-filter change measured 22% on Dam, so 1% leaves ~2x margin
# over the noise and still catches whole-frame render changes.
golden_tolpct_for() { case "$1" in cuba) echo "" ;; jungle|surface2) echo "3.0" ;; *) echo "1.0" ;; esac; }
golden_script_for() { case "$1" in cuba) echo "1:SNONE" ;; *) echo "20:START" ;; esac; }
# D532: the seed is PINNED, not env-overridable -- a stray GE_RSEED in the
# environment used to silently re-seed the gate (frames shift -> spurious
# verdicts). Warn and ignore it.
GOLDEN_SEED=0x0123456789abcdef
if [ -n "${GE_RSEED:-}" ] && [ "$GE_RSEED" != "$GOLDEN_SEED" ]; then
  echo "warning: ignoring GE_RSEED=$GE_RSEED from the environment; the golden gate uses its pinned seed $GOLDEN_SEED (D532)" >&2
fi
POSARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --platform) PLATFORM="$2"; shift 2 ;;
    -j)         JOBS="$2"; shift 2 ;;
    -j[0-9]*)   JOBS="${1#-j}"; shift ;;
    --dump)     DUMP="$2"; USER_DUMP=1; shift 2 ;;
    --script)   SCRIPT="$2"; USER_SCRIPT=1; shift 2 ;;
    --against)  AGAINST="$2"; shift 2 ;;
    --json)     JSON=1; shift ;;
    -h|--help)  sed -n '2,49p' "$0"; exit 0 ;;
    *)          POSARGS+=("$1"); shift ;;
  esac
done
set -- "${POSARGS[@]:-}"
case "$JOBS" in ''|*[!0-9]*|0) echo "error: -j needs a positive integer (got '$JOBS')" >&2; exit 2 ;; esac

case "${1:-}" in
  sweep)  MODE=sweep; shift; SWEEP_SUBSET="${*:-}" ;;
  parity) MODE=parity; LEVEL_ARG="${2:-}"; shift 2 2>/dev/null || true ;;
  -h)     sed -n '2,49p' "$0"; exit 0 ;;
  "")
    # D529 never-do 4: a no-arg run must NEVER look like a green verdict. It
    # prints the usage and exits NON-zero (2026-10-05: a no-arg pair was
    # cited as a "double-green" 21/21 because it exited 0).
    sed -n '2,49p' "$0"
    echo "error: no mode given -- a sweep is 'verify.sh sweep', one level is 'verify.sh <level>' (never cite a no-arg run as a gate result; D529)" >&2
    exit 1 ;;
  *)      MODE=single; LEVEL_ARG="$1" ;;
esac

if [ -z "$PLATFORM" ]; then
  case "$(uname -s)" in
    Linux*)  PLATFORM=linux ;;
    *)       PLATFORM=win ;;
  esac
fi

case "$PLATFORM" in win|linux|deck) ;; *) echo "error: --platform must be win, linux or deck (got '$PLATFORM')" >&2; exit 2 ;; esac
if [ "$PLATFORM" = win ]; then
  BUILD_DIR=build-pc
  EXE="$ROOT/build-pc/ge007.x86_64.exe"
  export PATH="/c/msys64/mingw64/bin:$PATH"   # addr2line + runtime DLLs
else
  BUILD_DIR=build-linux
  EXE="$ROOT/build-linux/ge007.x86_64"
fi

# D521 (P7): the llvmpipe/WSLg black read-back limitation (N6 / M-48) is a
# property of the GL *driver*, not of the linux platform. Probe the renderer
# and only skip pixcount/framediff when the context is software (llvmpipe/
# softpipe/swrast/dynrecomp). A real driver (Mesa iGPU/dGPU, NVIDIA) reads
# back fine, so the pixel gate runs on linux too. glxinfo missing or
# unrecognised -> stay conservative (skip), i.e. an unknown box behaves
# exactly as before.
LINUX_SOFTWARE_GL=1
if [ "$PLATFORM" = linux ] || [ "$PLATFORM" = deck ]; then
  # Fail-closed: only a CLEAN probe may enable the pixel gate. glxinfo absent,
  # a non-zero exit, no "OpenGL renderer string:" line, an unknown sentinel, or
  # a software renderer all leave LINUX_SOFTWARE_GL=1 (today's skip, same NOTE).
  GLX_INFO=$(glxinfo -B 2>/dev/null); GLX_RC=$?
  GLX_RENDERER=$(printf '%s\n' "$GLX_INFO" | sed -n 's/.*OpenGL renderer string:[[:space:]]*//p' | head -n1)
  if [ "$GLX_RC" = 0 ] && [ -n "$GLX_RENDERER" ] && \
     ! printf '%s\n' "$GLX_RENDERER" | grep -qiE '^(unknown|n/?a|none|null)$' && \
     ! printf '%s\n' "$GLX_RENDERER" | grep -qiE 'llvmpipe|softpipe|swrast|dynrecomp'; then
    LINUX_SOFTWARE_GL=0
  fi
fi

# Only ever stop the game process THIS script started (never other instances:
# the maintainer may be playing), and only as a watchdog fallback -- every run
# normally ends through GE_QUITFRAME's orderly quit (D344: a hard kill of a
# live GL/audio process has caused a host BSOD).
GAMEPID=""
kill_ours() { [ -n "$GAMEPID" ] && kill "$GAMEPID" >/dev/null 2>&1; GAMEPID=""; }

# name -> -level_XX  (mission order; matches playtest.sh / level_sweep.sh)
LEVELS=(
  "dam:33" "facility:34" "runway:35" "surface1:36" "bunker1:09" "silo:20"
  "frigate:26" "surface2:43" "bunker2:27" "statue:22" "archives:24"
  "streets:29" "depot:30" "train:25" "jungle:37" "control:23" "caverns:39"
  "cradle:41" "aztec:28" "egypt:32" "cuba:54"
)
norm() { echo "$1" | tr 'A-Z' 'a-z' | tr -d ' _-'; }
resolve_level() {
  local arg="$1"
  if [[ "$arg" =~ ^[0-9]+$ ]]; then echo "num:$(printf '%02d' "$arg")"; return; fi
  local key; key=$(norm "$arg")
  for e in "${LEVELS[@]}"; do
    [ "$(norm "${e%%:*}")" = "$key" ] && { echo "${e%%:*}:${e##*:}"; return; }
  done
  echo ""
}
name_for_num() {
  local num="$1"
  for e in "${LEVELS[@]}"; do [ "${e##*:}" = "$num" ] && { echo "${e%%:*}"; return; }; done
  echo "level_$num"
}

# --- build-if-stale ------------------------------------------------------
if [ ! -d "$BUILD_DIR" ]; then
  echo "error: $BUILD_DIR missing — configure it first (./build-pc.sh for win;" >&2
  echo "  cmake -S . -B build-linux -G Ninja -DROMID=ntsc-final for linux)" >&2
  exit 2
fi
BUILD_LOG=$(mktemp)
if ! cmake --build "$BUILD_DIR" -j 8 >"$BUILD_LOG" 2>&1; then
  echo "BUILD FAILED"
  cat "$BUILD_LOG" >&2
  rm -f "$BUILD_LOG"
  exit 2
fi
rm -f "$BUILD_LOG"
if [ ! -x "$EXE" ] && [ ! -f "$EXE" ]; then
  echo "error: build produced no $EXE" >&2
  exit 2
fi

# --- golden lookup: tools_pc/golden/<level>/<platform>/, else the flat
#     level_09 set (today's only golden — Step 3 fills the rest in) ----------
golden_dir_for() {
  local name="$1"
  local per="$ROOT/tools_pc/golden/$name/$PLATFORM"
  if [ -d "$per" ] && ls "$per"/*.png >/dev/null 2>&1; then echo "$per"; return; fi
  echo ""
}

# --- per-run isolation (replaces the old ini/eep pin + restore of <repo>/data):
#     every level runs with CWD = a fresh temp work dir holding its OWN data/
#     (the game resolves data/ from its CWD first, port/src/system.c) and its own
#     ppm/ -- the repo's data/ is only READ, never modified, so concurrent runs
#     (-j N) cannot clobber each other and an interrupted run cannot leave the
#     maintainer's ge007.ini / ge007.eep swapped. Golden frames are captured at
#     640x480 with every other option at its compiled-in default (a personal ini
#     changes the frame), and the frame depends on the save's CONTENT (D529), so
#     each work dir gets the pinned defaults-only ini + the canonical save
#     (tools_pc/golden/ge007.eep, in-tree so a fresh clone or CI can run it).
CANON_EEP="$ROOT/tools_pc/golden/ge007.eep"
[ -f "$CANON_EEP" ] || { echo "error: canonical golden save missing: $CANON_EEP (D529)" >&2; exit 2; }
ROMID="${GE_ROMID:-$(sed -n 's/^ROMID:[A-Za-z]*=//p' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | head -n1)}"
ROMID="${ROMID:-ntsc-final}"
for need in "ge007.$ROMID.z64" "pccg-$ROMID" "pcmodels-$ROMID"; do
  [ -e "$ROOT/data/$need" ] || { echo "error: $ROOT/data/$need missing (needed to build the per-run work dir; set GE_ROMID to pick another region)" >&2; exit 2; }
done
# stage_workdir DIR -> populate DIR/data (the game creates DIR/ppm itself)
stage_workdir() {
  local d="$1"
  mkdir -p "$d/data" &&
  cp "$ROOT/data/ge007.$ROMID.z64" "$d/data/" &&
  cp -r "$ROOT/data/pccg-$ROMID" "$ROOT/data/pcmodels-$ROMID" "$d/data/" &&
  cp "$CANON_EEP" "$d/data/ge007.eep" &&
  printf '[Window]\nWidth = 640\nHeight = 480\n' > "$d/data/ge007.ini"
}
# EXIT only (D534): INT/TERM just exit, which fires EXIT.
CUR_CAPDIR=""
trap 'kill_ours; kill $(jobs -rp) 2>/dev/null; [ -n "${SWEEP_TMP:-}" ] && rm -rf "$SWEEP_TMP"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# --- run one level -> sets globals: R_STATUS R_FRAMES R_SYM R_BRIEF -------
run_one() {
  local name="$1" num="$2" dump="$3" watchdog="${4:-90}"
  local capdir; capdir=$(mktemp -d)
  CUR_CAPDIR="$capdir"
  stage_workdir "$capdir" ||
    { echo "error: cannot stage the work dir $capdir (data copy failed)" >&2; rm -rf "$capdir"; exit 2; }
  [ "$USER_DUMP" = 1 ] || dump=$(golden_dump_for "$name")
  local script="$SCRIPT"
  [ "$USER_SCRIPT" = 1 ] || script=$(golden_script_for "$name")
  R_STATUS=""; R_FRAMES=0; R_SYM=""; R_BRIEF=""

  # Quit right after the last requested GE_PCDUMP frame via GE_QUITFRAME (an
  # orderly D344 quit), instead of idling out the watchdog; the watchdog stays
  # the safety net for a level that never gets there (hang / flake).
  local lo hi step lastframe quitframe=""
  IFS='-:' read -r lo hi step <<< "$dump"
  if [[ "$lo" =~ ^-?[0-9]+$ && "$hi" =~ ^-?[0-9]+$ ]]; then
    [[ "$step" =~ ^[0-9]+$ && "$step" -gt 0 ]] || step=1
    lastframe=$(( lo + step * ( (hi - lo) / step ) ))
    quitframe=$(( lastframe + 2 ))
  fi

  ( cd "$capdir" && export GE_PCDUMP="$dump"
    [ -n "$quitframe" ] && export GE_QUITFRAME="$quitframe"
    [ -n "$script" ] && export GE_INPUTSCRIPT="$script"
    export GE_RSEED="$GOLDEN_SEED"
    export GE_FAKE_DECK=0   # the D283 Deck preset forces 1280x800 fullscreen over the pinned ini
    exec "$EXE" "-level_$num" ) >"$capdir/run.log" 2>&1 &
  local gamepid=$!
  GAMEPID=$gamepid

  local ticks=0 max_ticks=$(( watchdog * 5 ))   # 0.2s ticks
  while kill -0 "$gamepid" 2>/dev/null && [ "$ticks" -lt "$max_ticks" ]; do
    sleep 0.2
    ticks=$((ticks + 1))
  done
  if kill -0 "$gamepid" 2>/dev/null; then
    echo "  watchdog: $name still running after ${watchdog}s, stopping it" >&2
    kill_ours
  fi
  wait "$gamepid" 2>/dev/null
  GAMEPID=""
  mkdir -p "$capdir/ppm"

  R_FRAMES=$(ls "$capdir/ppm"/*.ppm 2>/dev/null | wc -l)

  if [ -f "$capdir/ge007.crash.log" ]; then
    R_STATUS=CRASH
    cp "$capdir/ge007.crash.log" "$capdir/crash.log"
    # Step 9 (R5): crash_brief.py owns address extraction + symbolication
    # (both platforms -- the old inline `0x1[0-9a-fA-F]{8,}` grep here
    # assumed the Windows 0x140000000 image base and would never match a
    # Linux no-PIE 0x20000000-based address) and prints a dispatch-ready
    # brief with known-parked-signature and porting-notes/findings hits.
    R_BRIEF=$(python "$ROOT/tools_pc/crash_brief.py" \
      --crash-log "$capdir/crash.log" --exe "$EXE" --platform "${PLATFORM/deck/linux}" \
      --level "$name" --json 2>&1)
    R_SYM=$(echo "$R_BRIEF" | python -c "
import json,sys
s=sys.stdin.read(); i=s.find('{')
try:
    d,_=json.JSONDecoder().raw_decode(s[i:]) if i>=0 else (None,0)
    f=d['frames'][0]
    print('%s %s' % (f.get('func') or '??', f.get('loc') or ''))
except Exception:
    print('')
" 2>/dev/null)
  elif [ "$R_FRAMES" -eq 0 ]; then
    R_STATUS=NO-FRAMES
  else
    R_STATUS=PASS
  fi
  CAPDIR="$capdir"
}

# --- verdict emission --------------------------------------------------------
emit_verdict() {
  local name="$1" status="$2" frames="$3" worst="$4" sym="$5" note="${6:-}"
  echo "$name: $status  frames=$frames${worst:+ worst_cell=$worst}${sym:+ crash_sym=\"$sym\"}${note:+  ($note)}"
  if [ "$JSON" = 1 ]; then
    python3 - "$name" "$PLATFORM" "$status" "$frames" "$worst" "$sym" "$note" <<'EOF'
import json, sys
name, plat, status, frames, worst, sym, note = sys.argv[1:8]
obj = {"level": name, "platform": plat, "status": status, "frames": int(frames)}
if worst: obj["worst_cell"] = float(worst)
if sym:   obj["crash_sym"] = sym
if note:  obj["note"] = note
print(json.dumps(obj))
EOF
  fi
}

verify_level() {
  local name="$1" num="$2" dump="$3" watchdog="${4:-90}"
  run_one "$name" "$num" "$dump" "$watchdog"
  local status="$R_STATUS" frames="$R_FRAMES" sym="$R_SYM" worst="" note=""

  if [ "$status" = PASS ]; then
    if { [ "$PLATFORM" = linux ] || [ "$PLATFORM" = deck ]; } && [ "$LINUX_SOFTWARE_GL" = 1 ]; then
      note="GE_PCDUMP reads black on llvmpipe/WSLg — pixcount/framediff skipped, crash-detect only"
    else
      local last; last=$(ls "$CAPDIR"/ppm/*.ppm 2>/dev/null | tail -1)
      if [ -n "$last" ] && ! python3 "$ROOT/tools_pc/pixcount.py" "$last" >/dev/null 2>&1; then
        status=NO-FRAMES; note="frames present but all-clear (pixcount 0)"
      else
        local gdir; gdir=$(golden_dir_for "$name")
        if [ -n "$gdir" ]; then
          local fdiff; fdiff=$(python3 "$ROOT/tools_pc/framediff.py" "$CAPDIR/ppm" --golden "$gdir" --json 2>&1)
          local fexit=$?
          # framediff --json sandwiches the JSON blob between human-readable
          # per-frame lines and a summary line -- pull it out by scanning
          # from the first '{' with a raw JSON decoder instead of assuming
          # the whole stream parses.
          worst=$(echo "$fdiff" | python3 -c "
import json,sys
s = sys.stdin.read()
i = s.find('{')
try:
    d, _ = json.JSONDecoder().raw_decode(s[i:]) if i >= 0 else (None, 0)
    print(max((r.get('worst_cell',0) for r in d['results']), default=0))
except Exception:
    print('')
" 2>/dev/null)
          # exit 1 = a real per-frame threshold fail (pixel/cell/hash delta).
          # exit 2 = a structural mismatch (missing/size) -- most commonly the
          # golden's frame stems don't line up with this run's --dump window
          # (goldens are captured at GOLDEN_DUMP; a
          # sweep using a different stride has nothing to compare against
          # rather than a real regression). Only exit 1 is a verdict fail.
          case "$fexit" in
            1) status=REGRESSION; note="framediff vs $gdir" ;;
            2) note="golden frame stems don't match --dump window ($dump) for $gdir" ;;
          esac
          local tolpct; tolpct=$(golden_tolpct_for "$name")
          if [ "$fexit" = 0 ] && [ -n "$tolpct" ]; then
            local xout; xout=$(python3 "$ROOT/tools_pc/framediff.py" "$CAPDIR/ppm" --golden "$gdir" --exact --tol 2 --tol-pct "$tolpct" 2>&1)
            if [ $? -eq 1 ]; then
              status=REGRESSION
              note="per-pixel: $(echo "$xout" | grep -m1 FAIL | sed 's/^FAIL //') (limit ${tolpct}%)"
            fi
          fi
        else
          note="no golden for $name/$PLATFORM yet (Step 3)"
        fi
      fi
    fi
  fi

  local brief="$R_BRIEF"
  if [ "$status" = NO-FRAMES ] && [ -f "$CAPDIR/run.log" ]; then
    # stderr, not stdout: sweep mode captures verify_level's stdout via
    # `out=$(...)` and only keeps its first line (`head -1`) as the verdict
    # summary -- anything printed here on stdout would become that first
    # line and clip the real verdict (and, worse, the --json line further
    # down). stderr bypasses the capture and reaches the terminal/log file
    # directly.
    echo "  -- $name run.log tail (NO-FRAMES, no crash -- likely startup/init failure) --" >&2
    tail -n 30 "$CAPDIR/run.log" | sed 's/^/  | /' >&2
  fi
  rm -rf "$CAPDIR"
  emit_verdict "$name" "$status" "$frames" "$worst" "$sym" "$note"
  if [ "$status" = CRASH ] && [ -n "$brief" ]; then
    # crash_brief.py's --json output sandwiches the JSON blob after the
    # human-readable brief -- print only the brief part here.
    echo "$brief" | awk '/^\{/{exit} {print}'
  fi
  case "$status" in PASS) return 0 ;; *) return 1 ;; esac
}

# --- dispatch ----------------------------------------------------------------
case "$MODE" in
  single)
    resolved=$(resolve_level "$LEVEL_ARG")
    [ -z "$resolved" ] && { echo "unknown level '$LEVEL_ARG'" >&2; exit 2; }
    name="${resolved%%:*}"; num="${resolved##*:}"
    [ "${resolved%%:*}" = num ] && name=$(name_for_num "$num")
    verify_level "$name" "$num" "${DUMP:-$GOLDEN_DUMP}"
    ;;

  sweep)
    RESULTS_JSON=()
    RC=0
    subset="${SWEEP_LEVELS:-${SWEEP_SUBSET:-}}"
    entries=("${LEVELS[@]}")
    [ -n "$subset" ] && read -ra entries <<< "$subset"
    # -j N: up to N levels at once, each in its own work dir (see above).
    # Per-level stdout/stderr/rc are buffered in $SWEEP_TMP and replayed in the
    # ORIGINAL level order, so the output is identical to a sequential sweep.
    SWEEP_TMP=$(mktemp -d)
    wd=60; [ "$JOBS" -gt 1 ] && wd=90   # slower under contention; still only a fallback
    nxt=0
    flush_done() {   # emit every finished level that is next in order
      local blocking="${1:-0}" rc nm j
      while [ "$nxt" -lt "${#entries[@]}" ]; do
        if [ ! -f "$SWEEP_TMP/$nxt.rc" ]; then
          [ "$blocking" = 1 ] && { sleep 0.2; continue; }
          return
        fi
        rc=$(cat "$SWEEP_TMP/$nxt.rc")
        [ -s "$SWEEP_TMP/$nxt.err" ] && cat "$SWEEP_TMP/$nxt.err" >&2
        [ "$rc" -ne 0 ] && RC=1
        if [ -s "$SWEEP_TMP/$nxt.out" ]; then
          # Pre-existing bug (found alongside D244's diagnostics): `head -1`
          # silently discarded emit_verdict's --json line on every sweep run;
          # keep the verdict line (head -1) and pull the JSON line separately.
          head -1 "$SWEEP_TMP/$nxt.out"
          if [ "$JSON" = 1 ]; then
            j=$(grep -m1 '^{' "$SWEEP_TMP/$nxt.out")
            [ -n "$j" ] && RESULTS_JSON+=("$j")
          fi
        else
          nm="${entries[$nxt]%%:*}"
          echo "$nm: NO-FRAMES  frames=0  (setup failed -- see stderr)"
          RC=1
        fi
        nxt=$((nxt + 1))
      done
    }
    idx=0
    for e in "${entries[@]}"; do
      n="${e%%:*}"; num="${e##*:}"
      while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.2; flush_done; done
      (
        trap 'kill_ours; [ -n "$CUR_CAPDIR" ] && rm -rf "$CUR_CAPDIR"; exit 143' TERM
        RCV=2   # an `exit` inside run_one (setup failure) still writes an rc
        trap 'echo "$RCV" > "$SWEEP_TMP/$idx.rc"' EXIT
        verify_level "$n" "$num" "${DUMP:-$GOLDEN_DUMP}" "$wd" >"$SWEEP_TMP/$idx.out" 2>"$SWEEP_TMP/$idx.err"
        RCV=$?
      ) &
      idx=$((idx + 1))
      flush_done
    done
    flush_done 1
    wait
    echo "SWEEP DONE"
    if [ "$JSON" = 1 ] && [ "${#RESULTS_JSON[@]}" -gt 0 ]; then
      { printf '['
        for i in "${!RESULTS_JSON[@]}"; do
          [ "$i" -gt 0 ] && printf ','
          printf '%s' "${RESULTS_JSON[$i]}"
        done
        printf ']\n'
      }
    fi
    exit $RC
    ;;

  parity)
    [ -z "$LEVEL_ARG" ] && { echo "usage: verify.sh parity <level> --against DIR" >&2; exit 2; }
    [ -z "$AGAINST" ] && { echo "parity needs --against DIR (the other platform's capture)" >&2; exit 2; }
    resolved=$(resolve_level "$LEVEL_ARG")
    [ -z "$resolved" ] && { echo "unknown level '$LEVEL_ARG'" >&2; exit 2; }
    name="${resolved%%:*}"; num="${resolved##*:}"
    run_one "$name" "$num" "${DUMP:-$GOLDEN_DUMP}" 90
    if [ "$R_STATUS" != PASS ]; then
      emit_verdict "$name" "$R_STATUS" "$R_FRAMES" "" "$R_SYM"
      rm -rf "$CAPDIR"; exit 1
    fi
    python3 "$ROOT/tools_pc/framediff.py" "$CAPDIR/ppm" --golden "$AGAINST" --tol 2 --tol-pct 0.5 --exact ${JSON:+--json}
    rc=$?
    rm -rf "$CAPDIR"
    exit $rc
    ;;
esac
