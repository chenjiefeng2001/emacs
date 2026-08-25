#!/bin/bash
# EIPB Phase 3.1 runner: IDE-MIXED-01 across builds, randomized
# round-robin (doctrine 7+8).  Usage: eipb_p31_run.sh [sessions]
set -u
REPO=/mnt/c/Users/14977/source/repos/emacs
CORE=$REPO/bench/eipb/phase3/eipb_p31_core.el
LOG=${EIPB_P31_LOG:-$REPO/bench/results/eipb_t31.log}
ROUNDS=${1:-2}
: > "$LOG"
echo "EIPB3_MIXED_BEGIN|$(date +%s)|sessions=$ROUNDS|order=randomized" >> "$LOG"

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
  order=$(shuf -e A B C D | tr '\n' ' ')
  echo "EIPB3_ROUND|$r|order=$order" >> "$LOG"
  for lbl in $order; do run_one "$lbl" "r$r"; done
done
echo "EIPB3_MIXED_DONE|$(date +%s)" >> "$LOG"
echo DONE; grep -a 'FATAL\|skip' "$LOG" | head -3 || true
