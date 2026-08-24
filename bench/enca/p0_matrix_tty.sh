#!/bin/bash
# P0 tty-only matrix: {A,B,C} x 3 rounds, results via log file.
OUT=/mnt/c/Users/14977/source/repos/emacs/bench/results/p0_baseline_tty.csv
echo "build,suite,cell,rep,ms" > $OUT

for round in 1 2 3; do
  for t in a b c; do
    cd ~/enca-p0-$t || continue
    export TERM=xterm-256color
    script -qec "./src/emacs -nw -Q --no-site-file -l /mnt/c/Users/14977/source/repos/emacs/test/enca/p0-bench-tty.el" /dev/null > /dev/null 2>&1
    if [ -f /tmp/p0_tty.log ]; then
      sed "s/^P0T|/$t,tty,round$round,/" /tmp/p0_tty.log | tr -d '\r' >> $OUT
    fi
  done
done
echo "LINES=$(( $(wc -l < $OUT) - 1 ))"
