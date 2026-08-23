#!/bin/bash
# EVS-2.3: sync ENCA sources from the Windows checkout and rebuild.
set -e
cd ~/enca-p11
cp /mnt/c/Users/14977/source/repos/emacs/src/enca-evs.c src/
rm -rf src/enca
cp -r /mnt/c/Users/14977/source/repos/emacs/src/enca src/enca
if make -j20 > build.log 2>&1; then
  echo BUILD_OK
else
  echo BUILD_FAIL
  grep -E 'error:' build.log | head -30
  exit 1
fi
