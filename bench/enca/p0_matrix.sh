#!/bin/bash
# P0 matrix: {A,B,C} x {batch, tty} x 3 rounds -> CSV on /mnt/c.
OUT=/mnt/c/Users/14977/source/repos/emacs/bench/results/p0_baseline.csv
echo "build,suite,cell,rep,ms" > $OUT

run_batch () {
  local t=$1 round=$2
  cd ~/enca-p0-$t || return
  ./src/emacs --batch -Q -l /mnt/c/Users/14977/source/repos/emacs/test/enca/p0-bench-batch.el 2>&1 \
   | grep -a '^P0B|' | sed "s/^P0B|/$t,batch,round$round,/" | tr -d '\r' \
   | awk -F'|' '{printf "%s,%s,%s,%s,%s\n",$1,$2,$3,$4,$5}' >> $OUT
}

run_tty () {
  local t=$1 round=$2
  cd ~/enca-p0-$t || return
  export TERM=xterm-256color
  script -qec "./src/emacs -nw -Q --no-site-file -l /mnt/c/Users/14977/source/repos/emacs/test/enca/p0-bench-tty.el" /dev/null 2>&1 \
   | tr -s '\r' '\n' | grep -a '^P0T|' | sed "s/^P0T|/$t,tty,round$round,/" | tr -d '\r' \
   | awk -F'|' '{printf "%s,%s,%s,%s,%s\n",$1,$2,$3,$4,$5}' >> $OUT
}

for round in 1 2 3; do
  for t in a b c; do
    echo "== $t round $round batch =="
    run_batch $t $round
    echo "== $t round $round tty =="
    run_tty $t $round
  done
done
echo "LINES=$(( $(wc -l < $OUT) - 1 ))"
