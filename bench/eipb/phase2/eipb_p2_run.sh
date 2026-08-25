#!/bin/bash
# EIPB Phase 2 runner: tail-latency attribution (Q1 font-lock,
# Q2 GC-in-path, Q3 window/buffer).  Contract: EIPB.md section 7.
#
# Session-2 lessons encoded here:
#   - one emacs invocation PER SECTION with its own timeout, so a slow
#     FL cell (python 1MB cold fontify ~280 s) can never starve Q2/Q3;
#   - the log is truncated per script run and stamped with markers;
#   - SECTION_DONE lines are emitted per section, not per build.
#
# Usage: eipb_p2_run.sh [LABEL=DIR ...]   default: D then A
set -u
REPO=/mnt/c/Users/14977/source/repos/emacs
CORE=$REPO/bench/eipb/phase2/eipb_p2_core.el
LOG=${EIPB_P2_LOG:-$REPO/bench/results/eipb_t2.log}
: > "$LOG"
echo "EIPB2_RUN_BEGIN|$(date +%s)|sections=FL,GCPATH,WB|builds=${*:-D,A}" >> "$LOG"

run_section () { # <label> <dir> <section> <timeout>
  local label=$1 dir=$2 section=$3 tmo=$4 before after
  before=$(grep -ac "^EIPB2|$label|" "$LOG" || true)
  TERM=xterm-256color EIPB_BUILD=$label EIPB_LOG=$LOG \
    EIPB_P2_SECTION=$section \
    timeout "$tmo" script -qec \
    "$dir/src/emacs -nw -Q --no-site-file -l $CORE" \
    /dev/null >/dev/null 2>&1 || true
  after=$(grep -ac "^EIPB2|$label|" "$LOG" || true)
  echo "EIPB2|$label|$section|SECTION_DONE_lines|$(( after - before ))" >> "$LOG"
}

run_build () { # <label> <dir>
  local label=$1 dir=$2
  if ! test -x "$dir/src/emacs"; then
    echo "EIPB2|$label|SKIP|missing|0" >> "$LOG"
    return 0
  fi
  echo "== p2 build $label ($dir)"
  run_section "$label" "$dir" FL     4500
  run_section "$label" "$dir" GCPATH 900
  run_section "$label" "$dir" WB     1800
}

if [ $# -gt 0 ]; then
  for spec in "$@"; do
    label=${spec%%=*}; dir=${spec#*=}
    run_build "$label" "$dir"
  done
else
  run_build D ~/enca-p11
  run_build A ~/enca-p0-a
fi
echo "EIPB2_RUN_DONE|$(date +%s)" >> "$LOG"
echo DONE; grep -a 'FATAL\|error' "$LOG" | tail -5 || true
