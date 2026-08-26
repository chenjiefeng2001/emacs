#!/bin/bash
# EIPB Phase 4.1 SOAK runner: one continuous session per build,
# block order shuffled (round-robin adapted to long runs: blocks
# cannot be interleaved without breaking continuity).
# Usage: eipb_soak_run.sh [blocks] [secs-per-block]
#   blocks default 4 (A,B,C,D); secs default 1800.
# Sidecar RSS sampling every 60 s into results/eipb_t41_rss.csv.
set -u
REPO=/mnt/c/Users/14977/source/repos/emacs
CORE=$REPO/bench/eipb/phase4/eipb_soak_core.el
LOG=${EIPB_T41_LOG:-$REPO/bench/results/eipb_t41.log}
RSS=${EIPB_T41_RSS:-$REPO/bench/results/eipb_t41_rss.csv}
BLOCKS=${1:-4}
SECS=${2:-1800}
GRACE=$(( SECS + 600 ))
: > "$LOG"
: > "$RSS"
echo "EIPB4_RUN_BEGIN|$(date +%s)|blocks=$BLOCKS|secs=$SECS|order=randomized" >> "$LOG"

dir_of () {
  case $1 in
    A) echo ~/enca-p0-a ;;  B) echo ~/enca-p0-b ;;
    C) echo ~/enca-p0-c ;;  D) echo ~/enca-p11 ;;
  esac
}

run_block () { # <label> <secs>
  local label=$1 secs=$2 dir epid rc before after
  dir=$(dir_of "$label")
  if ! test -x "$dir/src/emacs"; then
    echo "EIPB4|$label|skip|missing|0" >> "$LOG"; return 0
  fi
  before=$(grep -ac "^EIPB4|$label|" "$LOG" || true)
  echo "EIPB4|$label|block|begin|$(date +%s)" >> "$LOG"
  TERM=xterm-256color EIPB_BUILD=$label EIPB_LOG=$LOG \
    EIPB_SOAK_SECS=$secs \
    timeout "$GRACE" script -qec \
    "$dir/src/emacs -nw -Q --no-site-file -l $CORE" \
    /dev/null >/dev/null 2>&1 &
  local spid=$!
  # wait for emacs pid line, then sample RSS every 60 s
  sleep 20
  epid=$(awk -F'|' -v b=$label '$1=="EIPB4" && $2==b && $3=="meta" && $4=="emacs_pid"{v=$5} END{print v}' "$LOG")
  epid=${epid%%.*}   # defensive int-cast (pid must never be float)
  if test -z "$epid" || ! kill -0 "$epid" 2>/dev/null; then
    epid=$(pgrep -f "$dir/src/emacs.*eipb_soak_core" | sed -n 1p)
  fi
  echo "EIPB4|$label|meta|rss_pid|$epid" >> "$LOG"
  while kill -0 "$spid" 2>/dev/null; do
    if test -n "$epid" && kill -0 "$epid" 2>/dev/null; then
      rss=$(ps -o rss= -p "$epid" 2>/dev/null | tr -d ' ')
      test -n "$rss" && \
        echo "$label,$(date +%s),$rss" >> "$RSS"
    fi
    sleep 60
  done
  wait "$spid"; rc=$?
  # orphan insurance: a timed-out script may leave emacs spinning,
  # contaminating every later block (97% CPU observed in smoke)
  pkill -f "$dir/src/emacs.*eipb_soak_core" 2>/dev/null && sleep 2 || true
  after=$(grep -ac "^EIPB4|$label|" "$LOG" || true)
  if grep -aq "^EIPB4|$label|teardown|msg|exit-clean" "$LOG"; then
    echo "EIPB4|$label|teardown|status|clean" >> "$LOG"
  else
    echo "EIPB4|$label|teardown|status|HANG_OR_CRASH_rc=$rc" >> "$LOG"
  fi
  echo "EIPB4|$label|block|end_lines|$(( after - before ))" >> "$LOG"
}

# shuffle block order once for the whole run
order=$(shuf -e A B C D)
echo "EIPB4_ORDER|$order" >> "$LOG"
count=0
for lbl in $order; do
  count=$(( count + 1 ))
  test "$count" -gt "$BLOCKS" && break
  run_block "$lbl" "$SECS"
done
echo "EIPB4_RUN_DONE|$(date +%s)" >> "$LOG"
echo DONE; grep -a 'FATAL\|HANG\|skip' "$LOG" | head -5 || true
