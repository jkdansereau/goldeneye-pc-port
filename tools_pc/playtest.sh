#!/bin/bash
# Playtest launcher for docs/LEVEL-PLAYTEST.md (WS6 human validation).
#
# Usage:
#   ./tools_pc/playtest.sh <level> [difficulty-note]   launch a level
#   ./tools_pc/playtest.sh --list                      print the level table
#
#   <level> is the -level_XX number (33) OR a level name (dam, "surface 1",
#   bunker2 — case/space/underscore insensitive).
#
# Launches the game bare (-level_XX, pools auto-injected per D121), tees all
# output to build-pc/playtest_L<XX>_<HHMMSS>.log, and on a crash resolves the
# faulting PC(s) from ge007.crash.log with addr2line automatically.
# Human plays start-to-finish; close the window when done.

set -e
cd "$(dirname "$0")/.."
export PATH="/c/msys64/mingw64/bin:$PATH"

# name -> -level_XX  (mission order; docs/LEVEL-PLAYTEST.md)
LEVELS=(
  "dam:33" "facility:34" "runway:35" "surface1:36" "bunker1:09" "silo:20"
  "frigate:26" "surface2:43" "bunker2:27" "statue:22" "archives:24"
  "streets:29" "depot:30" "train:25" "jungle:37" "control:23" "caverns:39"
  "cradle:41" "aztec:28" "egypt:32" "cuba:54"
)

print_list() {
    printf '%3s  %-10s  %s\n' "#" "-level_XX" "level"
    local i=1
    for e in "${LEVELS[@]}"; do
        printf '%3d  -level_%-4s  %s\n' "$i" "${e##*:}" "${e%%:*}"
        i=$((i + 1))
    done
}

case "${1:-}" in
    ""|-h|--help)      echo "usage: playtest.sh <level|--list> [difficulty]"; print_list; exit 0 ;;
    --list|list|list-levels) print_list; exit 0 ;;
esac

# resolve arg -> two-digit level number
ARG="$1"
DIFF="${2:-Agent}"
norm() { echo "$1" | tr 'A-Z' 'a-z' | tr -d ' _-'; }
LEVEL=""
if [[ "$ARG" =~ ^[0-9]+$ ]]; then
    LEVEL=$(printf '%02d' "$ARG")
else
    key=$(norm "$ARG")
    for e in "${LEVELS[@]}"; do
        [ "$(norm "${e%%:*}")" = "$key" ] && LEVEL="${e##*:}"
    done
fi
if [ -z "$LEVEL" ]; then
    echo "unknown level '$ARG'"; print_list; exit 1
fi

if [ ! -x build-pc/ge007.x86_64.exe ]; then
    echo "binary missing — building..."
    ./build-pc.sh ntsc-final
fi

LOG="build-pc/playtest_L${LEVEL}_$(date +%H%M%S).log"
echo "== playtest level ${LEVEL} (difficulty: ${DIFF}) =="
echo "== log: ${LOG}"
echo "== checklist: docs/LEVEL-PLAYTEST.md — spawn, geometry, guards,"
echo "==   doors/pickups/objectives, alarms, exit -> MISSION COMPLETE, auto-advance"

rm -f ge007.crash.log
set +e
./build-pc/ge007.x86_64.exe "-level_${LEVEL}" 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e

echo "==============================="
if [ -f ge007.crash.log ]; then
    echo "RESULT: CRASH"
    cp ge007.crash.log "${LOG%.log}.crash.log"
    for pc in $(grep -oE '0x1[0-9a-fA-F]{8,}' ge007.crash.log | head -6); do
        sym=$(addr2line -e build-pc/ge007.x86_64.exe -f -C "$pc" 2>/dev/null | paste -sd' ' -)
        echo "  $pc  ->  $sym"
    done
    echo "  (image base 0x140000000; full log: ${LOG%.log}.crash.log)"
elif grep -q "FATAL: EXCEPTION" "$LOG"; then
    echo "RESULT: CRASH (see log — no ge007.crash.log written)"
    grep -A3 "FATAL: EXCEPTION" "$LOG" | head -8
elif grep -qi "mission complete" "$LOG"; then
    echo "RESULT: MISSION COMPLETE seen in log"
else
    echo "RESULT: exited rc=${RC} (no crash marker; check log + your notes)"
fi
echo "log: ${LOG}"
