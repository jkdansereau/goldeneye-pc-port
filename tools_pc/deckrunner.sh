#!/usr/bin/env bash
# deckrunner.sh -- Game Mode job runner for tools_pc/levelbench.sh.
# Gamescope only shows windows Steam launched, so a harness started over ssh
# runs hidden (and throttled). Add this script to Steam once as a non-Steam
# game and start it; it then runs queued benches as its own children, which
# gamescope shows and paces like a normal game.
#
# Queue a job over ssh: one line of levelbench.sh arguments per file, e.g.
#   echo '--secs 45 bunker1' > ~/lb/queue/010-bunker1.job
# Jobs run in name order; results land in ~/lb/<job-name>/ (summary.txt),
# the job file moves to ~/lb/done/. `touch ~/lb/queue/stop` exits the runner
# (back to Steam). Idle-exits after 30 min with no jobs.
set -u
Q=~/lb/queue; D=~/lb/done
mkdir -p "$Q" "$D"
GAME=${GE_GAMEDIR:-$HOME/ge007-v050}
idle=0
echo "deckrunner: up $(date)" >> ~/lb/runner.log
while [ $idle -lt 1800 ]; do
    if [ -e "$Q/stop" ]; then rm -f "$Q/stop"; break; fi
    job=$(ls "$Q"/*.job 2>/dev/null | sort | head -1)
    if [ -z "$job" ]; then sleep 2; idle=$((idle + 2)); continue; fi
    idle=0
    name=$(basename "$job" .job)
    args=$(head -1 "$job")
    mv "$job" "$D/"
    echo "deckrunner: $name: $args" >> ~/lb/runner.log
    rm -rf ~/lb/"$name"
    # shellcheck disable=SC2086  # job line is word-split on purpose
    bash ~/levelbench.sh --dir "$GAME" --out ~/lb/"$name" $args > ~/lb/"$name".console 2>&1
    echo "deckrunner: $name done" >> ~/lb/runner.log
    touch ~/lb/"$name".finished
done
echo "deckrunner: exit $(date)" >> ~/lb/runner.log
