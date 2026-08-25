#!/bin/bash
# EIPB Phase 2.1 runner: B/C attribution on the mid-buffer signal.
# Two interleaved rounds x builds A,B,C,D (drift-cancelling order).
# Usage: eipb_p21_run.sh [rounds]   default 2
set -u
REPO=/mnt/c/Users/14977/source/repos/emacs
CORE=$REPO/bench/eipb/phase2/eipb_p21_core.el
LOG=${EIPB_P21_LOG:-$REPO/bench/results/eipb_p21.log}
ROUNDS=${1:-2}
: > "$LOG"
echo "EIPB2_P21_BEGIN|$(date +%s)|rounds=$ROUNDS|order=A,B,C,D" >> "$LOG"

run_one () { # <label> <dir> <round>
  local label=$1 dir=$2 round=$3 before after
  if ! test -x "$dir/src/emacs"; then
    echo "EIPB2|$label|p21/skip|r$round|0" >> "$LOG"; return 0
  fi
  before=$(grep -ac "^EIPB2|$label|" "$LOG" || true)
  TERM=xterm-256color EIPB_BUILD=$label EIPB_LOG=$LOG \
    timeout 600 script -qec \
    "$dir/src/emacs -nw -Q --no-site-file -l $CORE" \
    /dev/null >/dev/null 2>&1 || true
  after=$(grep -ac "^EIPB2|$label|" "$LOG" || true)
  echo "EIPB2|$label|p21/round$r$round|SECTION_DONE_lines|$(( after - before ))" >> "$LOG"
}

for r in $(seq 1 "$ROUNDS"); do
  run_one A ~/enca-p0-a "r$r"
  run_one B ~/enca-p0-b "r$r"
  run_one C ~/enca-p0-c "r$r"
  run_one D ~/enca-p11 "r$r"
done
echo "EIPB2_P21_DONE|$(date +%s)" >> "$LOG"
echo DONE; grep -a 'FATAL\|skip' "$LOG" | head -3 || true
