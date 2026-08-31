#!/bin/sh
# Attach to a RUNNING ge007 (not owned by another gdb) and dump the
# Facility-outro-hang animation state (modelSetAnimFrame2WithChrStuff).
# The game keeps running after detach.
export PATH="/c/msys64/mingw64/bin:$PATH"
PID=$(ps -W | grep ge007.x86_64.exe | awk '{print $4}' | head -1)
if [ -z "$PID" ]; then
    echo "no ge007.x86_64.exe process found"
    exit 1
fi
echo "attaching to winpid $PID"
gdb -batch -p "$PID" -x "$(dirname "$0")/dump_animgen.cmd" 2>&1 | grep -v "DBGHELP\|warning:"
