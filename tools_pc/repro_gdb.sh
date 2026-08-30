#!/bin/bash
# Repro harness: launch a level under gdb-attach, dump a full backtrace on the
# first access violation (0xc0000005 / SIGSEGV). Human plays; close nothing --
# gdb prints the trace and detaches when it faults.
#
# Usage: ./tools_pc/repro_gdb.sh <XX>   e.g. ./tools_pc/repro_gdb.sh 34
set -e
cd "$(dirname "$0")/.."
export PATH="/c/msys64/mingw64/bin:$PATH"
LEVEL="${1:?usage: repro_gdb.sh <XX>}"
STAMP=$(date +%H%M%S)
LOG="build-pc/repro_L${LEVEL}_${STAMP}.log"
GDBLOG="build-pc/repro_L${LEVEL}_${STAMP}.gdb.txt"
export GE_PCDUMP="1-99999:30"   # rolling frame dump -> ppm/ (last few = pre-crash)

cat > "build-pc/repro_cmds_${STAMP}.txt" <<'EOF'
set pagination off
set width 0
set confirm off
handle SIGSEGV stop print nopass
continue
echo \n\n=================== FAULT CAUGHT ===================\n
bt
echo \n--- frame 0 ---\n
frame 0
info args
info locals
echo \n--- full backtrace ---\n
bt full
echo \n--- registers ---\n
info registers rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11
echo \n===================================================\n
detach
quit
EOF

echo "== launching -level_${LEVEL} (nohup); play a firefight to trigger the crash =="
nohup ./build-pc/ge007.x86_64.exe "-level_${LEVEL}" > "$LOG" 2>&1 &
BASHPID=$!
sleep 6
# winpid = 4th column of ps -p <bashpid>
WINPID=$(ps -p "$BASHPID" | awk 'NR==2{print $4}')
echo "== bash pid=$BASHPID  winpid=$WINPID  attaching gdb =="
echo "== game log: $LOG"
echo "== gdb  log: $GDBLOG"
gdb -batch -x "build-pc/repro_cmds_${STAMP}.txt" -p "$WINPID" 2>&1 | tee "$GDBLOG"
echo "== done. crash log: ge007.crash.log ; pre-crash frames: ppm/ =="
