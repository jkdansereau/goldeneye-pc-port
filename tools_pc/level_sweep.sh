#!/bin/bash
cd "$(dirname "$0")/.."
LV="Dam:33 Facility:34 Runway:35 Surface1:36 Bunker1:09 Silo:20 Frigate:26 Surface2:43 Bunker2:27 Statue:22 Archives:24 Streets:29 Depot:30 Train:25 Jungle:37 Control:23 Caverns:39 Cradle:41 Aztec:28 Egypt:32 Cuba:54"
OUT=/tmp/sweep_results
mkdir -p /tmp/sweeplogs
: > $OUT
rm -f /tmp/sweeplogs/*.crash.log
for entry in $LV; do
  name=${entry%%:*}; num=${entry##*:}
  rm -f ge007.crash.log; rm -rf ppm
  export PATH="/c/msys64/mingw64/bin:$PATH"; GE_PCDUMP="80-260:40" ./build-pc/ge007.x86_64.exe -level_$num > /tmp/sweeplogs/$name.log 2>&1 &
  pid=$!
  sleep 24
  taskkill //F //IM ge007.x86_64.exe > /dev/null 2>&1
  sleep 2
  status="UNKNOWN"
  if [ -f ge007.crash.log ]; then
    pc=$(grep -oE '0x[0-9a-fA-F]+' ge007.crash.log | head -1)
    sym=$(addr2line -e build-pc/ge007.x86_64.exe -f -C "$pc" 2>/dev/null | head -1)
    status="CRASH @ $sym ($pc)"
    cp ge007.crash.log /tmp/sweeplogs/$name.crash.log
  else
    last=$(ls ppm/*.ppm 2>/dev/null | tail -1)
    if [ -n "$last" ]; then
      pcnt=$(python tools_pc/pixcount.py "$last" 2>/dev/null | grep -oE 'non-clear [0-9]+ \([0-9.]+%\)')
      status="PASS  $pcnt"
    else
      status="NO-FRAMES"
    fi
  fi
  echo "$name ($num): $status" | tee -a $OUT
done
echo "SWEEP DONE"
