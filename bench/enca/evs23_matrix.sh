#!/bin/bash
# EVS-2.3 full A/B matrix run.
cd ~/enca-p11
for m in full incr; do
  echo "===== ARM $m ====="
  EVS23_MODES=$m ./src/emacs --batch -l /mnt/c/Users/14977/source/repos/emacs/test/enca/evs23-bench.el 2>&1
done
