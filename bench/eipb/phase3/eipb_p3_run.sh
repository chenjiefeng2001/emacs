#!/bin/bash
# EIPB Phase 3 runner: user-path coverage (T3-A file open chain,
# T3-B interactive isearch).  Doctrine 7+8: RANDOMIZED round-robin
# build order, N independent sessions (default 3).
# Usage: eipb_p3_run.sh [sessions]   default 3
set -u
REPO=/mnt/c/Users/14977/source/repos/emacs
CORE=$REPO/bench/eipb/phase3/eipb_p3_core.el
LOG=${EIPB_P3_LOG:-$REPO/bench/results/eipb_t3.log}
ROUNDS=${1:-3}
: > "$LOG"
echo "EIPB3_RUN_BEGIN|$(date +%s)|sessions=$ROUNDS|order=randomized" >> "$LOG"

dir_of () {
  case $1 in
    A) echo ~/enca-p0-a ;;  B) echo ~/enca-p0-b ;;
    C) echo ~/enca-p0-c ;;  D) echo ~/enca-p11 ;;
  esac
}

run_one () { # <label> <round>
  local label=$1 round=$2 dir before after
  dir=$(dir_of "$label")
  if ! test -x "$dir/src/emacs"; then
    echo "EIPB3|$label|skip|$round|0" >> "$LOG"; return 0
  fi
  before=$(grep -ac "^EIPB3|$label|" "$LOG" || true)
  TERM=xterm-256color EIPB_BUILD=$label EIPB_LOG=$LOG \
    timeout 900 script -qec \
    "$dir/src/emacs -nw -Q --no-site-file -l $CORE" \
    /dev/null >/dev/null 2>&1 || true
  after=$(grep -ac "^EIPB3|$label|" "$LOG" || true)
  echo "EIPB3|$label|$round|SECTION_DONE_lines|$(( after - before ))" >> "$LOG"
}

for r in $(seq 1 "$ROUNDS"); do
  order=$(shuf -e A B C D)
  echo "EIPB3_ROUND|$r|order=$order" >> "$LOG"
  for lbl in $order; do run_one "$lbl" "r$r"; done
done
echo "EIPB3_RUN_DONE|$(date +%s)" >> "$LOG"
echo DONE; grep -a 'FATAL\|skip' "$LOG" | head -3 || true
