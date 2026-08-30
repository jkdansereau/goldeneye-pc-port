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
# Env overrides:
#   SWEEP_SECS=45          per-level watchdog seconds
#   SWEEP_DUMP=80-400:40   GE_PCDUMP window
#   SWEEP_LEVELS="Dam:33"  subset to run
cd "$(dirname "$0")/.."

SWEEP_SECS=${SWEEP_SECS:-45}
SWEEP_DUMP=${SWEEP_DUMP:-80-400:40}
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

  # watchdog: poll so a clean early exit does not cost the full window
  local waited=0
  while [ $waited -lt $SWEEP_SECS ] && kill -0 $pid 2>/dev/null; do
    sleep 1; waited=$((waited + 1))
  done
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
    echo "PASS  $pcnt"
  else
    echo "NO-FRAMES"
  fi
}

for entry in $LV; do
  name=${entry%%:*}; num=${entry##*:}
  status=$(run_level "$name" "$num")

  # NO-FRAMES is the flaky outcome (D117 + host load) -- retry once before
  # believing it. A CRASH is deterministic enough to report as-is.
  if [ "$status" = "NO-FRAMES" ]; then
    sleep 3
    retry=$(run_level "$name" "$num")
    if [ "$retry" = "NO-FRAMES" ]; then
      status="NO-FRAMES (2 attempts)"
    else
      status="$retry  [passed on retry]"
      [ -f $LOGS/$name.crash.log ] || true
    fi
  fi

  echo "$name ($num): $status" | tee -a $OUT
done
echo "SWEEP DONE"
