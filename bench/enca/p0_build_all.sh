#!/bin/bash
# P0: build all three trees sequentially.
set -e
for t in a b c; do
  echo "=== BUILD $t ==="
  cd ~/enca-p0-$t
  if make -j20 > build.log 2>&1; then
    echo "BUILD OK $t"
    ls -la src/emacs | awk '{print $5, $9}'
  else
    echo "BUILD FAIL $t"
    grep -E 'error:' build.log | head -15
    exit 1
  fi
done
