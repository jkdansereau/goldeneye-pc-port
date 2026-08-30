#!/bin/bash
# Level sweep: bare -level_XX boot for all 21 solo levels; PASS/NO-FRAMES/CRASH.
#
# Flakiness notes (M-13..M-21): the port is not frame-deterministic (D117) and
# a loaded machine (a concurrent ninja build, another sweep, a stray ge007)
# routinely turns a known-good level into a spurious NO-FRAMES. This runner is
# therefore SERIALIZED (a lockdir + a stray-process kill before each level) and
# uses a 45 s watchdog, and it RETRIES a NO-FRAMES row once before reporting it.
# Do not run it next to a build.
#
# Speed (M-22): the 45 s watchdog is a CEILING, not a fixed cost. A level that
# has produced its whole GE_PCDUMP frame set is killed immediately (PASS), and a
# level that renders a few frames then stops moving is cut after ~STALL_SECS. So
# a healthy 21-level sweep now finishes in a few minutes instead of ~16.
#
# Env overrides:
#   SWEEP_SECS=45          per-level watchdog seconds
#   SWEEP_DUMP=80-400:40   GE_PCDUMP window
#   SWEEP_LEVELS="Dam:33"  subset to run
cd "$(dirname "$0")/.."

SWEEP_SECS=${SWEEP_SECS:-45}
SWEEP_DUMP=${SWEEP_DUMP:-80-400:40}
STALL_SECS=${STALL_SECS:-18}   # frames stopped growing this long -> stop early

# how many .ppm files a complete GE_PCDUMP window produces (start-end:stride)
_d=${SWEEP_DUMP%%:*}; _stride=${SWEEP_DUMP##*:}
_start=${_d%%-*}; _end=${_d##*-}
if [ "$_start" -ge 0 ] 2>/dev/null && [ "$_stride" -gt 0 ] 2>/dev/null; then
  EXPECT_FRAMES=$(( (_end - _start) / _stride + 1 ))
else
  EXPECT_FRAMES=4
fi
[ "$EXPECT_FRAMES" -lt 1 ] && EXPECT_FRAMES=1
LV=${SWEEP_LEVELS:-"Dam:33 Facility:34 Runway:35 Surface1:36 Bunker1:09 Silo:20 Frigate:26 Surface2:43 Bunker2:27 Statue:22 Archives:24 Streets:29 Depot:30 Train:25 Jungle:37 Control:23 Caverns:39 Cradle:41 Aztec:28 Egypt:32 Cuba:54"}

OUT=/tmp/sweep_results
LOGS=/tmp/sweeplogs
LOCK=/tmp/ge007_sweep.lock

# --- serialize: one sweep at a time -----------------------------------------
if ! mkdir "$LOCK" 2>/dev/null; then
  echo "another sweep holds $LOCK (stale? rmdir it)" >&2
  exit 1
fi
trap 'taskkill //F //IM ge007.x86_64.exe >/dev/null 2>&1; rmdir "$LOCK" 2>/dev/null' EXIT INT TERM

export PATH="/c/msys64/mingw64/bin:$PATH"   # addr2line + runtime DLLs

mkdir -p $LOGS
: > $OUT
rm -f $LOGS/*.crash.log

# one run of one level -> echoes the status string
run_level() {
  local name=$1 num=$2
  rm -f ge007.crash.log; rm -rf ppm
  # kill anything left over from a previous level / another session
  taskkill //F //IM ge007.x86_64.exe > /dev/null 2>&1
  sleep 1

  GE_PCDUMP="$SWEEP_DUMP" ./build-pc/ge007.x86_64.exe -level_$num > $LOGS/$name.log 2>&1 &
  local pid=$!

  # watchdog: poll so we do not pay the full window when the outcome is already
  # known -- exit as soon as the whole dump set is on disk (PASS), or the frame
  # count has been frozen for STALL_SECS (level rendered a bit then wedged), or
  # the process exits on its own (clean exit / crash).
  local waited=0 fcnt=0 prev=0 stall=0 stalled=0
  while [ $waited -lt $SWEEP_SECS ] && kill -0 $pid 2>/dev/null; do
    sleep 1; waited=$((waited + 1))
    fcnt=$(ls ppm/*.ppm 2>/dev/null | wc -l)
    [ "$fcnt" -ge "$EXPECT_FRAMES" ] && break
    if [ "$fcnt" -gt 0 ] && [ "$fcnt" -eq "$prev" ]; then
      stall=$((stall + 1))
      if [ $stall -ge $STALL_SECS ]; then stalled=1; break; fi
    else
      stall=0
    fi
    prev=$fcnt
  done
  # was the process still alive when we gave up (vs. a clean exit / crash)?
  local alive=0
  kill -0 $pid 2>/dev/null && alive=1
  taskkill //F //IM ge007.x86_64.exe > /dev/null 2>&1
  wait $pid 2>/dev/null
  sleep 2

  if [ -f ge007.crash.log ]; then
    local pc sym
    pc=$(grep -oE '0x[0-9a-fA-F]+' ge007.crash.log | head -1)
    sym=$(addr2line -e build-pc/ge007.x86_64.exe -f -C "$pc" 2>/dev/null | head -1)
    cp ge007.crash.log $LOGS/$name.crash.log
    echo "CRASH @ $sym ($pc)"
    return
  fi

  local last pcnt
  last=$(ls ppm/*.ppm 2>/dev/null | tail -1)
  if [ -n "$last" ]; then
    pcnt=$(python tools_pc/pixcount.py "$last" 2>/dev/null | grep -oE 'non-clear [0-9]+ \([0-9.]+%\)')
    # frames rendered then froze while the process stayed alive -> STALLED,
    # not a real PASS (the Silo -level_20 fly-down case, D117/D134 load
    # sensitivity). A complete dump set is still a genuine PASS.
    if [ "$stalled" = 1 ] && [ "$alive" = 1 ] && [ "$fcnt" -lt "$EXPECT_FRAMES" ]; then
      echo "STALLED (froze at $fcnt/$EXPECT_FRAMES frames)  $pcnt"
    else
      echo "PASS  $pcnt"
    fi
  else
    echo "NO-FRAMES"
  fi
}

for entry in $LV; do
  name=${entry%%:*}; num=${entry##*:}
  status=$(run_level "$name" "$num")

  # NO-FRAMES and STALLED are the flaky outcomes (D117 + host load) -- retry
  # once before believing them. A CRASH is deterministic enough to report as-is.
  case "$status" in
    NO-FRAMES|STALLED*)
      sleep 3
      retry=$(run_level "$name" "$num")
      case "$retry" in
        NO-FRAMES)  status="NO-FRAMES (2 attempts)" ;;
        STALLED*)   status="$retry (2 attempts)" ;;
        *)          status="$retry  [passed on retry]" ;;
      esac
      ;;
  esac

  echo "$name ($num): $status" | tee -a $OUT
done
echo "SWEEP DONE"
