#!/usr/bin/env bash
# levelbench.sh -- boot one solo level, run a scripted input pattern, quit
# cleanly, and keep the log + a short summary. Runs on the machine with the
# game (Windows Git Bash/MSYS2, Linux, Steam Deck). For the Deck from the PC
# use tools_pc/deckbench.sh, which copies this script over and runs it.
#
#   levelbench.sh [options] <level-number|name> [ENV=VAL ...]
#     --secs N        run length in game seconds (default 45; GE_QUITFRAME = 60*N)
#     --script NAME   fight (default) | idle | turn | raw:<GE_INPUTSCRIPT string>
#     --exe PATH      game binary (default: ./ge007.x86_64[.exe] in --dir)
#     --dir DIR       game dir holding data/ (default: cwd)
#     --out DIR       results dir (default: <dir>/levelbench/<level>-<time>)
#     --record FILE   human plays (no script); pad input saved to FILE
#     --replay FILE   replay a --record file instead of a script
#                     (both pin GE_RSEED so guards make the same choices)
#     --gamescope     wrap in nested `gamescope -r 90 -w 1280 -h 800 -f`
#                     (Deck desktop mode: approximates Game Mode pacing)
#
# Script timing is wall-clock ms since SDL init (GE_INPUTSCRIPT_MS=1), so
# load-time differences between machines don't shift it. "fight": taps Z
# every 1.5 s for the first 20 s (skips the intro cutscene; fires once in
# play), then 1.2 s Z holds every 4 s, so guards hear shots and start their
# patterns. Z is fire in every control style and harmless in cutscenes.
set -u
SECS=45; SCRIPT=fight; REC=; REP=; EXE=; DIR=$PWD; OUT=; GSCOPE=0
while [ $# -gt 0 ]; do
    case $1 in
        --secs) SECS=$2; shift 2;;
        --script) SCRIPT=$2; shift 2;;
        --exe) EXE=$2; shift 2;;
        --dir) DIR=$2; shift 2;;
        --out) OUT=$2; shift 2;;
        --record) REC=$2; SCRIPT=none; shift 2;;
        --replay) REP=$2; SCRIPT=none; shift 2;;
        --gamescope) GSCOPE=1; shift;;
        -h|--help) sed -n 2,22p "$0"; exit 0;;
        *) break;;
    esac
done
[ $# -ge 1 ] || { echo "usage: $0 [options] <level> [ENV=VAL ...]" >&2; exit 2; }
LVL=$1; shift
case ${LVL,,} in
    dam) LVL=33;; facility) LVL=34;; runway) LVL=35;; surface1) LVL=36;;
    bunker1) LVL=09;; silo) LVL=20;; frigate) LVL=26;; surface2) LVL=43;;
    bunker2) LVL=27;; statue) LVL=22;; archives) LVL=24;; streets) LVL=29;;
    depot) LVL=30;; train) LVL=25;; jungle) LVL=37;; control) LVL=23;;
    caverns) LVL=39;; cradle) LVL=41;; aztec) LVL=28;; egypt) LVL=32;; cuba) LVL=54;;
esac
cd "$DIR" || exit 2
if [ -z "$EXE" ]; then
    for c in ./ge007.x86_64 ./ge007.x86_64.exe; do [ -x "$c" ] && EXE=$c && break; done
fi
[ -n "$EXE" ] || { echo "no game binary in $DIR (use --exe)" >&2; exit 2; }
END=$((SECS * 1000))

gen_script() {
    local s= t
    case $SCRIPT in
        none) ;;
        idle) s="1000:SNONE";;
        fight|turn)
            for ((t = 4000; t < 20000 && t < END; t += 1500)); do s+="$t:Z;"; done
            for ((t = 20000; t < END; t += 4000)); do
                s+="$t:ZHOLD;$((t + 1200)):ZREL;"
                if [ "$SCRIPT" = turn ]; then   # sweep the view between bursts
                    s+="$((t + 1500)):SLEFT;$((t + 2300)):SRIGHT;$((t + 3100)):SNONE;"
                fi
            done;;
        raw:*) s=${SCRIPT#raw:};;
        *) echo "unknown --script $SCRIPT" >&2; exit 2;;
    esac
    printf '%s' "${s%;}"
}
INPUT=$(gen_script)

[ -n "$OUT" ] || OUT="levelbench/L$LVL-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
# The game writes $S/ge007.log next to ge007.ini; find it after the run by mtime.
STAMP="$OUT/.start"; : > "$STAMP"

CMD=("$EXE" "-level_$LVL")
if [ "$GSCOPE" = 1 ]; then
    CMD=(gamescope -r 90 -w 1280 -h 800 -W 1280 -H 800 -f -- "${CMD[@]}")
fi
{
    echo "level=$LVL secs=$SECS script=$SCRIPT gamescope=$GSCOPE exe=$EXE"
    echo "binary md5=$(md5sum "$EXE" | cut -c1-8)"
    echo "extra env: $*"
    echo "GE_INPUTSCRIPT=$INPUT"
} > "$OUT/run.txt"

IENV=(GE_INPUTSCRIPT_MS=1 "GE_INPUTSCRIPT=$INPUT")
[ "$SCRIPT" = none ] && IENV=("GE_RSEED=${GE_RSEED:-0123456789abcdef}")
[ -n "$REC" ] && IENV+=("GE_INPUTRECORD=$REC")
[ -n "$REP" ] && IENV+=("GE_INPUTREPLAY=$REP")
echo "input env: ${IENV[*]}" >> "$OUT/run.txt"
env GE_QUITFRAME=$((SECS * 60)) "${IENV[@]}" "$@" \
    timeout $((SECS + 90)) "${CMD[@]}" > "$OUT/stdout.txt" 2>&1
RC=$?
echo "exit=$RC" >> "$OUT/run.txt"

LOG=$(find . -maxdepth 3 -name ge007.log -newer "$STAMP" 2>/dev/null | head -1)
[ -z "$LOG" ] && [ -f "${XDG_DATA_HOME:-$HOME/.local/share}/ge007/ge007.log" ] &&
    LOG="${XDG_DATA_HOME:-$HOME/.local/share}/ge007/ge007.log"
[ -n "$LOG" ] && cp "$LOG" "$OUT/ge007.log"
for f in ge007.crash.log; do [ -f "$f" ] && [ "$f" -nt "$STAMP" ] && cp "$f" "$OUT/"; done
rm -f "$STAMP"

L="$OUT/ge007.log"; [ -f "$L" ] || L="$OUT/stdout.txt"
{
    cat "$OUT/run.txt"
    echo "--- exit: $(grep -m1 -E 'quit requested|video: exiting' "$L" || echo 'no clean quit line')"
    [ -f "$OUT/ge007.crash.log" ] && echo "!!! CRASH LOG PRESENT"
    echo "--- D578 cadence (one per ~10 s)"
    grep -E 'D578 cadence' "$L" | sed 's/^.*D578 cadence/cadence/'
    echo "--- D583 decisions"
    grep -E 'D583 decisions' "$L" | sed 's/^.*D583 decisions/decisions/' | tail -5
    n=$(grep -c 'D583 SLOW' "$L")
    echo "--- D583 SLOW frames: $n (worst 8 by draw time)"
    grep 'D583 SLOW' "$L" | awk '{ for (i = 1; i <= NF; i++) if ($i == "draw") { print $(i+1), $0; break } }' |
        sort -rn | head -8 | cut -d' ' -f2- | sed 's/^.*D583 SLOW/SLOW/'
} > "$OUT/summary.txt"
cat "$OUT/summary.txt"
echo "results: $OUT"
