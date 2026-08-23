#!/bin/bash
# EVS-3: sync sources, patch generated Makefile with wake.o (the
# committed configure.ac is already correct; this regenerates-free
# path keeps the WSL build tree in sync), rebuild.
set -e
cd ~/enca-p11
cp /mnt/c/Users/14977/source/repos/emacs/src/enca-evs.c src/
rm -rf src/enca
cp -r /mnt/c/Users/14977/source/repos/emacs/src/enca src/enca
grep -q 'enca/wake/wake.o' src/Makefile || \
  sed -i 's|enca/snapshot/snapshot.o enca/scheduler/scheduler.o|enca/snapshot/snapshot.o enca/scheduler/scheduler.o enca/wake/wake.o|' src/Makefile
if make -j20 > build.log 2>&1; then
  echo BUILD_OK
else
  echo BUILD_FAIL
  grep -E 'error:|undefined' build.log | head -20
  exit 1
fi
