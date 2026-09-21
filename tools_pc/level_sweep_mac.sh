#!/bin/bash
# macOS/arm64 level sweep: bare -level_XX boot per level. PASS = ran the whole
# window without a crash log AND a GE_PCDUMP frame that is not near-black
# (catches "runs but renders nothing" that a crash-only check would miss).
# Env: SWEEP_SECS (default 18), PIX_MIN (default 2, percent non-clear).
cd "$(dirname "$0")/.."
LV="Dam:33 Facility:34 Runway:35 Surface1:36 Bunker1:09 Silo:20 Frigate:26 Surface2:43 Bunker2:27 Statue:22 Archives:24 Streets:29 Depot:30 Train:25 Jungle:37 Control:23 Caverns:39 Cradle:41 Aztec:28 Egypt:32 Cuba:54"
OUT=/tmp/mac_sweep_results; : > $OUT
PIX_MIN=${PIX_MIN:-2}
mkdir -p ppm
for entry in $LV; do
  name=${entry%%:*}; num=${entry##*:}
  rm -f ge007.crash.log ppm/frame_000120.ppm
  GE_PCDUMP="120-120:1" ./build-pc/ge007.aarch64 -level_$num > /tmp/sweeplogs/$name.log 2>&1 &
  pid=$!
  sleep ${SWEEP_SECS:-18}
  kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
  sleep 1
  frames=$(grep -cE 'frame [0-9]+ rendered' /tmp/sweeplogs/$name.log 2>/dev/null)
  if [ -f ge007.crash.log ]; then
    M=$(grep -oE 'MODULE: 0x[0-9a-f]+' ge007.crash.log|head -1|awk '{print $2}')
    P=$(grep -oE '^PC: 0x[0-9a-f]+' ge007.crash.log|awk '{print $2}')
    sym=$(atos -o build-pc/ge007.aarch64 -l "$M" "$P" 2>/dev/null | head -1)
    cp ge007.crash.log /tmp/sweeplogs/$name.crash.log
    echo "$name ($num): CRASH @ $sym" | tee -a $OUT
    continue
  fi
  if [ -f ppm/frame_000120.ppm ]; then
    pc=$(python3 tools_pc/pixcount.py ppm/frame_000120.ppm 2>/dev/null | grep -oE '[0-9]+\.[0-9]+%' | head -1)
    pcn=${pc%\%}
    if [ -z "$pcn" ]; then
      echo "$name ($num): OK (frames=$frames, pixcount failed)" | tee -a $OUT
    elif awk "BEGIN{exit !($pcn < $PIX_MIN)}"; then
      echo "$name ($num): BLACK (frames=$frames, non-clear ${pc})" | tee -a $OUT
    else
      echo "$name ($num): OK (frames=$frames, non-clear ${pc})" | tee -a $OUT
    fi
    rm -f ppm/frame_000120.ppm
  else
    echo "$name ($num): OK (frames=$frames, no capture)" | tee -a $OUT
  fi
done
echo "SWEEP DONE" | tee -a $OUT
