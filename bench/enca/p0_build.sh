#!/bin/bash
# P0: build one tree.  Usage: p0_build.sh <dir-name-suffix a|b|c>
set -e
cd ~/enca-p0-$1
if make -j20 > build.log 2>&1; then
  echo "BUILD OK $1"
else
  echo "BUILD FAIL $1"
  grep -E 'error:' build.log | head -15
  exit 1
fi
ls -la src/emacs
