#!/bin/bash
# Smoke both arms at small sizes.
cd ~/enca-p11
for m in full incr; do
  echo "===== ARM $m ====="
  EVS23_MODES=$m EVS23_MAX_MB=1 ./src/emacs --batch -l /mnt/c/Users/14977/source/repos/emacs/test/enca/evs23-bench.el 2>&1 | tail -30
done
