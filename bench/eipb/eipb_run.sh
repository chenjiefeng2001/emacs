#!/bin/bash
# EIPB Tier-1 runner: core interactive benchmark across builds
# A (vanilla) / B (disabled) / C (enabled @P0) / D (current full ENCA).
# Contract: bench/eipb/EIPB.md.  Output: bench/results/eipb_t1.log.
set -u
REPO=/mnt/c/Users/14977/source/repos/emacs
LOG=$REPO/bench/results/eipb_t1.log
: > "$LOG"

run_build () { # <label> <dir>
  local label=$1 dir=$2
  if ! test -x "$dir/src/emacs"; then
    echo "EIPB|$label|SKIP|missing|$dir" >> "$LOG"
    return 0
  fi
  echo "== build $label ($dir)"
  export EIPB_BUILD=$label EIPB_LOG=$LOG
  # batch startup x3 (fs-warm by run 3)
  for i in 1 2 3; do
    t0=$(date +%s%N)
    "$dir/src/emacs" --batch -Q --no-site-file \
      -l "$REPO/bench/eipb/eipb_batch_probe.el" >/dev/null 2>&1 || true
    t1=$(date +%s%N)
    echo "EIPB|$label|startup/batch-run$i|wall_ms|$(( (t1-t0)/1000000 ))" >> "$LOG"
  done
  # tty startup cold + warm (shell wall; milestones inside log)
  for i in cold warm; do
    t0=$(date +%s%N)
    TERM=xterm-256color timeout 60 script -qec \
      "$dir/src/emacs -nw -Q --no-site-file -l $REPO/bench/eipb/eipb_startup.el" \
      /dev/null >/dev/null 2>&1 || true
    t1=$(date +%s%N)
    echo "EIPB|$label|startup/tty-$i|wall_ms|$(( (t1-t0)/1000000 ))" >> "$LOG"
  done
  # core harness (tty)
  TERM=xterm-256color timeout 300 script -qec \
    "$dir/src/emacs -nw -Q --no-site-file -l $REPO/bench/eipb/eipb_core.el" \
    /dev/null >> /dev/null 2>&1 || true
}

run_build A ~/enca-p0-a
run_build B ~/enca-p0-b
run_build C ~/enca-p0-c
run_build D ~/enca-p11
echo "EIPB_RUN_DONE" >> "$LOG"
echo DONE; grep -c '^EIPB' "$LOG"
