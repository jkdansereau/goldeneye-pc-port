#!/bin/bash
# Playtest launcher for docs/LEVEL-PLAYTEST.md (WS6 human validation).
# Usage: ./tools_pc/playtest.sh <XX> [difficulty-note]
#   e.g. ./tools_pc/playtest.sh 33 "Agent"
# Launches the game bare (-level_XX, pools auto-injected per D121), tees all
# output to build-pc/playtest_L<XX>_<HHMMSS>.log, and reports crash status
# on exit. Human plays start-to-finish; close the window when done.

set -e
cd "$(dirname "$0")/.."

LEVEL="${1:?usage: playtest.sh <XX> [difficulty]}"
DIFF="${2:-Agent}"
export PATH="/c/msys64/mingw64/bin:$PATH"

if [ ! -x build-pc/ge007.x86_64.exe ]; then
    echo "binary missing — building..."
    ./build-pc.sh ntsc-final
fi

LOG="build-pc/playtest_L${LEVEL}_$(date +%H%M%S).log"
echo "== playtest level ${LEVEL} (difficulty: ${DIFF}) =="
echo "== log: ${LOG}"
echo "== checklist: docs/LEVEL-PLAYTEST.md — spawn, geometry, guards,"
echo "==   doors/pickups/objectives, alarms, exit -> MISSION COMPLETE, auto-advance"

set +e
./build-pc/ge007.x86_64.exe "-level_${LEVEL}" 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e

echo "==============================="
if grep -q "FATAL: EXCEPTION" "$LOG"; then
    echo "RESULT: CRASH (see log)"
    grep -A3 "FATAL: EXCEPTION" "$LOG" | head -8
elif grep -qi "mission complete\|MISSION COMPLETE" "$LOG"; then
    echo "RESULT: MISSION COMPLETE seen in log"
else
    echo "RESULT: exited rc=${RC} (no crash marker; check log + your notes)"
fi
echo "log: ${LOG}"
